/*
 * Copyright (C) 1992 by Jim Weigand and Linus Torvalds
 * Copyright (C) 1992,1993 by Michael K. Johnson
 * - Thanks much to Gunter Windau for pointing out to me where the error
 *   checking ought to be.
 * Copyright (C) 1993 by Nigel Gamble (added interrupt code)
 * Copyright (C) 1994 by Alan Cox (Modularised it)
 * LPCAREFUL, LPABORT, LPGETSTATUS added by Chris Metcalf, metcalf@lcs.mit.edu
 * Statistics and support for slow printers by Rob Janssen, rob@knoware.nl
 * "lp=" command line parameters added by Grant Guenther, grant@torque.net
 * lp_read (Status readback) support added by Carsten Gross,
 *                                             carsten@sol.wohnheim.uni-ulm.de
 * Support for parport by Philip Blundell <Philip.Blundell@pobox.com>
 * Reverted interrupt to polling at runtime if more than one device is parport
 * registered and joined the interrupt and polling code.
 *                               by Andrea Arcangeli <arcangeli@mbox.queen.it>
 */

/* This driver is about due for a rewrite. */

#include <linux/module.h>

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/parport.h>
#include <linux/lp.h>

/* if you have more than 3 printers, remember to increase LP_NO */
struct lp_struct lp_table[] =
{
 {NULL, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, 0, 0, 0, 0,
  {0}},
 {NULL, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, 0, 0, 0, 0,
  {0}},
 {NULL, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, 0, 0, 0, 0,
  {0}}
};
#define LP_NO 3

/* Device name */
static char *dev_name = "lp";

/* Test if printer is ready (and optionally has no error conditions) */
#define LP_READY(minor, status) \
  ((LP_F(minor) & LP_CAREFUL) ? _LP_CAREFUL_READY(status) : (status & LP_PBUSY))
#define LP_CAREFUL_READY(minor, status) \
  ((LP_F(minor) & LP_CAREFUL) ? _LP_CAREFUL_READY(status) : 1)
#define _LP_CAREFUL_READY(status) \
   (status & (LP_PBUSY|LP_POUTPA|LP_PSELECD|LP_PERRORP)) == \
      (LP_PBUSY|LP_PSELECD|LP_PERRORP)

#undef LP_DEBUG
#undef LP_READ_DEBUG

/* Magic numbers */
#define AUTO -3
#define OFF -2
#define UNSPEC -1

static inline void lp_parport_release (int minor)
{
	parport_release (lp_table[minor].dev);
	lp_table[minor].should_relinquish = 0;
}

static inline void lp_parport_claim (int minor)
{
	if (parport_claim (lp_table[minor].dev))
		sleep_on (&lp_table[minor].lp_wait_q);
}

static inline void lp_schedule (int minor)
{
	if (lp_table[minor].should_relinquish) {
		lp_parport_release (minor);
		schedule ();
		lp_parport_claim (minor);
	}
	else
		schedule ();
}


static int lp_preempt (void *handle)
{
	struct lp_struct *lps = (struct lp_struct *)handle;

	/* Just remember that someone wants the port */
	lps->should_relinquish = 1;

	/* Don't actually release the port now */
	return 1;
}

static int lp_reset(int minor)
{
	w_ctr(minor, LP_PSELECP);
	udelay(LP_DELAY);
	w_ctr(minor, LP_PSELECP | LP_PINITP);
	return r_str(minor);
}

static inline int must_use_polling(int minor)
{
	return lp_table[minor].dev->port->irq == PARPORT_IRQ_NONE ||
                      lp_table[minor].dev->port->devices->next;
}

