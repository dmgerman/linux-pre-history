/*
 *  drivers/video/imsttfb.c -- frame buffer device for IMS TwinTurbo
 *
 *  This file is derived from the powermac console "imstt" driver:
 *  Copyright (C) 1997 Sigurdur Asgeirsson
 *  With additional hacking by Jeffrey Kuskin (jsk@mojave.stanford.edu)
 *  Modified by Danilo Beuche 1998
 *  Some register values added by Damien Doligez, INRIA Rocquencourt
 *
 *  This file was written by Ryan Nielsen (ran@krazynet.com)
 *  Most of the frame buffer device stuff was copied from atyfb.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#if defined(CONFIG_PPC)
#include <linux/nvram.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <video/macmodes.h>
#endif

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#ifndef __powerpc__
#define eieio()		/* Enforce In-order Execution of I/O */
#endif

enum {
	IBM = 0x00,
	TVP = 0x01
};

/* TwinTurbo (Cosmo) registers */
enum {
	S1SA	=  0, /* 0x00 */
	S2SA	=  1, /* 0x04 */
	SP	=  2, /* 0x08 */
	DSA	=  3, /* 0x0C */
	CNT	=  4, /* 0x10 */
	DP_OCTRL=  5, /* 0x14 */
	BI	=  8, /* 0x20 */
	MBC	=  9, /* 0x24 */
	BLTCTL	= 10, /* 0x28 */

	/* Scan Timing Generator Registers */
	HES	= 12, /* 0x30 */
	HEB	= 13, /* 0x34 */
	HSB	= 14, /* 0x38 */
	HT	= 15, /* 0x3C */
	VES	= 16, /* 0x40 */
	VEB	= 17, /* 0x44 */
	VSB	= 18, /* 0x48 */
	VT	= 19, /* 0x4C */
	HCIV	= 20, /* 0x50 */
	VCIV	= 21, /* 0x54 */
	TCDR	= 22, /* 0x58 */
	VIL	= 23, /* 0x5C */
	STGCTL	= 24, /* 0x60 */

	/* Screen Refresh Generator Registers */
	SSR	= 25, /* 0x64 */
	HRIR	= 26, /* 0x68 */
	SPR	= 27, /* 0x6C */
	CMR	= 28, /* 0x70 */
	SRGCTL	= 29, /* 0x74 */

	/* RAM Refresh Generator Registers */
	RRCIV	= 30, /* 0x78 */
	RRSC	= 31, /* 0x7C */
	RRCR	= 34, /* 0x88 */

	/* System Registers */
	GIOE	= 32, /* 0x80 */
	GIO	= 33, /* 0x84 */
	SCR	= 35, /* 0x8C */
	SSTATUS	= 36, /* 0x90 */
	PRC	= 37, /* 0x94 */

#if 0	
	/* PCI Registers */
	DVID	= 0x00000000L,
	SC	= 0x00000004L,
	CCR	= 0x00000008L,
	OG	= 0x0000000CL,
	BARM	= 0x00000010L,
	BARER	= 0x00000030L,
#endif
};

/* IBM ramdac direct registers */
enum {
	PADDRW	= 0x00,
	PDATA	= 0x04,
	PPMASK	= 0x08,
	PADDRR	= 0x0c,
	PIDXLO	= 0x10,	
	PIDXHI	= 0x14,	
	PIDXDATA= 0x18,
	PIDXCTL	= 0x1c
};

/* IBM ramdac indirect registers */
enum {
	CLKCTL		= 0x02,	/* (0x01) Miscellaneous Clock Control */
	SYNCCTL		= 0x03,	/* (0x00) Sync Control */
	HSYNCPOS	= 0x04,	/* (0x00) Horizontal Sync Position */
	PWRMNGMT	= 0x05,	/* (0x00) Power Management  [multiples of 4 unblack screen] */
	DACOP		= 0x06,	/* (0x02) DAC Operation  [0-3 coloured, 4-7 green, odd light, even dark] */
	PALETCTL	= 0x07,	/* (0x00) Palette Control */
	SYSCLKCTL	= 0x08,	/* (0x01) System Clock Control */
	PIXFMT		= 0x0a,	/* () Pixel Format  [bpp >> 3 + 2] */
	BPP8		= 0x0b,	/* () 8 Bits/Pixel Control */
	BPP16		= 0x0c, /* () 16 Bits/Pixel Control  [15 or 1 for 555] */
	BPP24		= 0x0d,	/* () 24 Bits/Pixel Control */
	BPP32		= 0x0e,	/* () 32 Bits/Pixel Control */
	PIXCTL1		= 0x10, /* (0x05) Pixel PLL Control 1  [5-7 ok] */
	PIXCTL2		= 0x11,	/* (0x00) Pixel PLL Control 2  [multiples of 20 ok] */
	SYSCLKN		= 0x15,	/* () System Clock N (System PLL Reference Divider) */
	SYSCLKM		= 0x16,	/* () System Clock M (System PLL VCO Divider) */
	SYSCLKP		= 0x17,	/* () System Clock P */
	SYSCLKC		= 0x18,	/* () System Clock C */
	/*
	 * Dot clock rate is 20MHz * (m + 1) / ((n + 1) * (p ? 2 * p : 1)
	 * c is charge pump bias which depends on the VCO frequency  
	 */
	PIXCLKM		= 0x20,	/* () Pixel Clock M */
	PIXCLKN		= 0x21,	/* () Pixel Clock N */
	PIXCLKP		= 0x22,	/* () Pixel Clock P */
	PIXCLKC		= 0x23,	/* () Pixel Clock C */
	CURSCTL		= 0x30,	/* (0x00) Cursor Control  [1=curs1, 2=curs2, 5=curs3] */
	CURSXLO		= 0x31,	/* () Cursor X position, low 8 bits */
	CURSXHI		= 0x32,	/* () Cursor X position, high 8 bits */
	CURSYLO		= 0x33,	/* () Cursor Y position, low 8 bits */
	CURSYHI		= 0x34,	/* () Cursor Y position, high 8 bits */
	CURSHOTX	= 0x35,	/* () ???  [position relative to CURSX] */
	CURSHOTY	= 0x36,	/* () ???  [position relative to CURSY] */
	CURSACAEN	= 0x37,	/* () ???  [even=cursor on, odd=cursor off] */
	CURS1R		= 0x40,	/* () Cursor 1 Red */
	CURS1G		= 0x41,	/* () Cursor 1 Green */
	CURS1B		= 0x42,	/* () Cursor 1 Blue */
	CURS2R		= 0x43,	/* () Cursor 2 Red */
	CURS2G		= 0x44,	/* () Cursor 2 Green */
	CURS2B		= 0x45,	/* () Cursor 2 Blue */
	CURS3R		= 0x46,	/* () Cursor 3 Red */
	CURS3G		= 0x47,	/* () Cursor 3 Green */
	CURS3B		= 0x48,	/* () Cursor 3 Blue */
	BORDR		= 0x60,	/* () Border Color Red */
	BORDG		= 0x61,	/* () Border Color Green */
	BORDB		= 0x62,	/* () Border Color Blue */
	MISCTL1		= 0x70,	/* (0x00) Miscellaneous Control 1 */
	MISCTL2		= 0x71,	/* (0x00) Miscellaneous Control 2 */
	MISCTL3		= 0x72,	/* (0x00) Miscellaneous Control 3 */
	KEYCTL		= 0x78	/* (0x00) Key Control/DB Operation */
};

