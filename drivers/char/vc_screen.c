/*
 * linux/drivers/char/vc_screen.c
 *
 * Provide access to virtual console memory.
 * /dev/vcs0: the screen as it is being viewed right now (possibly scrolled)
 * /dev/vcsN: the screen of /dev/ttyN (1 <= N <= 63)
 *            [minor: N]
 *
 * /dev/vcsaN: idem, but including attributes, and prefixed with
 *	the 4 bytes lines,columns,x,y (as screendump used to give)
 *            [minor: N+128]
 *
 * This replaces screendump and part of selection, so that the system
 * administrator can control access using file system permissions.
 *
 * aeb@cwi.nl - efter Friedas begravelse - 950211
 *
 * machek@k332.feld.cvut.cz - modified not to send characters to wrong console
 *	 - fixed some fatal off-by-one bugs (0-- no longer == -1 -> looping and looping and looping...)
 *	 - making it shorter - scr_readw are macros which expand in PRETTY long code
 */

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <asm/uaccess.h>

#undef attr
#undef org
#undef addr
#define HEADER_SIZE	4

static unsigned short
func_scr_readw(unsigned short *org)
{
return scr_readw( org );
}

static void
func_scr_writew(unsigned short val, unsigned short *org)
{
scr_writew( val, org );
}

static int
vcs_size(struct inode *inode)
{
	int size;
#ifdef CONFIG_FB_CONSOLE
	int cons = MINOR(inode->i_rdev) & 127;

	if (cons == 0)
		cons = fg_console;
	else
		cons--;
	if (!vc_cons_allocated(cons))
		return -ENXIO;
#endif

	size = get_video_num_lines(cons) * get_video_num_columns(cons);

	if (MINOR(inode->i_rdev) & 128)
		size = 2*size + HEADER_SIZE;
	return size;
}

static long long vcs_lseek(struct file *file, long long offset, int orig)
{
	int size = vcs_size(file->f_dentry->d_inode);

	switch (orig) {
		default:
			return -EINVAL;
		case 2:
			offset += size;
			break;
		case 1:
			offset += file->f_pos;
		case 0:
			break;
	}
	if (offset < 0 || offset > size)
		return -EINVAL;
	file->f_pos = offset;
	return file->f_pos;
}

#define RETURN( x ) { enable_bh( CONSOLE_BH ); return x; }
static ssize_t
vcs_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	unsigned int currcons = MINOR(inode->i_rdev);
	long p = *ppos;
	long viewed, attr, size, read;
	char *buf0;
	unsigned short *org = NULL;

	attr = (currcons & 128);
	currcons = (currcons & 127);
	disable_bh( CONSOLE_BH );
	if (currcons == 0) {
		currcons = fg_console;
		viewed = 1;
	} else {
		currcons--;
		viewed = 0;
	}
	if (!vc_cons_allocated(currcons))
		RETURN( -ENXIO );

	size = vcs_size(inode);
	if (p < 0 || p > size)
		RETURN( -EINVAL );
	if (count > size - p)
		count = size - p;

	buf0 = buf;
	if (!attr) {
		org = screen_pos(currcons, p, viewed);
		while (count-- > 0)
			put_user(func_scr_readw(org++) & 0xff, buf++);
	} else {
		if (p < HEADER_SIZE) {
			char header[HEADER_SIZE];
			header[0] = (char) get_video_num_lines(currcons);
			header[1] = (char) get_video_num_columns(currcons);
			getconsxy(currcons, header+2);
			while (p < HEADER_SIZE && count > 0)
			    { count--; put_user(header[p++], buf++); }
		}
		if (count > 0) {
		    p -= HEADER_SIZE;
		    org = screen_pos(currcons, p/2, viewed);
		    if ((p & 1) && count > 0)
			    { count--; put_user(func_scr_readw(org++) >> 8, buf++); }
		}
		while (count > 1) {
			put_user(func_scr_readw(org++), (unsigned short *) buf);
			buf += 2;
			count -= 2;
		}
		if (count > 0)
			put_user(func_scr_readw(org) & 0xff, buf++);
	}
	read = buf - buf0;
	*ppos += read;
	RETURN( read );
}

static ssize_t
vcs_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	unsigned int currcons = MINOR(inode->i_rdev);
	long p = *ppos;
	long viewed, attr, size, written;
	const char *buf0;
	unsigned short *org = NULL;

	attr = (currcons & 128);
	currcons = (currcons & 127);
	disable_bh( CONSOLE_BH );
	if (currcons == 0) {
		currcons = fg_console;
		viewed = 1;
	} else {
		currcons--;
		viewed = 0;
	}
	if (!vc_cons_allocated(currcons))
		RETURN( -ENXIO );

	size = vcs_size(inode);
	if (p < 0 || p > size)
		RETURN( -EINVAL );
	if (count > size - p)
		count = size - p;

	buf0 = buf;
	if (!attr) {
		org = screen_pos(currcons, p, viewed);
		while (count > 0) {
			unsigned char c;
			count--;
			get_user(c, (const unsigned char*)buf++);
			func_scr_writew((func_scr_readw(org) & 0xff00) | c, org);
			org++;
		}
	} else {
		if (p < HEADER_SIZE) {
			char header[HEADER_SIZE];
			getconsxy(currcons, header+2);
			while (p < HEADER_SIZE && count > 0)
				{ count--; get_user(header[p++], buf++); }
			if (!viewed)
				putconsxy(currcons, header+2);
		}
		if (count > 0) {
		    p -= HEADER_SIZE;
			org = screen_pos(currcons, p/2, viewed);
			if ((p & 1) && count > 0) {
			    char c;
				count--;
				get_user(c,buf++);
				func_scr_writew((c << 8) |
				     (func_scr_readw(org) & 0xff), org);
				org++;
			}
		}
		while (count > 1) {
			unsigned short w;
			get_user(w, (const unsigned short *) buf);
			func_scr_writew(w, org++);
			buf += 2;
			count -= 2;
		}
		if (count > 0) {
			unsigned char c;
			get_user(c, (const unsigned char*)buf++);
			func_scr_writew((func_scr_readw(org) & 0xff00) | c, org);
		}
	}
#ifdef CONFIG_FB_CONSOLE
	if (currcons == fg_console)
		/* Horribly inefficient if count < screen size.  */
		update_screen(currcons);
#endif
	written = buf - buf0;
	*ppos += written;
	RETURN( written );
}

static int
vcs_open(struct inode *inode, struct file *filp)
{
	unsigned int currcons = (MINOR(inode->i_rdev) & 127);
	if(currcons && !vc_cons_allocated(currcons-1))
		return -ENXIO;
	return 0;
}

static struct file_operations vcs_fops = {
	vcs_lseek,	/* lseek */
	vcs_read,	/* read */
	vcs_write,	/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	vcs_open,	/* open */
	NULL,		/* release */
	NULL		/* fsync */
};

__initfunc(int vcs_init(void))
{
	int error;

	error = register_chrdev(VCS_MAJOR, "vcs", &vcs_fops);
	if (error)
		printk("unable to get major %d for vcs device", VCS_MAJOR);
	return error;
}