static inline int lp_char(char lpchar, int minor, int use_polling)
{
	int status;
	unsigned int wait = 0;
	unsigned long count = 0;
	struct lp_stats *stats;

	do {
		status = r_str(minor);
		count++;
		if (resched_needed())
			lp_schedule (minor);
	} while (((use_polling && !LP_READY(minor, status)) || 
		 (!use_polling && !(status & LP_PBUSY))) &&
		 (count < LP_CHAR(minor)));

	if (count == LP_CHAR(minor) ||
	    (!use_polling && !LP_CAREFUL_READY(minor, status)))
		return 0;
	w_dtr(minor, lpchar);
	stats = &LP_STAT(minor);
	stats->chars++;
	/* must wait before taking strobe high, and after taking strobe
	   low, according spec.  Some printers need it, others don't. */
	while (wait != LP_WAIT(minor)) /* FIXME: should be a udelay() */
		wait++;
	/* control port takes strobe high */
	w_ctr(minor, LP_PSELECP | LP_PINITP | LP_PSTROBE);
	while (wait)			/* FIXME: should be a udelay() */
		wait--;
	/* take strobe low */
	w_ctr(minor, LP_PSELECP | LP_PINITP);
	/* update waittime statistics */
	if (count > stats->maxwait) {
#ifdef LP_DEBUG
		printk(KERN_DEBUG "lp%d success after %d counts.\n", minor, count);
#endif
		stats->maxwait = count;
	}
	count *= 256;
	wait = (count > stats->meanwait) ? count - stats->meanwait :
	    stats->meanwait - count;
	stats->meanwait = (255 * stats->meanwait + count + 128) / 256;
	stats->mdev = ((127 * stats->mdev) + wait + 64) / 128;

	return 1;
}

static void lp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct lp_struct *lp_dev = (struct lp_struct *) dev_id;

	if (waitqueue_active (&lp_dev->lp_wait_q))
		wake_up(&lp_dev->lp_wait_q);
}

static void lp_error(int minor)
{
	if (must_use_polling(minor)) {
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + LP_TIMEOUT_POLLED;
		lp_schedule (minor);
	}
}

static inline int lp_write_buf(unsigned int minor, const char *buf, int count)
{
	unsigned long copy_size;
	unsigned long total_bytes_written = 0;
	unsigned long bytes_written;
	struct lp_struct *lp = &lp_table[minor];
	unsigned char status;

	if (minor >= LP_NO)
		return -ENXIO;
	if (lp_table[minor].dev == NULL)
		return -ENXIO;

	do {
		bytes_written = 0;
		copy_size = (count <= LP_BUFFER_SIZE ? count : LP_BUFFER_SIZE);
		copy_from_user(lp->lp_buffer, buf, copy_size);

		while (copy_size) {
			if (lp_char(lp->lp_buffer[bytes_written], minor, must_use_polling(minor))) {
				--copy_size;
				++bytes_written;
				lp_table[minor].runchars++;
			} else {
				int rc = total_bytes_written + bytes_written;
				if (lp_table[minor].runchars > LP_STAT(minor).maxrun)
					LP_STAT(minor).maxrun = lp_table[minor].runchars;
				status = r_str(minor);
				if ((status & LP_POUTPA)) {
					printk(KERN_INFO "lp%d out of paper\n", minor);
					if (LP_F(minor) & LP_ABORT)
						return rc ? rc : -ENOSPC;
					lp_error(minor);
				} else if (!(status & LP_PSELECD)) {
					printk(KERN_INFO "lp%d off-line\n", minor);
					if (LP_F(minor) & LP_ABORT)
						return rc ? rc : -EIO;
					lp_error(minor);
				} else if (!(status & LP_PERRORP)) {
					printk(KERN_ERR "lp%d printer error\n", minor);
					if (LP_F(minor) & LP_ABORT)
						return rc ? rc : -EIO;
					lp_error(minor);
				}

				LP_STAT(minor).sleeps++;

				if (must_use_polling(minor)) {
#ifdef LP_DEBUG
					printk(KERN_DEBUG "lp%d sleeping at %d characters for %d jiffies\n", minor, lp_table[minor].runchars, LP_TIME(minor));
#endif
					lp_table[minor].runchars = 0;
					current->state = TASK_INTERRUPTIBLE;
					current->timeout = jiffies + LP_TIME(minor);
					lp_schedule (minor);
				} else {
					cli();
					enable_irq(lp->dev->port->irq);
					w_ctr(minor, LP_PSELECP|LP_PINITP|LP_PINTEN);
					status = r_str(minor);
					if ((!(status & LP_PACK) || (status & LP_PBUSY))
					    && LP_CAREFUL_READY(minor, status)) {
						w_ctr(minor, LP_PSELECP | LP_PINITP);
						sti();
						continue;
					}
					lp_table[minor].runchars = 0;
					current->timeout = jiffies + LP_TIMEOUT_INTERRUPT;
					interruptible_sleep_on(&lp->lp_wait_q);

					w_ctr(minor, LP_PSELECP | LP_PINITP);
					sti();
				}

				if (signal_pending(current)) {
					if (total_bytes_written + bytes_written)
						return total_bytes_written + bytes_written;
					else
						return -EINTR;
				}
			}
		}

		total_bytes_written += bytes_written;
		buf += bytes_written;
		count -= bytes_written;

	} while (count > 0);

	return total_bytes_written;
}