/* TI TVP 3030 RAMDAC Direct Registers */
enum {
	TVPADDRW = 0x00,	/* 0  Palette/Cursor RAM Write Adress/Index */
	TVPPDATA = 0x04,	/* 1  Palette Data RAM Data */
	TVPPMASK = 0x08,	/* 2  Pixel Read-Mask */
	TVPPADRR = 0x0c,	/* 3  Palette/Cursor RAM Read Adress */
	TVPCADRW = 0x10,	/* 4  Cursor/Overscan Color Write Address */
	TVPCDATA = 0x14,	/* 5  Cursor/Overscan Color Data */
				/* 6  reserved */
	TVPCADRR = 0x1c,	/* 7  Cursor/Overscan Color Read Address */
				/* 8  reserved */
	TVPDCCTL = 0x24,	/* 9  Direct Cursor Control */
	TVPIDATA = 0x28,	/* 10 Index Data */
	TVPCRDAT = 0x2c,	/* 11 Cursor RAM Data */
	TVPCXPOL = 0x30,	/* 12 Cursor-Position X LSB */
	TVPCXPOH = 0x34,	/* 13 Cursor-Position X MSB */
	TVPCYPOL = 0x38,	/* 14 Cursor-Position Y LSB */
	TVPCYPOH = 0x3c,	/* 15 Cursor-Position Y MSB */
};

/* TI TVP 3030 RAMDAC Indirect Registers */
enum {
	TVPIRREV = 0x01,	/* Silicon Revision [RO] */
	TVPIRICC = 0x06,	/* Indirect Cursor Control 	(0x00) */
	TVPIRBRC = 0x07,	/* Byte Router Control 	(0xe4) */
	TVPIRLAC = 0x0f,	/* Latch Control 		(0x06) */
	TVPIRTCC = 0x18,	/* True Color Control  	(0x80) */
	TVPIRMXC = 0x19,	/* Multiplex Control		(0x98) */
	TVPIRCLS = 0x1a,	/* Clock Selection		(0x07) */
	TVPIRPPG = 0x1c,	/* Palette Page		(0x00) */
	TVPIRGEC = 0x1d,	/* General Control 		(0x00) */
	TVPIRMIC = 0x1e,	/* Miscellaneous Control	(0x00) */
	TVPIRPLA = 0x2c,	/* PLL Address */
	TVPIRPPD = 0x2d,	/* Pixel Clock PLL Data */
	TVPIRMPD = 0x2e,	/* Memory Clock PLL Data */
	TVPIRLPD = 0x2f,	/* Loop Clock PLL Data */
	TVPIRCKL = 0x30,	/* Color-Key Overlay Low */
	TVPIRCKH = 0x31,	/* Color-Key Overlay High */
	TVPIRCRL = 0x32,	/* Color-Key Red Low */
	TVPIRCRH = 0x33,	/* Color-Key Red High */
	TVPIRCGL = 0x34,	/* Color-Key Green Low */
	TVPIRCGH = 0x35,	/* Color-Key Green High */
	TVPIRCBL = 0x36,	/* Color-Key Blue Low */
	TVPIRCBH = 0x37,	/* Color-Key Blue High */
	TVPIRCKC = 0x38,	/* Color-Key Control 		(0x00) */
	TVPIRMLC = 0x39,	/* MCLK/Loop Clock Control	(0x18) */
	TVPIRSEN = 0x3a,	/* Sense Test			(0x00) */
	TVPIRTMD = 0x3b,	/* Test Mode Data */
	TVPIRRML = 0x3c,	/* CRC Remainder LSB [RO] */
	TVPIRRMM = 0x3d,	/* CRC Remainder MSB [RO] */
	TVPIRRMS = 0x3e,	/* CRC  Bit Select [WO] */
	TVPIRDID = 0x3f,	/* Device ID [RO] 		(0x30) */
	TVPIRRES = 0xff		/* Software Reset [WO] */
};

struct initvalues {
	__u8 addr, value;
};

static struct initvalues ibm_initregs[] = {
	{ CLKCTL,	0x21 },
	{ SYNCCTL,	0x00 },
	{ HSYNCPOS,	0x00 },
	{ PWRMNGMT,	0x00 },
	{ DACOP,	0x02 },
	{ PALETCTL,	0x00 },
	{ SYSCLKCTL,	0x01 },

	/*
	 * Note that colors in X are correct only if all video data is
	 * passed through the palette in the DAC.  That is, "indirect
	 * color" must be configured.  This is the case for the IBM DAC
	 * used in the 2MB and 4MB cards, at least.
	 */
	{ BPP8,		0x00 },
	{ BPP16,	0x00 },
	{ BPP24,	0x00 },
	{ BPP32,	0x00 },

	{ PIXCTL1,	0x05 },
	{ PIXCTL2,	0x00 },
	{ SYSCLKN,	0x08 },
	{ SYSCLKM,	0x4f },
	{ SYSCLKP,	0x00 },
	{ SYSCLKC,	0x00 },
	{ CURSCTL,	0x00 },
	{ CURS2R,	0xff },
	{ CURS2G,	0xff },
	{ CURS2B,	0xff },
	{ BORDR,	0xff },
	{ BORDG,	0xff },
	{ BORDB,	0xff },
	{ MISCTL1,	0x01 },
	{ MISCTL2,	0x45 },
	{ MISCTL3,	0x00 },
	{ KEYCTL,	0x00 }
};

static struct initvalues tvp_initregs[] = {
	{ 0x6,	0x00 },
	{ 0x7,	0xe4 },
	{ 0xf,	0x06 },
	{ 0x18,	0x80 },
	{ 0x19,	0x4d },
	{ 0x1a,	0x05 },
	{ 0x1c,	0x00 },
	{ 0x1d,	0x00 },
	{ 0x1e,	0x08 },
	{ 0x30,	0xff },
	{ 0x31,	0xff },
	{ 0x32,	0xff },
	{ 0x33,	0xff },
	{ 0x34,	0xff },
	{ 0x35,	0xff },
	{ 0x36,	0xff },
	{ 0x37,	0xff },
	{ 0x38,	0x00 },
	{ TVPIRPLA,	0x00 },
	{ TVPIRPPD,	0xc0 },
	{ TVPIRPPD,	0xd5 },
	{ TVPIRPPD,	0xea },
	{ TVPIRPLA,	0x00 },
	{ TVPIRMPD,	0xb9 },
	{ TVPIRMPD,	0x3a },
	{ TVPIRMPD,	0xb1 },
	{ TVPIRPLA,	0x00 },
	{ TVPIRLPD,	0xc1 },
	{ TVPIRLPD,	0x3d },
	{ TVPIRLPD,	0xf3 },
};

struct imstt_regvals {
	__u32 pitch;
	__u16 hes, heb, hsb, ht, ves, veb, vsb, vt, vil;
	__u8 pclk_m, pclk_n, pclk_p;
	/* Values of the tvp which change depending on colormode x resolution */
	__u8 mlc[3];	/* Memory Loop Config 0x39 */
	__u8 lckl_p[3];	/* P value of LCKL PLL */
};

struct imstt_cursor {
	int enable;
	int on;
	int vbl_cnt;
	int blink_rate;
	__u16 x, y;
	struct timer_list timer;
};

struct fb_info_imstt {
	struct fb_info info;
	struct fb_fix_screeninfo fix;
	struct display disp;
	struct display_switch dispsw;
	union {
#ifdef FBCON_HAS_CFB16
		__u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
		__u32 cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
		__u32 cfb32[16];
#endif
	} fbcon_cmap;
	struct {
		__u8 red, green, blue;
	} palette[256];
	struct imstt_regvals init;
	struct imstt_cursor cursor;
	__u8 *frame_buffer_phys, *frame_buffer;
	__u32 *dc_regs_phys, *dc_regs;
	__u8 *cmap_regs_phys, *cmap_regs;
	__u32 total_vram;
	__u32 ramdac;
};

#define USE_NV_MODES		1
#define INIT_BPP		8
#define INIT_XRES		640
#define INIT_YRES		480
#define CURSOR_BLINK_RATE	20
#define CURSOR_DRAW_DELAY	2

