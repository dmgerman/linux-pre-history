/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = 1 << (irq_nr & 7);
	save_flags(flags);
	if (irq_nr < 8) {
		cli();
		cache_21 |= mask;
		outb(cache_21,0x21);
		restore_flags(flags);
		return;
	}
	cli();
	cache_A1 |= mask;
	outb(cache_A1,0xA1);
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	save_flags(flags);
	if (irq_nr < 8) {
		cli();
		cache_21 &= mask;
		outb(cache_21,0x21);
		restore_flags(flags);
		return;
	}
	cli();
	cache_A1 &= mask;
	outb(cache_A1,0xA1);
	restore_flags(flags);
}

/*
 * Initial irq handlers.
 */
struct irqaction {
	void (*handler)(int, struct pt_regs *);
	unsigned long flags;
	unsigned long mask;
	const char *name;
};

static struct irqaction irq_action[16] = {
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL }
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action = irq_action;

	for (i = 0 ; i < 16 ; i++, action++) {
		if (!action->handler)
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s\n",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
	}
	return len;
}

int request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	unsigned long irqflags, const char * devname)
{
	struct irqaction * action;
	unsigned long flags;

	if (irq > 15)
		return -EINVAL;
	action = irq + irq_action;
	if (action->handler)
		return -EBUSY;
	if (!handler)
		return -EINVAL;
	save_flags(flags);
	cli();
	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	if (irq < 8) {
		cache_21 &= ~(1<<irq);
		outb(cache_21,0x21);
	} else {
		cache_21 &= ~(1<<2);
		cache_A1 &= ~(1<<(irq-8));
		outb(cache_21,0x21);
		outb(cache_A1,0xA1);
	}
	restore_flags(flags);
	return 0;
}

void free_irq(unsigned int irq)
{
	halt();
}

static void handle_nmi(struct pt_regs * regs)
{
	printk("Whee.. NMI received. Probable hardware error\n");
	printk("61=%02x, 461=%02x\n", inb(0x61), inb(0x461));
}

static void unexpected_irq(int irq, struct pt_regs * regs)
{
	int i;

	printk("IO device interrupt, irq = %d\n", irq);
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
	printk("Expecting: ");
	for (i = 0; i < 16; i++)
		if (irq_action[i].handler)
			printk("[%s:%d] ", irq_action[i].name, i);
	printk("\n");
	printk("64=%02x, 60=%02x, 3fa=%02x 2fa=%02x\n",
		inb(0x64), inb(0x60), inb(0x3fa), inb(0x2fa));
	outb(0x0c, 0x3fc);
	outb(0x0c, 0x2fc);
	outb(0,0x61);
	outb(0,0x461);
}

static inline void handle_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq + irq_action;

	kstat.interrupts[irq]++;
	if (!action->handler) {
		unexpected_irq(irq, regs);
		return;
	}
	action->handler(irq, regs);
}

static void local_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	switch (vector) {
		/* com1: map to irq 4 */
		case 0x900:
			handle_irq(4, regs);
			return;

		/* com2: map to irq 3 */
		case 0x920:
			handle_irq(3, regs);
			return;

		/* keyboard: map to irq 1 */
		case 0x980:
			handle_irq(1, regs);
			return;

		/* mouse: map to irq 12 */
		case 0x990:
			handle_irq(12, regs);
			return;
		default:
			printk("Unknown local interrupt %lx\n", vector);
	}
}

/*
 * The vector is 0x8X0 for EISA interrupt X, and 0x9X0 for the local
 * motherboard interrupts.. This is for the Jensen.
 *
 *	0x660 - NMI
 *
 *	0x800 - IRQ0  interval timer (not used, as we use the RTC timer)
 *	0x810 - IRQ1  line printer (duh..)
 *	0x860 - IRQ6  floppy disk
 *	0x8E0 - IRQ14 SCSI controller
 *
 *	0x900 - COM1
 *	0x920 - COM2
 *	0x980 - keyboard
 *	0x990 - mouse
 *
 * The PCI version is more sane: it doesn't have the local interrupts at
 * all, and has only normal PCI interrupts from devices. Happily it's easy
 * enough to do a sane mapping from the Jensen.. Note that this means
 * that we may have to do a hardware "ack" to a different interrupt than
 * we report to the rest of the world..
 */
static void device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	if (vector == 0x660) {
		handle_nmi(regs);
		return;
	}

	ack = irq = (vector - 0x800) >> 4;
#ifndef CONFIG_PCI
	if (vector >= 0x900) {
		local_device_interrupt(vector, regs);
		return;
	}
	/* irq1 is supposed to be the keyboard, silly Jensen */
	if (irq == 1)
		irq = 7;
#endif
	printk("%d%d", irq, ack);
	if (!irq)
		printk(".");
	else
		handle_irq(irq, regs);

	/* ACK the interrupt making it the lowest priority */
	/*  First the slave .. */
	if (ack > 7) {
		outb(0xC0 | (ack - 8), 0xa0);
		ack = 2;
	}
	/* .. then the master */
	outb(0xC0 | ack, 0x20);
}

static void machine_check(unsigned long vector, unsigned long la_ptr, struct pt_regs * regs)
{
	printk("Machine check\n");
}

asmlinkage void do_entInt(unsigned long type, unsigned long vector,
	unsigned long la_ptr, struct pt_regs *regs)
{
	switch (type) {
		case 0:
			printk("Interprocessor interrupt? You must be kidding\n");
			break;
		case 1:
			/* timer interrupt.. */
			handle_irq(0, regs);
			return;
		case 2:
			machine_check(vector, la_ptr, regs);
			break;
		case 3:
			device_interrupt(vector, regs);
			return;
		case 4:
			printk("Performance counter interrupt\n");
			break;;
		default:
			printk("Hardware intr %ld %lx? Huh?\n", type, vector);
	}
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
}

extern asmlinkage void entInt(void);

void init_IRQ(void)
{
	wrent(entInt, 0);
}
