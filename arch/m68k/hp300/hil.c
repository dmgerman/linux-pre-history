/*
 *  linux/arch/m68k/hp300/hil.c
 *
 *  Copyright (C) 1998 Philip Blundell <philb@gnu.org>
 *
 *  HP300 Human Interface Loop driver.  This handles the keyboard and mouse.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/keyboard.h>
#include <linux/kbd_ll.h>
#include <asm/io.h>
#include <asm/hwtest.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/system.h>

#define HILBASE			0xf0428000
#define HIL_DATA			0x1
#define HIL_CMD			0x3

#define	HIL_BUSY		0x02
#define	HIL_DATA_RDY		0x01

#define hil_busy()		(readb(HILBASE + HIL_CMD) & HIL_BUSY)
#define hil_data_available()	(readb(HILBASE + HIL_CMD) & HIL_DATA_RDY)
#define hil_status()		(readb(HILBASE + HIL_CMD))
#define hil_command(x)		do { writeb((x), HILBASE + HIL_CMD); } while (0)
#define hil_read_data()		(readb(HILBASE + HIL_DATA))
#define hil_write_data(x)	do { writeb((x), HILBASE + HIL_DATA); } while (0)

#define	HIL_SETARD		0xA0		/* set auto-repeat delay */
#define	HIL_SETARR		0xA2		/* set auto-repeat rate */
#define	HIL_SETTONE		0xA3		/* set tone generator */
#define	HIL_CNMT		0xB2		/* clear nmi */
#define	HIL_INTON		0x5C		/* Turn on interrupts. */
#define	HIL_INTOFF		0x5D		/* Turn off interrupts. */
#define	HIL_TRIGGER		0xC5		/* trigger command */
#define	HIL_STARTCMD		0xE0		/* start loop command */
#define	HIL_TIMEOUT		0xFE		/* timeout */
#define	HIL_READTIME		0x13		/* Read real time register */

#define	HIL_READBUSY		0x02		/* internal "busy" register */
#define	HIL_READKBDLANG		0x12		/* read keyboard language code */
#define	HIL_READKBDSADR	 	0xF9
#define	HIL_WRITEKBDSADR 	0xE9
#define	HIL_READLPSTAT  	0xFA
#define	HIL_WRITELPSTAT 	0xEA
#define	HIL_READLPCTRL  	0xFB
#define	HIL_WRITELPCTRL 	0xEB

#define HIL_IRQ			1

static u_short hp_plain_map[NR_KEYS] __initdata = {
	0xf200, 0xf01b, 0xf20e, 0xf700, 0xf700, 0xf700, 0xf702, 0xf036,
	0xf037, 0xf038, 0xf039, 0xf030, 0xf02d, 0xf03d, 0xf008, 0xf009,
	0xfb71, 0xfb77, 0xfb65, 0xfb72, 0xfb74, 0xfb79, 0xfb75, 0xfb69,
	0xfb62, 0xfb76, 0xf063, 0xfb78, 0xfb7a, 0xf702, 0xfb61, 0xfb73,
	0xfb64, 0xfb66, 0xfb67, 0xfb68, 0xfb6a, 0xfb6b, 0xfb6c, 0xf03b,
	0xfb68, 0xfb67, 0xfb66, 0xfb64, 0xfb73, 0xfb61, 0xfb63, 0xfb76,
	0xfb75, 0xfb79, 0xfb74, 0xfb72, 0xfb65, 0xfb77, 0xfb71, 0xf200,
	0xf037, 0xf036, 0xf035, 0xf034, 0xf033, 0xf032, 0xf031, 0xf104,
	0xf105, 0xf106, 0xf107, 0xf108, 0xf109, 0xf200, 0xf200, 0xf114,
	0xf603, 0xf200, 0xf30b, 0xf601, 0xf200, 0xf602, 0xf30a, 0xf200,
	0xf600, 0xf200, 0xf115, 0xf07f, 0xf200, 0xf200, 0xf200, 0xf200,
	0xf038, 0xf039, 0xf030, 0xf200, 0xf200, 0xf008, 0xf200, 0xf200,
	0xfb69, 0xfb6f, 0xfb70, 0xf312, 0xf313, 0xf30d, 0xf30c, 0xf307,
	0xfb6a, 0xfb6b, 0xfb6c, 0xf305, 0xf306, 0xf00d, 0xf302, 0xf303,
	0xfb6d, 0xf02c, 0xf02e, 0xf02f, 0xf200, 0xf200, 0xf200, 0xf200,
	0xfb6e, 0xf020, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200
};