static int currcon = 0;

static struct imstt_regvals tvp_reg_init_2 = {
	512,
	0x0002, 0x0006, 0x0026, 0x0028, 0x0003, 0x0016, 0x0196, 0x0197, 0x0196,
	0xec, 0x2a, 0xf3,
	{ 0x3c, 0x3b, 0x39 }, { 0xf3, 0xf3, 0xf3 }
};

static struct imstt_regvals tvp_reg_init_6 = {
	640,
	0x0004, 0x0009, 0x0031, 0x0036, 0x0003, 0x002a, 0x020a, 0x020d, 0x020a,
	0xef, 0x2e, 0xb2,
	{ 0x39, 0x39, 0x38 }, { 0xf3, 0xf3, 0xf3 }
};

static struct imstt_regvals tvp_reg_init_12 = {
	800,
	0x0005, 0x000e, 0x0040, 0x0042, 0x0003, 0x018, 0x270, 0x271, 0x270,
	0xf6, 0x2e, 0xf2,
	{ 0x3a, 0x39, 0x38 }, { 0xf3, 0xf3, 0xf3 }
};

static struct imstt_regvals tvp_reg_init_13 = {
	832,
	0x0004, 0x0011, 0x0045, 0x0048, 0x0003, 0x002a, 0x029a, 0x029b, 0x0000,
	0xfe, 0x3e, 0xf1,
	{ 0x39, 0x38, 0x38 }, { 0xf3, 0xf3, 0xf2 }
};

static struct imstt_regvals tvp_reg_init_17 = {
	1024,
	0x0006, 0x0210, 0x0250, 0x0053, 0x1003, 0x0021, 0x0321, 0x0324, 0x0000,
	0xfc, 0x3a, 0xf1,
	{ 0x39, 0x38, 0x38 }, { 0xf3, 0xf3, 0xf2 }
};

static struct imstt_regvals tvp_reg_init_18 = {
	1152,
  	0x0009, 0x0011, 0x059, 0x5b, 0x0003, 0x0031, 0x0397, 0x039a, 0x0000, 
	0xfd, 0x3a, 0xf1,
	{ 0x39, 0x38, 0x38 }, { 0xf3, 0xf3, 0xf2 }
};

static struct imstt_regvals tvp_reg_init_19 = {
	1280,
	0x0009, 0x0016, 0x0066, 0x0069, 0x0003, 0x0027, 0x03e7, 0x03e8, 0x03e7,
	0xf7, 0x36, 0xf0,
	{ 0x38, 0x38, 0x38 }, { 0xf3, 0xf2, 0xf1 }
};

static struct imstt_regvals tvp_reg_init_20 = {
	1280,
	0x0009, 0x0018, 0x0068, 0x006a, 0x0003, 0x0029, 0x0429, 0x042a, 0x0000,
	0xf0, 0x2d, 0xf0,
	{ 0x38, 0x38, 0x38 }, { 0xf3, 0xf2, 0xf1 }
};

static __u32
getclkMHz (struct fb_info_imstt *p)
{
	__u32 clk_m, clk_n, clk_p;

	clk_m = p->init.pclk_m;
	clk_n = p->init.pclk_n;
	clk_p = p->init.pclk_p;

	return 20 * (clk_m + 1) / ((clk_n + 1) * (clk_p ? 2 * clk_p : 1));
}

static void
setclkMHz (struct fb_info_imstt *p, __u32 MHz)
{
	__u32 clk_m, clk_n, clk_p, x, stage, spilled;

	clk_m = clk_n = clk_p = 0;
	stage = spilled = 0;
	for (;;) {
		switch (stage) {
			case 0:
				clk_m++;
				break;
			case 1:
				clk_n++;
				break;
		}
		x = 20 * (clk_m + 1) / ((clk_n + 1) * (clk_p ? 2 * clk_p : 1));
		if (x == MHz)
			break;
		if (x > MHz) {
			spilled = 1;
			stage = 1;
		} else if (spilled && x < MHz) {
			stage = 0;
		}
	}

	p->init.pclk_m = clk_m;
	p->init.pclk_n = clk_n;
	p->init.pclk_p = clk_p;
}

static struct imstt_regvals *
compute_imstt_regvals_ibm (struct fb_info_imstt *p, int xres, int yres)
{
	struct imstt_regvals *init = &p->init;
	__u32 MHz, hes, heb, veb, htp, vtp;

	switch (xres) {
		case 640:
			hes = 0x0008; heb = 0x0012; veb = 0x002a; htp = 10; vtp = 2;
			MHz = 30 /* .25 */ ;
			break;
		case 832:
			hes = 0x0005; heb = 0x0020; veb = 0x0028; htp = 8; vtp = 3;
			MHz = 57 /* .27_ */ ;
			break;
		case 1024:
			hes = 0x000a; heb = 0x001c; veb = 0x0020; htp = 8; vtp = 3;
			MHz = 80;
			break;
		case 1152:
			hes = 0x0012; heb = 0x0022; veb = 0x0031; htp = 4; vtp = 3;
			MHz = 101 /* .6_ */ ;
			break;
		case 1280:
			hes = 0x0012; heb = 0x002f; veb = 0x0029; htp = 4; vtp = 1;
			MHz = yres == 960 ? 126 : 135;
			break;
		case 1600:
			hes = 0x0018; heb = 0x0040; veb = 0x002a; htp = 4; vtp = 3;
			MHz = 200;
			break;
		default:
			return 0;
	}

	setclkMHz(p, MHz);

	init->hes = hes;
	init->heb = heb;
	init->hsb = init->heb + xres / 8;
	init->ht = init->hsb + htp;
	init->ves = 0x0003;
	init->veb = veb;
	init->vsb = init->veb + yres;
	init->vt = init->vsb + vtp;
	init->vil = init->vsb;

	init->pitch = xres;

	return init;
}

static struct imstt_regvals *
compute_imstt_regvals_tvp (struct fb_info_imstt *p, int xres, int yres)
{
	struct imstt_regvals *init;

	switch (xres) {
		case 512:
			init = &tvp_reg_init_2;
			break;
		case 640:
			init = &tvp_reg_init_6;
			break;
		case 800:
			init = &tvp_reg_init_12;
			break;
		case 832:
			init = &tvp_reg_init_13;
			break;
		case 1024:
			init = &tvp_reg_init_17;
			break;
		case 1152:
			init = &tvp_reg_init_18;
			break;
		case 1280:
			init = yres == 960 ? &tvp_reg_init_19 : &tvp_reg_init_20;
			break;
		default:
			return 0;
	}
	p->init = *init;

	return init;
}

static struct imstt_regvals *
compute_imstt_regvals (struct fb_info_imstt *p, u_int xres, u_int yres)
{
	if (p->ramdac == IBM)
		return compute_imstt_regvals_ibm(p, xres, yres);
	else
		return compute_imstt_regvals_tvp(p, xres, yres);
}

static void
set_imstt_regvals_ibm (struct fb_info_imstt *p, u_int bpp)
{
	struct imstt_regvals *init = &p->init;
	__u8 pformat = (bpp >> 3) + 2;

	p->cmap_regs[PIDXHI] = 0;		eieio();
	p->cmap_regs[PIDXLO] = PIXCLKM;		eieio();
	p->cmap_regs[PIDXDATA] = init->pclk_m;	eieio();
	p->cmap_regs[PIDXLO] = PIXCLKN;		eieio();
	p->cmap_regs[PIDXDATA] = init->pclk_n;	eieio();
	p->cmap_regs[PIDXLO] = PIXCLKP;		eieio();
	p->cmap_regs[PIDXDATA] = init->pclk_p;	eieio();
	p->cmap_regs[PIDXLO] = PIXCLKC;		eieio();
	p->cmap_regs[PIDXDATA] = 0x02;		eieio();

	p->cmap_regs[PIDXHI] = 0;		eieio();
	p->cmap_regs[PIDXLO] = PIXFMT;		eieio();
	p->cmap_regs[PIDXDATA] = pformat;	eieio();
}