static ssize_t lp_write(struct file * file, const char * buf,
		        size_t count, loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	ssize_t retv;

	if (jiffies-lp_table[minor].lastcall > LP_TIME(minor))
		lp_table[minor].runchars = 0;

	lp_table[minor].lastcall = jiffies;

 	/* Claim Parport or sleep until it becomes available
 	 * (see lp_wakeup() for details)
 	 */
 	lp_parport_claim (minor);

	retv = lp_write_buf(minor, buf, count);
 
 	lp_parport_release (minor);
 	return retv;
}

static long long lp_lseek(struct file * file, long long offset, int origin)
{
	return -ESPIPE;
}

#ifdef CONFIG_PRINTER_READBACK

static int lp_read_nibble(int minor) 
{
	unsigned char i;
	i=r_str(minor)>>3;
	i&=~8;
	if ( ( i & 0x10) == 0) i|=8;
	return(i & 0x0f);
}

static void lp_select_in_high(int minor) {
	w_ctr(minor, (r_ctr(minor) | 8));
}

/* Status readback confirming to ieee1284 */
static ssize_t lp_read(struct file * file, char * buf,
		       size_t count, loff_t *ppos)
{
	unsigned char z=0, Byte=0, status;
	char *temp;
	ssize_t retval;
	unsigned int counter=0;
	unsigned int i;
	unsigned int minor=MINOR(file->f_dentry->d_inode->i_rdev);
	
 	/* Claim Parport or sleep until it becomes available
 	 * (see lp_wakeup() for details)
 	 */
 	lp_parport_claim (minor);

	temp=buf;	
#ifdef LP_READ_DEBUG 
	printk(KERN_INFO "lp%d: read mode\n", minor);
#endif

	retval = verify_area(VERIFY_WRITE, buf, count);
	if (retval)
		return retval;
	if (parport_ieee1284_nibble_mode_ok(lp_table[minor].dev->port, 0)==0) {
#ifdef LP_READ_DEBUG
		printk(KERN_INFO "lp%d: rejected IEEE1284 negotiation.\n",
		       minor);
#endif
		lp_select_in_high(minor);
		parport_release(lp_table[minor].dev);
		return temp-buf;          /*  End of file */
	}
	for (i=0; i<=(count*2); i++) {
		w_ctr(minor, r_ctr(minor) | 2); /* AutoFeed high */
		do {
			status=(r_str(minor) & 0x40);
			udelay(50);
			counter++;
			if (resched_needed())
				schedule ();
		} while ( (status == 0x40) && (counter < 20) );
		if ( counter == 20 ) { /* Timeout */
#ifdef LP_READ_DEBUG
			printk(KERN_DEBUG "lp_read: (Autofeed high) timeout\n");
#endif		
			w_ctr(minor, r_ctr(minor) & ~2);
			lp_select_in_high(minor);
			parport_release(lp_table[minor].dev);
			return temp-buf; /* end the read at timeout */
		}
		counter=0;
		z=lp_read_nibble(minor);
		w_ctr(minor, r_ctr(minor) & ~2); /* AutoFeed low */
		do {
			status=(r_str(minor) & 0x40);
			udelay(20);
			counter++;
			if (resched_needed())
				schedule ();
		} while ( (status == 0) && (counter < 20) );
		if (counter == 20) { /* Timeout */
#ifdef LP_READ_DEBUG
			printk(KERN_DEBUG "lp_read: (Autofeed low) timeout\n");
#endif
			if (signal_pending(current)) {
				lp_select_in_high(minor);
				parport_release(lp_table[minor].dev);
				if (temp !=buf)
					return temp-buf;
				else 
					return -EINTR;
			}
			current->state=TASK_INTERRUPTIBLE;
			current->timeout=jiffies + LP_TIME(minor);
			schedule ();
		}
		counter=0;
		if (( i & 1) != 0) {
			Byte= (Byte | z<<4);
			put_user(Byte, temp);
			temp++;
		} else Byte=z;
	}
	lp_select_in_high(minor);
	parport_release(lp_table[minor].dev);
	return temp-buf;	
}