static u_short hp_shift_map[NR_KEYS] __initdata = {
	0xf200, 0xf01b, 0xf20e, 0xf700, 0xf700, 0xf700, 0xf002, 0xf036,
	0xf037, 0xf038, 0xf039, 0xf030, 0xf02d, 0xf03d, 0xf008, 0xf009,
	0xfb71, 0xfb77, 0xfb65, 0xfb72, 0xfb74, 0xfb79, 0xfb75, 0xfb69,
	0xfb62, 0xfb76, 0xf063, 0xfb78, 0xfb7a, 0xf702, 0xfb61, 0xfb73,
	0xfb64, 0xfb66, 0xfb67, 0xfb68, 0xfb6a, 0xfb6b, 0xfb6c, 0xf03b,
	0xfb68, 0xfb67, 0xfb66, 0xfb64, 0xfb73, 0xfb61, 0xfb63, 0xfb76,
	0xfb75, 0xfb79, 0xfb74, 0xfb72, 0xfb65, 0xfb77, 0xfb71, 0xf200,
	0xf037, 0xf036, 0xf035, 0xf034, 0xf033, 0xf032, 0xf031, 0xf104,
	0xf105, 0xf106, 0xf107, 0xf108, 0xf109, 0xf200, 0xf200, 0xf114,
	0xf603, 0xf200, 0xf30b, 0xf601, 0xf200, 0xf602, 0xf30a, 0xf200,
	0xf600, 0xf200, 0xf115, 0xf07f, 0xf200, 0xf200, 0xf200, 0xf200,
	0xf038, 0xf039, 0xf030, 0xf200, 0xf200, 0xf008, 0xf200, 0xf200,
	0xfb69, 0xfb6f, 0xfb70, 0xf312, 0xf313, 0xf30d, 0xf30c, 0xf307,
	0xfb6a, 0xfb6b, 0xfb6c, 0xf305, 0xf306, 0xf00d, 0xf302, 0xf303,
	0xfb6d, 0xf02c, 0xf02e, 0xf02f, 0xf200, 0xf200, 0xf200, 0xf200,
	0xfb6e, 0xf020, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200
};

static u_short hp_ctrl_map[NR_KEYS] __initdata = {
	0xf200, 0xf200, 0xf200, 0xf000, 0xf01b, 0xf01c, 0xf01d, 0xf01e,
	0xf01f, 0xf07f, 0xf200, 0xf200, 0xf01f, 0xf200, 0xf008, 0xf200,
	0xf011, 0xf017, 0xf005, 0xf012, 0xf014, 0xf019, 0xf015, 0xf009,
	0xf00f, 0xf010, 0xf003, 0xf01d, 0xf201, 0xf702, 0xf001, 0xf013,
	0xf004, 0xf006, 0xf007, 0xf008, 0xf00a, 0xf00b, 0xf00c, 0xf200,
	0xf007, 0xf000, 0xf700, 0xf01c, 0xf01a, 0xf018, 0xf003, 0xf016,
	0xf002, 0xf00e, 0xf00d, 0xf200, 0xf200, 0xf07f, 0xf700, 0xf200,
	0xf703, 0xf000, 0xf207, 0xf100, 0xf101, 0xf102, 0xf103, 0xf104,
	0xf105, 0xf106, 0xf107, 0xf108, 0xf109, 0xf200, 0xf200, 0xf114,
	0xf603, 0xf200, 0xf30b, 0xf601, 0xf200, 0xf602, 0xf30a, 0xf200,
	0xf600, 0xf200, 0xf115, 0xf07f, 0xf200, 0xf200, 0xf200, 0xf200,
	0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200,
	0xf200, 0xf1ff, 0xf202, 0xf312, 0xf313, 0xf30d, 0xf30c, 0xf307,
	0xf308, 0xf309, 0xf304, 0xf305, 0xf306, 0xf301, 0xf302, 0xf303,
	0xf300, 0xf310, 0xf30e, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200,
	0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200
};