static void
set_imstt_regvals_tvp (struct fb_info_imstt *p, u_int bpp)
{
	struct imstt_regvals *init = &p->init;
	__u8 tcc, mxc, lckl_n, mic;
	__u8 mlc, lckl_p;

	switch (bpp) {
		case 8:
			tcc = 0x80;
			mxc = 0x4d;
			lckl_n = 0xc1;
			mlc = init->mlc[0];
			lckl_p = init->lckl_p[0];
			break;
		case 16:
			tcc = 0x44;
			mxc = 0x55;
			lckl_n = 0xe1;
			mlc = init->mlc[1];
			lckl_p = init->lckl_p[1];
			break;
		case 24:
			/* ?!? */
		case 32:
			tcc = 0x46;
			mxc = 0x5d;
			lckl_n = 0xf1;
			mlc = init->mlc[2];
			lckl_p = init->lckl_p[2];
			break;
	}
	mic = 0x08;

	p->cmap_regs[TVPADDRW] = TVPIRPLA;	eieio();
	p->cmap_regs[TVPIDATA] = 0x00;		eieio();
	p->cmap_regs[TVPADDRW] = TVPIRPPD;	eieio();
	p->cmap_regs[TVPIDATA] = init->pclk_m;	eieio();
	p->cmap_regs[TVPADDRW] = TVPIRPPD;	eieio();
	p->cmap_regs[TVPIDATA] = init->pclk_n;	eieio();
	p->cmap_regs[TVPADDRW] = TVPIRPPD;	eieio();
	p->cmap_regs[TVPIDATA] = init->pclk_p;	eieio();

	p->cmap_regs[TVPADDRW] = TVPIRTCC;	eieio();
	p->cmap_regs[TVPIDATA] = tcc;		eieio();
	p->cmap_regs[TVPADDRW] = TVPIRMXC;	eieio();
	p->cmap_regs[TVPIDATA] = mxc;		eieio();
	p->cmap_regs[TVPADDRW] = TVPIRMIC;	eieio();
	p->cmap_regs[TVPIDATA] = mic;		eieio();

	p->cmap_regs[TVPADDRW] = TVPIRPLA;	eieio();
	p->cmap_regs[TVPIDATA] = 0x00;		eieio();
	p->cmap_regs[TVPADDRW] = TVPIRLPD;	eieio();
	p->cmap_regs[TVPIDATA] = lckl_n;	eieio();

	p->cmap_regs[TVPADDRW] = TVPIRPLA;	eieio();
	p->cmap_regs[TVPIDATA] = 0x15;		eieio();
	p->cmap_regs[TVPADDRW] = TVPIRMLC;	eieio();
	p->cmap_regs[TVPIDATA] = mlc;		eieio();

	p->cmap_regs[TVPADDRW] = TVPIRPLA;	eieio();
	p->cmap_regs[TVPIDATA] = 0x2a;		eieio();
	p->cmap_regs[TVPADDRW] = TVPIRLPD;	eieio();
	p->cmap_regs[TVPIDATA] = lckl_p;	eieio();
}

static void
set_imstt_regvals (struct fb_info_imstt *p, u_int bpp)
{
	struct imstt_regvals *init = &p->init;
	__u32 ctl, pitch, byteswap, scr;

	if (p->ramdac == IBM)
		set_imstt_regvals_ibm(p, bpp);
	else
		set_imstt_regvals_tvp(p, bpp);

  /*
   * From what I (jsk) can gather poking around with MacsBug,
   * bits 8 and 9 in the SCR register control endianness
   * correction (byte swapping).  These bits must be set according
   * to the color depth as follows:
   *     Color depth    Bit 9   Bit 8
   *     ==========     =====   =====
   *        8bpp          0       0
   *       16bpp          0       1
   *       24bpp          1       0
   *       32bpp          1       1
   */
	switch (bpp) {
		case 8:
			ctl = 0x17b1;
			pitch = init->pitch / 4;
			byteswap = 0x0;
			break;
		case 16:
			ctl = 0x17b3;
			pitch = init->pitch / 2;
			byteswap = 0x1;
			break;
		case 24:
			ctl = 0x17b9;
			pitch = init->pitch - (p->init.pitch / 4);
			byteswap = 0x2;
			break;
		case 32:
			ctl = 0x17b5;
			pitch = init->pitch;
			byteswap = 0x3;
			break;
	}
	if (p->ramdac == TVP)
		ctl -= 0x30;

	out_le32(&p->dc_regs[HES], init->hes);
	out_le32(&p->dc_regs[HEB], init->heb);
	out_le32(&p->dc_regs[HSB], init->hsb);
	out_le32(&p->dc_regs[HT], init->ht);
	out_le32(&p->dc_regs[VES], init->ves);
	out_le32(&p->dc_regs[VEB], init->veb);
	out_le32(&p->dc_regs[VSB], init->vsb);
	out_le32(&p->dc_regs[VT], init->vt);
	out_le32(&p->dc_regs[VIL], init->vil);
	out_le32(&p->dc_regs[HCIV], 1);
	out_le32(&p->dc_regs[VCIV], 1);
	out_le32(&p->dc_regs[TCDR], 4);
	out_le32(&p->dc_regs[RRCIV], 1);
	out_le32(&p->dc_regs[RRSC], 0x980);
	out_le32(&p->dc_regs[RRCR], 0x11);

	out_le32(&p->dc_regs[SSR], 0);
	if (p->ramdac == IBM) {
		out_le32(&p->dc_regs[HRIR], 0x0100);
		out_le32(&p->dc_regs[CMR], 0x00ff);
		out_le32(&p->dc_regs[SRGCTL], 0x0073);
	} else {
		out_le32(&p->dc_regs[HRIR], 0x0200);
		out_le32(&p->dc_regs[CMR], 0x01ff);
		out_le32(&p->dc_regs[SRGCTL], 0x0003);
	}

	switch (p->total_vram) {
		case 0x00200000:
			scr = 0x059d | (byteswap << 8);
			break;
		case 0x00400000:
			pitch /= 2;
			scr = 0xd0dc | (byteswap << 8);
			break;
		case 0x00800000:
			pitch /= 2;
			scr = 0x150dd | (byteswap << 8);
			break;
	}

	out_le32(&p->dc_regs[SCR], scr);
	out_le32(&p->dc_regs[SPR], pitch);

	out_le32(&p->dc_regs[STGCTL], ctl);
}

static void
set_16 (struct fb_info_imstt *p, __u8 x)
{
	if (p->ramdac == IBM) {
		p->cmap_regs[PIDXHI] = 0;	eieio();
		p->cmap_regs[PIDXLO] = BPP16;	eieio();
		p->cmap_regs[PIDXDATA] = x;	eieio();
	} else {
		/* ?!? */
	}
}

#define set_555(_p)	set_16(_p, 15)
#define set_565(_p)	set_16(_p, 0)	/* 220, 224 is darker in X */