#endif

static int lp_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	if (minor >= LP_NO)
		return -ENXIO;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENXIO;
	if (LP_F(minor) & LP_BUSY)
		return -EBUSY;

	MOD_INC_USE_COUNT;

	/* If ABORTOPEN is set and the printer is offline or out of paper,
	   we may still want to open it to perform ioctl()s.  Therefore we
	   have commandeered O_NONBLOCK, even though it is being used in
	   a non-standard manner.  This is strictly a Linux hack, and
	   should most likely only ever be used by the tunelp application. */
	if ((LP_F(minor) & LP_ABORTOPEN) && !(file->f_flags & O_NONBLOCK)) {
		int status = r_str(minor);
		if (status & LP_POUTPA) {
			printk(KERN_INFO "lp%d out of paper\n", minor);
			MOD_DEC_USE_COUNT;
			return -ENOSPC;
		} else if (!(status & LP_PSELECD)) {
			printk(KERN_INFO "lp%d off-line\n", minor);
			MOD_DEC_USE_COUNT;
			return -EIO;
		} else if (!(status & LP_PERRORP)) {
			printk(KERN_ERR "lp%d printer error\n", minor);
			MOD_DEC_USE_COUNT;
			return -EIO;
		}
	}
	lp_table[minor].lp_buffer = (char *) kmalloc(LP_BUFFER_SIZE, GFP_KERNEL);
	if (!lp_table[minor].lp_buffer) {
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	LP_F(minor) |= LP_BUSY;
	return 0;
}

static int lp_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	kfree_s(lp_table[minor].lp_buffer, LP_BUFFER_SIZE);
	lp_table[minor].lp_buffer = NULL;
	LP_F(minor) &= ~LP_BUSY;
	MOD_DEC_USE_COUNT;
	return 0;
}


static int lp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int retval = 0;

#ifdef LP_DEBUG
	printk(KERN_DEBUG "lp%d ioctl, cmd: 0x%x, arg: 0x%x\n", minor, cmd, arg);
#endif
	if (minor >= LP_NO)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;
	switch ( cmd ) {
		case LPTIME:
			LP_TIME(minor) = arg * HZ/100;
			break;
		case LPCHAR:
			LP_CHAR(minor) = arg;
			break;
		case LPABORT:
			if (arg)
				LP_F(minor) |= LP_ABORT;
			else
				LP_F(minor) &= ~LP_ABORT;
			break;
		case LPABORTOPEN:
			if (arg)
				LP_F(minor) |= LP_ABORTOPEN;
			else
				LP_F(minor) &= ~LP_ABORTOPEN;
			break;
		case LPCAREFUL:
			if (arg)
				LP_F(minor) |= LP_CAREFUL;
			else
				LP_F(minor) &= ~LP_CAREFUL;
			break;
		case LPWAIT:
			LP_WAIT(minor) = arg;
			break;
		case LPSETIRQ: 
			return -EINVAL;
			break;
		case LPGETIRQ:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
			    sizeof(int));
		    	if (retval)
		    		return retval;
			copy_to_user((int *) arg, &LP_IRQ(minor), sizeof(int));
			break;
		case LPGETSTATUS:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
			    sizeof(int));
		    	if (retval)
		    		return retval;
			else {
				int status = r_str(minor);
				copy_to_user((int *) arg, &status, sizeof(int));
			}
			break;
		case LPRESET:
			lp_reset(minor);
			break;
		case LPGETSTATS:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
			    sizeof(struct lp_stats));
		    	if (retval)
		    		return retval;
			else {
				copy_to_user((int *) arg, &LP_STAT(minor), sizeof(struct lp_stats));
				if (suser())
					memset(&LP_STAT(minor), 0, sizeof(struct lp_stats));
			}
			break;
 		case LPGETFLAGS:
 			retval = verify_area(VERIFY_WRITE, (void *) arg,
 			    sizeof(int));
 		    	if (retval)
 		    		return retval;
 			else {
 				int status = LP_F(minor);
				copy_to_user((int *) arg, &status, sizeof(int));
			}
			break;
		default:
			retval = -EINVAL;
	}
	return retval;
}