struct {
  unsigned char s, c;
  int valid;
} hil_last;

#define hil_getlast(s,c)  do { s = hil_last.s; c = hil_last.c; hil_last.valid = 0; } while (0)

struct {
  unsigned char data[16];
  unsigned int ptr;
} poll;

unsigned char curdev = 0;

static void poll_finished(void)
{
  switch (poll.data[0])
  {
  case 0x40:
    {
      unsigned char scode = (poll.data[1] >> 1) | ((poll.data[1] & 1)?0x80:0);
#if 0
      if (scode & 0x80)
	printk("[%02x]", scode & 0x7f);
#endif
      handle_scancode(scode);
    }
    break;
  }
  curdev = 0;
}

static inline void handle_status(unsigned char s, unsigned char c)
{
  if (c & 0x8) {
    /* End of block */
    if (c & 0x10)
      poll_finished();
  } else {
    if (c & 0x10) {
      if (curdev)
	poll_finished();		/* just in case */
      curdev = c & 7;
      poll.ptr = 0;
    }
  }
}

static inline void handle_data(unsigned char s, unsigned char c)
{
  if (curdev)
    poll.data[poll.ptr++] = c;
}

/* 
 * Handle HIL interrupts.
 */

static void hil_interrupt(int irq, void *handle, struct pt_regs *regs)
{
  unsigned char s, c;
  s = hil_status(); c = hil_read_data();
  switch (s >> 4)
  {
  case 0x5:
    handle_status(s, c);
    break;
  case 0x6:
    handle_data(s, c);
    break;
  case 0x4:
    hil_last.s = s;
    hil_last.c = c;
    mb();
    hil_last.valid = 1;
    break;
  }
}

/*
 * Send a command to the HIL
 */

static void hil_do(unsigned char cmd, unsigned char *data, unsigned int len)
{
  unsigned long flags;
  save_flags(flags); cli();
  while (hil_busy());
  hil_command(cmd);
  while (len--) {
    while (hil_busy());
    hil_write_data(*(data++));
  }
  restore_flags(flags);
}

/*
 * Initialise HIL. 
 */

__initfunc(void hp300_hil_init(void))
{
  unsigned char s, c, kbid;
  unsigned int n = 0;

  memcpy(key_maps[0], hp_plain_map, sizeof(plain_map));
  memcpy(key_maps[1], hp_shift_map, sizeof(plain_map));
  memcpy(key_maps[4], hp_ctrl_map, sizeof(plain_map));

  if (!hwreg_present((void *)(HILBASE + HIL_DATA)))
    return;		/* maybe this can happen */

  request_irq(HIL_IRQ, hil_interrupt, 0, "HIL", NULL);

  /* Turn on interrupts */
  hil_do(HIL_INTON, NULL, 0);

  /* Look for keyboards */
  hil_do(HIL_READKBDSADR, NULL, 0);
  while (!hil_last.valid) {
    if (n++ > 1000) {
      printk("HIL: timed out, assuming no keyboard present.\n");
      return;
    }
    mb();
  }
  hil_getlast(s, c);
  if (c == 0) {
    printk("HIL: no keyboard present.\n");
    return;
  }
  for (kbid = 0; (kbid < 8) && ((c & (1<<kbid)) == 0); kbid++);
  printk("HIL: keyboard found at id %d\n", kbid);
  /* set it to raw mode */
  c = 0;
  hil_do(HIL_WRITEKBDSADR, &c, 1);
}