static void
imstt_set_cursor (struct fb_info_imstt *p, int on)
{
	struct imstt_cursor *c = &p->cursor;

	p->cmap_regs[PIDXHI] = 0;
	if (!on) {
		p->cmap_regs[PIDXLO] = CURSCTL;
		p->cmap_regs[PIDXDATA] = 0x00;
	} else {
		p->cmap_regs[PIDXLO] = CURSXHI;
		p->cmap_regs[PIDXDATA] = c->x >> 8;
		p->cmap_regs[PIDXLO] = CURSXLO;
		p->cmap_regs[PIDXDATA] = c->x & 0xff;
		p->cmap_regs[PIDXLO] = CURSYHI;
		p->cmap_regs[PIDXDATA] = c->y >> 8;
		p->cmap_regs[PIDXLO] = CURSYLO;
		p->cmap_regs[PIDXDATA] = c->y & 0xff;
		p->cmap_regs[PIDXLO] = CURSCTL;
		p->cmap_regs[PIDXDATA] = 0x02;
	}
}

static void
imsttfb_cursor (struct display *disp, int mode, int x, int y)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)disp->fb_info;
	struct imstt_cursor *c = &p->cursor;

	x *= fontwidth(disp);
	y *= fontheight(disp);

	if (c->x == x && c->y == y && (mode == CM_ERASE) == !c->enable)
		return;

	c->enable = 0;
	if (c->on)
		imstt_set_cursor(p, 0);
	c->x = x;
	c->y = y;

	switch (mode) {
		case CM_ERASE:
			c->on = 0;
			break;
		case CM_DRAW:
		case CM_MOVE:
			if (c->on)
				imstt_set_cursor(p, c->on);
			else
				c->vbl_cnt = CURSOR_DRAW_DELAY;
			c->enable = 1;
			break;
	}
}

static int
imsttfb_set_font (struct display *disp, int width, int height)
{
	return 1;
}

static void
imstt_cursor_timer_handler (unsigned long dev_addr)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)dev_addr;
	struct imstt_cursor *c = &p->cursor;

	if (!c->enable)
		goto out;

	if (c->vbl_cnt && --c->vbl_cnt == 0) {
		c->on ^= 1;
		imstt_set_cursor(p, c->on);
		c->vbl_cnt = c->blink_rate;
	}

out:
	c->timer.expires = jiffies + (HZ / 50);
	add_timer(&c->timer);
}

__initfunc(static void
imstt_cursor_init (struct fb_info_imstt *p))
{
	struct imstt_cursor *c = &p->cursor;

	c->enable = 1;
	c->on = 1;
	c->blink_rate = CURSOR_BLINK_RATE;
	c->vbl_cnt = CURSOR_DRAW_DELAY;
	c->x = c->y = 0;

	init_timer(&c->timer);
	c->timer.expires = jiffies + (HZ / 50);
	c->timer.data = (unsigned long)p;
	c->timer.function = imstt_cursor_timer_handler;
	add_timer(&c->timer);
}

static void
imsttfb_rectcopy (struct display *disp, int sy, int sx, int dy, int dx, int height, int width)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)disp->fb_info;
	__u32 cnt_reg, stat, xxx = 0;
	__u32	line_pitch = disp->var.xres * (disp->var.bits_per_pixel >> 3),
		rect_height = height,
		rect_width = width * (disp->var.bits_per_pixel >> 3),
		fb_offset_old = sy * line_pitch + (sx * (disp->var.bits_per_pixel >> 3)),
		fb_offset_new = dy * line_pitch + (dx * (disp->var.bits_per_pixel >> 3));

	stat = in_le32(&p->dc_regs[SSTATUS]) & 0xff;

	cnt_reg = ((rect_height - 1) << 16) | (rect_width - 1);
	out_le32(&p->dc_regs[CNT], cnt_reg);
	out_le32(&p->dc_regs[S1SA], fb_offset_old);
	out_le32(&p->dc_regs[DSA], fb_offset_old);
	out_le32(&p->dc_regs[SP], line_pitch);
	out_le32(&p->dc_regs[DP_OCTRL], line_pitch);
	out_le32(&p->dc_regs[BLTCTL], 0x5);

	if (++stat == 0x20)
		stat = 0x10;
	while ((in_le32(&p->dc_regs[SSTATUS]) & 0xff) != stat && xxx++ < 0xff)
		eieio();

	cnt_reg = ((rect_height - 1) << 16) | (0xffff - (rect_width - 2));
	out_le32(&p->dc_regs[CNT], cnt_reg);
	out_le32(&p->dc_regs[DSA], fb_offset_new);
#if 0
	out_le32(&p->dc_regs[S1SA], fb_offset_old);
	out_le32(&p->dc_regs[SP], line_pitch);
	out_le32(&p->dc_regs[DP_OCTRL], line_pitch);
#endif
	out_le32(&p->dc_regs[BLTCTL], 0x85);

	if (++stat == 0x20)
		stat = 0x10;
	while ((in_le32(&p->dc_regs[SSTATUS]) & 0xff) != stat && xxx++ < 0xff)
		eieio();
}