static struct file_operations lp_fops = {
	lp_lseek,
#ifdef CONFIG_PRINTER_READBACK
	lp_read,
#else
	NULL,
#endif
	lp_write,
	NULL,		/* lp_readdir */
	NULL,		/* lp_poll */
	lp_ioctl,
	NULL,		/* lp_mmap */
	lp_open,
	lp_release
};

static int parport[LP_NO] = { UNSPEC, };

#ifdef MODULE
#define lp_init init_module
MODULE_PARM(parport, "1-" __MODULE_STRING(LP_NO) "i");

#else

static int parport_ptr = 0;

void lp_setup(char *str, int *ints)
{
	/* Ugh. */
	if (!strncmp(str, "parport", 7)) {
		int n = simple_strtoul(str+7, NULL, 10);
		if (parport_ptr < LP_NO)
			parport[parport_ptr++] = n;
		else
			printk(KERN_INFO "lp: too many ports, %s ignored.\n",
			       str);
	} else if (!strcmp(str, "auto")) {
		parport[0] = AUTO;
	} else {
		if (ints[0] == 0 || ints[1] == 0) {
			/* disable driver on "lp=" or "lp=0" */
			parport[0] = OFF;
		} else {
			printk(KERN_WARNING "warning: 'lp=0x%x' is deprecated, ignored\n", ints[1]);
		}
	}
}

#endif

void lp_wakeup(void *ref)
{
	struct lp_struct *lp_dev = (struct lp_struct *) ref;

	if (!waitqueue_active (&lp_dev->lp_wait_q))
		return;	/* Wake up whom? */

	/* Claim the Parport */
	if (parport_claim(lp_dev->dev))
		return;	/* Shouldn't happen */

	wake_up(&lp_dev->lp_wait_q);
}

static int inline lp_searchfor(int list[], int a)
{
	int i;
	for (i = 0; i < LP_NO && list[i] != UNSPEC; i++) {
		if (list[i] == a) return 1;
	}
	return 0;
}

int lp_init(void)
{
	int count = 0;
	struct parport *pb;
  
	if (parport[0] == OFF) return 0;

	pb = parport_enumerate();

	while (pb) {
		/* We only understand PC-style ports. */
		if (pb->modes & PARPORT_MODE_PCSPP) {
			if (parport[0] == UNSPEC ||
			    lp_searchfor(parport, count) ||
			    (parport[0] == AUTO &&
			     pb->probe_info.class == PARPORT_CLASS_PRINTER)) {
				lp_table[count].dev =
				  parport_register_device(pb, dev_name, 
						lp_preempt, lp_wakeup,
						lp_interrupt, PARPORT_DEV_TRAN,
						(void *) &lp_table[count]);
				lp_table[count].flags |= LP_EXIST;
				init_waitqueue (&lp_table[count].lp_wait_q);
				lp_parport_claim (count);
				lp_reset (count);
				lp_parport_release (count);
				printk(KERN_INFO "lp%d: using %s (%s).\n", 
				       count, pb->name, (pb->irq == PARPORT_IRQ_NONE)?"polling":"interrupt-driven");
			}
			if (++count == LP_NO)
				break;
		}
		pb = pb->next;
  	}

  	/* Successful specified devices increase count
  	 * Unsuccessful specified devices increase failed
  	 */
  	if (count) {
		if (register_chrdev(LP_MAJOR, "lp", &lp_fops)) {
			printk("lp: unable to get major %d\n", LP_MAJOR);
			return -EIO;
		}
		return 0;
	}

	printk(KERN_INFO "lp: driver loaded but no devices found\n");
#ifdef MODULE
	return 0;
#else	
	return 1;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
	int offset;

	unregister_chrdev(LP_MAJOR, "lp");
	for (offset = 0; offset < LP_NO; offset++) {
		if (lp_table[offset].dev == NULL)
			continue;
		parport_unregister_device(lp_table[offset].dev);
	}
}
#endif
