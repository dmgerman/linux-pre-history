/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Holds initial configuration information for devices.
 *
 * NOTE:	This file is a nice idea, but its current format does not work
 *		well for drivers that support multiple units, like the SLIP
 *		driver.  We should actually have only one pointer to a driver
 *		here, with the driver knowing how many units it supports.
 *		Currently, the SLIP driver abuses the "base_addr" integer
 *		field of the 'device' structure to store the unit number...
 *		-FvK
 *
 * Version:	@(#)Space.c	1.0.7	08/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald J. Becker, <becker@super.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/ddi.h>
#include "dev.h"

#define LOOPBACK			/* always present, right?	*/

#define	NEXT_DEV	NULL


/* A unifed ethernet device probe.  This is the easiest way to have every
   ethernet adaptor have the name "eth[0123...]".
   */

extern int wd_probe(struct device *dev);
extern int el2_probe(struct device *dev);
extern int ne_probe(struct device *dev);
extern int hp_probe(struct device *dev);
extern int znet_probe(struct device *);
extern int express_probe(struct device *);
extern int el3_probe(struct device *);
extern int atp_probe(struct device *);
extern int at1500_probe(struct device *);
extern int depca_probe(struct device *);
extern int el1_probe(struct device *);

static int
ethif_probe(struct device *dev)
{
    short base_addr = dev->base_addr;

    if (base_addr < 0  ||  base_addr == 1)
	return 1;		/* ENXIO */

    if (1
#if defined(CONFIG_WD80x3) || defined(WD80x3)
	&& wd_probe(dev)
#endif
#if defined(CONFIG_EL2) || defined(EL2)
	&& el2_probe(dev)
#endif
#if defined(CONFIG_NE2000) || defined(NE2000)
	&& ne_probe(dev)
#endif
#if defined(CONFIG_HPLAN) || defined(HPLAN)
	&& hp_probe(dev)
#endif
#ifdef CONFIG_AT1500
	&& at1500_probe(dev)
#endif
#ifdef CONFIG_EL3
	&& el3_probe(dev)
#endif
#ifdef CONFIG_ZNET
	&& znet_probe(dev)
#endif
#ifdef CONFIG_EEXPRESS
	&& express_probe(dev)
#endif
#ifdef CONFIG_ATP		/* AT-LAN-TEC (RealTek) pocket adaptor. */
	&& atp_probe(dev)
#endif
#ifdef CONFIG_DEPCA
	&& depca_probe(dev)
#endif
#ifdef CONFIG_EL1
	&& el1_probe(dev)
#endif
	&& 1 ) {
	return 1;	/* -ENODEV or -EAGAIN would be more accurate. */
    }
    return 0;
}


/* This remains seperate because it requires the addr and IRQ to be
   set. */
#if defined(D_LINK) || defined(CONFIG_DE600)
    extern int d_link_init(struct device *);
    static struct device d_link_dev = {
	"dl0",
	0,
	0,
	0,
	0,
	D_LINK_IO,
	D_LINK_IRQ,
	0, 0, 0,
	NEXT_DEV,
	d_link_init
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&d_link_dev)
#endif

/* The first device defaults to I/O base '0', which means autoprobe. */
#ifdef EI8390
# define ETH0_ADDR EI8390
#else
# define ETH0_ADDR 0
#endif
#ifdef EI8390_IRQ
# define ETH0_IRQ EI8390_IRQ
#else
# define ETH0_IRQ 0
#endif
/* "eth0" defaults to autoprobe, other use a base of "-0x20", "don't probe".
   Enable these with boot-time setup. 0.99pl13+ can optionally autoprobe. */

static struct device eth3_dev = {
    "eth3", 0,0,0,0,0xffe0 /* I/O base*/, 0,0,0,0, NEXT_DEV, ethif_probe };
static struct device eth2_dev = {
    "eth2", 0,0,0,0,0xffe0 /* I/O base*/, 0,0,0,0, &eth3_dev, ethif_probe };
static struct device eth1_dev = {
    "eth1", 0,0,0,0,0xffe0 /* I/O base*/, 0,0,0,0, &eth2_dev, ethif_probe };

static struct device eth0_dev = {
    "eth0", 0, 0, 0, 0, ETH0_ADDR, ETH0_IRQ, 0, 0, 0, &eth1_dev, ethif_probe };

#   undef NEXT_DEV
#   define NEXT_DEV	(&eth0_dev)

#if defined(PLIP) || defined(CONFIG_PLIP)
    extern int plip_init(struct device *);
    static struct device plip2_dev = {
	"plip2", 0, 0, 0, 0, 0x278, 2, 0, 0, 0, NEXT_DEV, plip_init, };
    static struct device plip1_dev = {
	"plip1", 0, 0, 0, 0, 0x378, 7, 0, 0, 0, &plip2_dev, plip_init, };
    static struct device plip0_dev = {
	"plip0", 0, 0, 0, 0, 0x3BC, 5, 0, 0, 0, &plip1_dev, plip_init, };
#   undef NEXT_DEV
#   define NEXT_DEV	(&plip0_dev)
#endif  /* PLIP */

#if defined(SLIP) || defined(CONFIG_SLIP)
    extern int slip_init(struct device *);
    static struct device slip3_dev = {
	"sl3",			/* Internal SLIP driver, channel 3	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x3,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	NEXT_DEV,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
    static struct device slip2_dev = {
	"sl2",			/* Internal SLIP driver, channel 2	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x2,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	&slip3_dev,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
    static struct device slip1_dev = {
	"sl1",			/* Internal SLIP driver, channel 1	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x1,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	&slip2_dev,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
    static struct device slip0_dev = {
	"sl0",			/* Internal SLIP driver, channel 0	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x0,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	&slip1_dev,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
#   undef	NEXT_DEV
#   define	NEXT_DEV	(&slip0_dev)
#endif	/* SLIP */


#ifdef LOOPBACK
    extern int loopback_init(struct device *dev);
    static struct device loopback_dev = {
	"lo",			/* Software Loopback interface		*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	NEXT_DEV,		/* next device				*/
	loopback_init		/* loopback_init should set up the rest	*/
    };
#   undef	NEXT_DEV
#   define	NEXT_DEV	(&loopback_dev)
#endif


struct device *dev_base = NEXT_DEV;