static void
imsttfbcon_bmove (struct display *disp, int sy, int sx, int dy, int dx, int height, int width)
{
	sy *= fontheight(disp);
	sx *= fontwidth(disp);
	dy *= fontheight(disp);
	dx *= fontwidth(disp);
	height *= fontheight(disp);
	width *= fontwidth(disp);

	imsttfb_rectcopy(disp, sy, sx, dy, dx, height, width);
}

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_imstt8 = {
	fbcon_cfb8_setup, imsttfbcon_bmove, fbcon_cfb8_clear, fbcon_cfb8_putc,
	fbcon_cfb8_putcs, fbcon_cfb8_revc, NULL, NULL, fbcon_cfb8_clear_margins,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB16
static struct display_switch fbcon_imstt16 = {
	fbcon_cfb16_setup, imsttfbcon_bmove, fbcon_cfb16_clear, fbcon_cfb16_putc,
	fbcon_cfb16_putcs, fbcon_cfb16_revc, NULL, NULL, fbcon_cfb16_clear_margins,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB24
static struct display_switch fbcon_imstt24 = {
	fbcon_cfb24_setup, imsttfbcon_bmove, fbcon_cfb24_clear, fbcon_cfb24_putc,
	fbcon_cfb24_putcs, fbcon_cfb24_revc, NULL, NULL, fbcon_cfb24_clear_margins,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB32
static struct display_switch fbcon_imstt32 = {
	fbcon_cfb32_setup, imsttfbcon_bmove, fbcon_cfb32_clear, fbcon_cfb32_putc,
	fbcon_cfb32_putcs, fbcon_cfb32_revc, NULL, NULL, fbcon_cfb32_clear_margins,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>

extern struct vc_mode display_info;
extern struct fb_info *console_fb_info;

static void
set_display_info (struct display *disp)
{
	display_info.width = disp->var.xres;
	display_info.height = disp->var.yres;
	display_info.depth = disp->var.bits_per_pixel;
	display_info.pitch = disp->line_length;

	switch (disp->var.xres) {
		case 512:
			display_info.mode = 2;
			break;
		case 640:
			display_info.mode = 6;
			break;
		case 800:
			display_info.mode = 12;
			break;
		case 832:
			display_info.mode = 13;
			break;
		case 1024:
			display_info.mode = 17;
			break;
		case 1152:
			display_info.mode = 18;
			break;
		case 1280:
			display_info.mode = disp->var.yres == 960 ? 19 : 20;
			break;
		default:
			display_info.mode = 0;
	}
}
#endif

static int
imsttfb_getcolreg (u_int regno, u_int *red, u_int *green,
		   u_int *blue, u_int *transp, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;

	if (regno > 255)
		return 1;
	*red = (p->palette[regno].red << 8) | p->palette[regno].red;
	*green = (p->palette[regno].green << 8) | p->palette[regno].green;
	*blue = (p->palette[regno].blue << 8) | p->palette[regno].blue;
	*transp = 0;

	return 0;
}

static int
imsttfb_setcolreg (u_int regno, u_int red, u_int green, u_int blue,
		   u_int transp, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	u_int bpp = fb_display[currcon].var.bits_per_pixel;
	u_int i;

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	p->palette[regno].red = red;
	p->palette[regno].green = green;
	p->palette[regno].blue = blue;

	/* PADDRW/PDATA are the same as TVPPADDRW/TVPPDATA */
	if (p->ramdac == TVP && fb_display[currcon].var.green.length == 5 /* && bpp == 16 */) {
		p->cmap_regs[PADDRW] = regno << 3;	eieio();
	} else {
		p->cmap_regs[PADDRW] = regno;		eieio();
	}
	p->cmap_regs[PDATA] = red;	eieio();
	p->cmap_regs[PDATA] = green;	eieio();
	p->cmap_regs[PDATA] = blue;	eieio();

	if (regno < 16)
		switch (bpp) {
#ifdef FBCON_HAS_CFB16
			case 16:
				p->fbcon_cmap.cfb16[regno] = (regno << (fb_display[currcon].var.green.length == 5 ? 10 : 11)) | (regno << 5) | regno;
				break;
#endif
#ifdef FBCON_HAS_CFB24
			case 24:
				p->fbcon_cmap.cfb24[regno] = (regno << 16) | (regno << 8) | regno;
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case 32:
				i = (regno << 8) | regno;
				p->fbcon_cmap.cfb32[regno] = (i << 16) | i;
				break;
#endif
		}

	return 0;
}

static void
do_install_cmap (int con, struct fb_info *info)
{
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, imsttfb_setcolreg, info);
	else {
		u_int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_set_cmap(fb_default_cmap(size), 1, imsttfb_setcolreg, info);
	}
}

static int
imsttfb_open (struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
imsttfb_release (struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
imsttfb_get_fix (struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	struct fb_var_screeninfo *var = &fb_display[con].var;

	*fix = p->fix;
	fix->visual = var->bits_per_pixel == 8 ? FB_VISUAL_PSEUDOCOLOR
					       : FB_VISUAL_DIRECTCOLOR;
	fix->line_length = var->xres * (var->bits_per_pixel >> 3);

	return 0;
}

static int
imsttfb_get_var (struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	*var = fb_display[con].var;

	return 0;
}

static void
set_dispsw (struct display *disp, struct fb_info_imstt *p)
{
	u_int accel = disp->var.accel_flags & FB_ACCELF_TEXT;

	disp->visual = disp->var.bits_per_pixel == 8 ? FB_VISUAL_PSEUDOCOLOR
					 	     : FB_VISUAL_DIRECTCOLOR;
	p->dispsw = fbcon_dummy;
	disp->dispsw = &p->dispsw;
	disp->dispsw_data = 0;
	switch (disp->var.bits_per_pixel) {
		case 8:
			disp->var.red.offset = 0;
			disp->var.red.length = 8;
			disp->var.green.offset = 0;
			disp->var.green.length = 8;
			disp->var.blue.offset = 0;
			disp->var.blue.length = 8;
			disp->var.transp.offset = 0;
			disp->var.transp.length = 0;
#ifdef FBCON_HAS_CFB8
			p->dispsw = accel ? fbcon_imstt8 : fbcon_cfb8;
#endif
			break;
		case 16:	/* RGB 555 */
			if (disp->var.red.offset != 11)
				disp->var.red.offset = 10;
			disp->var.red.length = 5;
			disp->var.green.offset = 5;
			if (disp->var.green.length != 6)
				disp->var.green.length = 5;
			disp->var.blue.offset = 0;
			disp->var.blue.length = 5;
			disp->var.transp.offset = 0;
			disp->var.transp.length = 0;
#ifdef FBCON_HAS_CFB16
			p->dispsw = accel ? fbcon_imstt16 : fbcon_cfb16;
			disp->dispsw_data = p->fbcon_cmap.cfb16;
#endif
			break;
		case 24:	/* RGB 888 */
			disp->var.red.offset = 16;
			disp->var.red.length = 8;
			disp->var.green.offset = 8;
			disp->var.green.length = 8;
			disp->var.blue.offset = 0;
			disp->var.blue.length = 8;
			disp->var.transp.offset = 0;
			disp->var.transp.length = 0;
#ifdef FBCON_HAS_CFB24
			p->dispsw = accel ? fbcon_imstt24 : fbcon_cfb24;
			disp->dispsw_data = p->fbcon_cmap.cfb24;
#endif
			break;
		case 32:	/* RGBA 8888 */
			disp->var.red.offset = 16;
			disp->var.red.length = 8;
			disp->var.green.offset = 8;
			disp->var.green.length = 8;
			disp->var.blue.offset = 0;
			disp->var.blue.length = 8;
			disp->var.transp.offset = 24;
			disp->var.transp.length = 8;
#ifdef FBCON_HAS_CFB32
			p->dispsw = accel ? fbcon_imstt32 : fbcon_cfb32;
			disp->dispsw_data = p->fbcon_cmap.cfb32;
#endif
			break;
	}

	if (p->ramdac == IBM) {
		p->dispsw.cursor = imsttfb_cursor;
		p->dispsw.set_font = imsttfb_set_font;
	}
}

static int
imsttfb_set_var (struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	struct display *disp;
	u_int oldbpp, oldxres, oldyres, oldgreenlen, oldaccel;

	disp = &fb_display[con];

	if ((var->bits_per_pixel != 8 && var->bits_per_pixel != 16
	    && var->bits_per_pixel != 24 && var->bits_per_pixel != 32)
	    || var->xres_virtual < var->xres || var->yres_virtual < var->yres
	    || var->nonstd
	    || (var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;

	if ((var->xres * var->yres) * (var->bits_per_pixel >> 3) > p->total_vram
	    || (var->xres_virtual * var->yres_virtual) * (var->bits_per_pixel >> 3) > p->total_vram)
		return -EINVAL;

	if (!((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW))
		return 0;

	if (!compute_imstt_regvals(p, var->xres, var->yres))
		return -EINVAL;

	oldbpp = disp->var.bits_per_pixel;
	oldxres = disp->var.xres;
	oldyres = disp->var.yres;
	oldgreenlen = disp->var.green.length;
	oldaccel = disp->var.accel_flags;

	disp->var = *var;

	disp->var.activate = 0;
	disp->var.red.msb_right = 0;
	disp->var.green.msb_right = 0;
	disp->var.blue.msb_right = 0;
	disp->var.transp.msb_right = 0;
	disp->var.height = -1;
	disp->var.width = -1;
	disp->var.vmode = FB_VMODE_NONINTERLACED;
	disp->var.left_margin = disp->var.right_margin = 16;
	disp->var.upper_margin = disp->var.lower_margin = 16;
	disp->var.hsync_len = disp->var.vsync_len = 8;

	disp->screen_base = p->frame_buffer;
	disp->inverse = 0;
	disp->ypanstep = 1;
	disp->ywrapstep = 0;
	disp->scrollmode = SCROLL_YREDRAW;

	if (oldbpp != disp->var.bits_per_pixel || oldgreenlen != disp->var.green.length || oldaccel != disp->var.accel_flags)
		set_dispsw(disp, p);

	if (oldxres != disp->var.xres || oldbpp != disp->var.bits_per_pixel)
		disp->line_length = disp->var.xres * (disp->var.bits_per_pixel >> 3);

#ifdef CONFIG_FB_COMPAT_XPMAC
	set_display_info(disp);
#endif

	if (info->changevar)
		(*info->changevar)(con);

	if (con == currcon) {
		if (oldgreenlen != disp->var.green.length) {
			if (disp->var.green.length == 6)
				set_565(p);
			else
				set_555(p);
		}
		if (oldxres != disp->var.xres || oldyres != disp->var.yres || oldbpp != disp->var.bits_per_pixel)
			set_imstt_regvals(p, disp->var.bits_per_pixel);
			
	}
	disp->var.pixclock = 1000000 / getclkMHz(p);

	if (oldbpp != disp->var.bits_per_pixel) {
		int err = fb_alloc_cmap(&disp->cmap, 0, 0);
		if (err)
			return err;
		do_install_cmap(con, info);
	}

	return 0;
}

static int
imsttfb_pan_display (struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	u_int off;

	if (var->xoffset + fb_display[con].var.xres > fb_display[con].var.xres_virtual
	    || var->yoffset + fb_display[con].var.yres > fb_display[con].var.yres_virtual)
		return -EINVAL;

	fb_display[con].var.xoffset = var->xoffset;
	fb_display[con].var.yoffset = var->yoffset;
	if (con == currcon) {
		off = var->yoffset * (fb_display[con].line_length / 8)
		      + (var->xoffset * (fb_display[con].var.bits_per_pixel >> 3)) / 8;
		out_le32(&p->dc_regs[SSR], off);
	}

	return 0;
}

static int
imsttfb_get_cmap (struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	if (con == currcon)	/* current console? */
		return fb_get_cmap(cmap, kspc, imsttfb_getcolreg, info);
	else if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else {
		u_int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
	}

	return 0;
}

static int
imsttfb_set_cmap (struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		if ((err = fb_alloc_cmap(&fb_display[con].cmap, size, 0)))
			return err;
	}
	if (con == currcon)			/* current console? */
		return fb_set_cmap(cmap, kspc, imsttfb_setcolreg, info);
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);

	return 0;
}

#define FBIMSTT_SETREG		0x545401
#define FBIMSTT_GETREG		0x545402
#define FBIMSTT_SETCMAPREG	0x545403
#define FBIMSTT_GETCMAPREG	0x545404
#define FBIMSTT_SETINITREG	0x545405
#define FBIMSTT_GETINITREG	0x545406

static int
imsttfb_ioctl (struct inode *inode, struct file *file, u_int cmd,
	       u_long arg, int con, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	__u8 init[2];
	__u32 reg[2];

	switch (cmd) {
		case FBIMSTT_SETREG:
			if (copy_from_user(reg, (void *)arg, 8) || reg[0] > (0x40000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			out_le32(&p->dc_regs[reg[0]], reg[1]);
			break;
		case FBIMSTT_GETREG:
			if (copy_from_user(reg, (void *)arg, 4) || reg[0] > (0x40000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			reg[1] = in_le32(&p->dc_regs[reg[0]]);
			if (copy_to_user((void *)(arg + 4), &reg[1], 4))
				return -EFAULT;
			break;
		case FBIMSTT_SETCMAPREG:
			if (copy_from_user(reg, (void *)arg, 8) || reg[0] > (0x17c0000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			out_le32(&((u_int *)p->cmap_regs)[reg[0]], reg[1]);
			break;
		case FBIMSTT_GETCMAPREG:
			if (copy_from_user(reg, (void *)arg, 4) || reg[0] > (0x17c0000 - sizeof(reg[0])) / sizeof(reg[0]))
				return -EFAULT;
			reg[1] = in_le32(&((u_int *)p->cmap_regs)[reg[0]]);
			if (copy_to_user((void *)(arg + 4), &reg[1], 4))
				return -EFAULT;
			break;
		case FBIMSTT_SETINITREG:
			if (copy_from_user(init, (void *)arg, 2))
				return -EFAULT;
			p->cmap_regs[PIDXHI] = 0;		eieio();
			p->cmap_regs[PIDXLO] = init[0];		eieio();
			p->cmap_regs[PIDXDATA] = init[1];	eieio();
			break;
		case FBIMSTT_GETINITREG:
			if (copy_from_user(init, (void *)arg, 1))
				return -EFAULT;
			p->cmap_regs[PIDXHI] = 0;		eieio();
			p->cmap_regs[PIDXLO] = init[0];		eieio();
			init[1] = p->cmap_regs[PIDXDATA];
			if (copy_to_user((void *)(arg + 1), &init[1], 1))
				return -EFAULT;
			break;
		default:
			return -ENOIOCTLCMD;
	}

	return 0;
}

static struct fb_ops imsttfb_ops = {
	imsttfb_open,
	imsttfb_release,
	imsttfb_get_fix,
	imsttfb_get_var,
	imsttfb_set_var,
	imsttfb_get_cmap,
	imsttfb_set_cmap,
	imsttfb_pan_display,
	imsttfb_ioctl
};

static int
imsttfbcon_switch (int con, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	struct display *old = &fb_display[currcon], *new = &fb_display[con];

	if (old->cmap.len)
		fb_get_cmap(&old->cmap, 1, imsttfb_getcolreg, info);
	
	if (p->ramdac == IBM)
		imsttfb_cursor(old, CM_ERASE, p->cursor.x, p->cursor.y);

	currcon = con;

	if (old->var.xres != new->var.xres
	    || old->var.yres != new->var.yres
	    || old->var.bits_per_pixel != new->var.bits_per_pixel
	    || old->var.green.length != new->var.green.length) {
		set_dispsw(new, p);
		if (!compute_imstt_regvals(p, new->var.xres, new->var.yres))
			return -1;
		if (new->var.bits_per_pixel == 16) {
			if (new->var.green.length == 6)
				set_565(p);
			else
				set_555(p);
		}
		set_imstt_regvals(p, new->var.bits_per_pixel);
#ifdef CONFIG_FB_COMPAT_XPMAC
		set_display_info(new);
#endif
	}

	do_install_cmap(con, info);

	return 0;
}

static int
imsttfbcon_updatevar (int con, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	u_int off;

	if (con == currcon) {
		off = fb_display[con].var.yoffset * (fb_display[con].line_length / 8)
		      + (fb_display[con].var.xoffset * (fb_display[con].var.bits_per_pixel >> 3)) / 8;
		out_le32(&p->dc_regs[SSR], off);
	}

	return 0;
}

static void
imsttfbcon_blank (int blank, struct fb_info *info)
{
	struct fb_info_imstt *p = (struct fb_info_imstt *)info;
	__u32 ctrl;

	ctrl = in_le32(&p->dc_regs[STGCTL]);
	if (blank > 0) {
		switch (blank - 1) {
			case VESA_NO_BLANKING:
				ctrl &= ~0x00000380;
				break;
			case VESA_VSYNC_SUSPEND:
				ctrl &= ~0x00000020;
				break;
			case VESA_HSYNC_SUSPEND:
				ctrl &= ~0x00000010;
				break;
			case VESA_POWERDOWN:
				ctrl &= ~0x00000380;
				break;
		}
	} else {
		ctrl |= p->ramdac == IBM ? 0x000017b0 : 0x00001780;
	}
	out_le32(&p->dc_regs[STGCTL], ctrl);
}

__initfunc(static void
init_imstt(struct fb_info_imstt *p))
{
	__u32 i, tmp;

	tmp = in_le32(&p->dc_regs[PRC]);
	if (p->ramdac == IBM)
		p->total_vram = (tmp & 0x0004) ? 0x00400000 : 0x00200000;
	else
		p->total_vram = 0x00800000;

	memset(p->frame_buffer, 0, p->total_vram);

	/* initialize the card */
	tmp = in_le32(&p->dc_regs[STGCTL]);
	out_le32(&p->dc_regs[STGCTL], tmp & ~0x1);

	/* set default values for DAC registers */ 
	if (p->ramdac == IBM) {
		p->cmap_regs[PPMASK] = 0xff;
		p->cmap_regs[PIDXHI] = 0;	eieio();
		for (i = 0; i < sizeof(ibm_initregs) / sizeof(*ibm_initregs); i++) {
			p->cmap_regs[PIDXLO] = ibm_initregs[i].addr;	eieio();
			p->cmap_regs[PIDXDATA] = ibm_initregs[i].value;	eieio();
		}
	} else {
		for (i = 0; i < sizeof(tvp_initregs) / sizeof(*tvp_initregs); i++) {
			p->cmap_regs[TVPADDRW] = tvp_initregs[i].addr;	eieio();
			p->cmap_regs[TVPIDATA] = tvp_initregs[i].value;	eieio();
		}
	}

#if USE_NV_MODES && defined(CONFIG_PPC)
	{
		int vmode, cmode;

		vmode = nvram_read_byte(NV_VMODE);
		if (vmode <= 0 || vmode > VMODE_MAX)
			vmode = VMODE_640_480_67;
		cmode = nvram_read_byte(NV_CMODE);
		if (cmode < CMODE_8 || cmode > CMODE_32)
			cmode = CMODE_8;
		if (mac_vmode_to_var(vmode, cmode, &p->disp.var)) {
			p->disp.var.xres = p->disp.var.xres_virtual = INIT_XRES;
			p->disp.var.yres = p->disp.var.yres_virtual = INIT_YRES;
			p->disp.var.bits_per_pixel = INIT_BPP;
		}
	}
#else
	p->disp.var.xres = p->disp.var.xres_virtual = INIT_XRES;
	p->disp.var.yres = p->disp.var.yres_virtual = INIT_YRES;
	p->disp.var.bits_per_pixel = INIT_BPP;
#endif

	p->disp.var.height = p->disp.var.width = -1;
	p->disp.var.vmode = FB_VMODE_NONINTERLACED;
	p->disp.var.left_margin = p->disp.var.right_margin = 16;
	p->disp.var.upper_margin = p->disp.var.lower_margin = 16;
	p->disp.var.hsync_len = p->disp.var.vsync_len = 8;
	p->disp.var.accel_flags = 0; /* FB_ACCELF_TEXT; */

	set_dispsw(&p->disp, p);
	if (p->ramdac == IBM)
		imstt_cursor_init(p);

	if (p->disp.var.green.length == 6)
		set_565(p);
	else
		set_555(p);

	if ((p->disp.var.xres * p->disp.var.yres) * (p->disp.var.bits_per_pixel >> 3) > p->total_vram
	    || !(compute_imstt_regvals(p, p->disp.var.xres, p->disp.var.yres))) {
		printk("imsttfb: %ux%ux%u not supported\n", p->disp.var.xres, p->disp.var.yres, p->disp.var.bits_per_pixel);
		kfree(p);
		return;
	}

	set_imstt_regvals(p, p->disp.var.bits_per_pixel);

	p->disp.var.pixclock = 1000000 / getclkMHz(p);

	sprintf(p->fix.id, "IMS TT (%s)", p->ramdac == IBM ? "IBM" : "TVP");
	p->fix.smem_start = (__u8 *)p->frame_buffer_phys;
	p->fix.smem_len = p->total_vram;
	p->fix.mmio_start = (__u8 *)p->dc_regs_phys;
	p->fix.mmio_len = 0x40000;
	p->fix.accel = FB_ACCEL_IMS_TWINTURBO;
	p->fix.type = FB_TYPE_PACKED_PIXELS;
	p->fix.visual = p->disp.var.bits_per_pixel == 8 ? FB_VISUAL_PSEUDOCOLOR
							: FB_VISUAL_DIRECTCOLOR;
	p->fix.line_length = p->disp.var.xres * (p->disp.var.bits_per_pixel >> 3);
	p->fix.xpanstep = 8;
	p->fix.ypanstep = 1;
	p->fix.ywrapstep = 0;

	p->disp.screen_base = p->frame_buffer;
	p->disp.visual = p->fix.visual;
	p->disp.type = p->fix.type;
	p->disp.type_aux = p->fix.type_aux;
	p->disp.line_length = p->fix.line_length;
	p->disp.can_soft_blank = 1;
	p->disp.ypanstep = 1;
	p->disp.ywrapstep = 0;
	p->disp.scrollmode = SCROLL_YREDRAW;

	strcpy(p->info.modename, p->fix.id);
	p->info.node = -1;
	p->info.fbops = &imsttfb_ops;
	p->info.disp = &p->disp;
	p->info.fontname[0] = 0;
	p->info.changevar = 0;
	p->info.switch_con = &imsttfbcon_switch;
	p->info.updatevar = &imsttfbcon_updatevar;
	p->info.blank = &imsttfbcon_blank;
	p->info.flags = FBINFO_FLAG_DEFAULT;

	for (i = 0; i < 16; i++) {
		u_int j = color_table[i];
		p->palette[i].red = default_red[j];
		p->palette[i].green = default_grn[j];
		p->palette[i].blue = default_blu[j];
	}

	if (register_framebuffer(&p->info) < 0) {
		kfree(p);
		return;
	}

	tmp = (in_le32(&p->dc_regs[SSTATUS]) & 0x0f00) >> 8;
	printk("fb%d: %s frame buffer; %uMB vram; chip version %u\n",
		GET_FB_IDX(p->info.node), p->fix.id, p->total_vram >> 20, tmp);

#ifdef CONFIG_FB_COMPAT_XPMAC
	strncpy(display_info.name, p->fix.id, sizeof display_info.name);
	display_info.fb_address = (__u32)p->frame_buffer_phys;
	display_info.cmap_adr_address = (__u32)&p->cmap_regs_phys[PADDRW];
	display_info.cmap_data_address = (__u32)&p->cmap_regs_phys[PDATA];
	display_info.disp_reg_address = 0;
	set_display_info(&p->disp);
	if (!console_fb_info)
		console_fb_info = &p->info;
#endif /* CONFIG_FB_COMPAT_XPMAC */
}

#if defined(CONFIG_FB_OF) || defined(CONFIG_PPC)
__initfunc(void
imsttfb_of_init(struct device_node *dp))
{
	struct fb_info_imstt *p;
	int i;
	__u32 addr, size = 0;
	__u8 bus, devfn;
	__u16 cmd;

	for (i = 0; i < dp->n_addrs; i++) {
		if (dp->addrs[i].size >= 0x02000000) {
			addr = dp->addrs[i].address;
			size = dp->addrs[i].size;
		}
	}
	if (!size)
		return;

	p = kmalloc(sizeof(struct fb_info_imstt), GFP_ATOMIC);
	if (!p)
		return;

	memset(p, 0, sizeof(struct fb_info_imstt));
	p->frame_buffer_phys = (__u8 *)addr;
	p->frame_buffer = (__u8 *)ioremap(addr, size);
	p->dc_regs_phys = (__u32 *)(p->frame_buffer_phys + 0x00800000);
	p->dc_regs = (__u32 *)(p->frame_buffer + 0x00800000);
	p->cmap_regs_phys = (__u8 *)(p->frame_buffer_phys + 0x00840000);
	p->cmap_regs = (__u8 *)(p->frame_buffer + 0x00840000);

	if (dp->name[11] == '8')
		p->ramdac = TVP;
	else
		p->ramdac = IBM;

	if (!pci_device_loc(dp, &bus, &devfn)) {
		if (!pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd)) {
			cmd |= PCI_COMMAND_MEMORY;
			pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
		}
	}

	init_imstt(p);
}
#endif

__initfunc(void
imsttfb_init(void))
{
#if defined(CONFIG_PPC)
	u_int i;
	struct device_node *dp;
	char *names[4] = {"IMS,tt128mb","IMS,tt128mbA","IMS,tt128mb8","IMS,tt128mb8A"};

#ifdef CONFIG_FB_OF
	if (prom_num_displays)
		return;
#endif

	for (i = 0; i < 4; i++) {
		dp = find_devices(names[i]);
		if (dp)
			imsttfb_of_init(dp);
	}
#else
	/* ... */
#endif
}
