#ifndef __LINUX_UHCI_H
#define __LINUX_UHCI_H

#include <linux/list.h>

#include "usb.h"

/*
 * Universal Host Controller Interface data structures and defines
 */

/* Command register */
#define USBCMD		0
#define   USBCMD_RS		0x0001	/* Run/Stop */
#define   USBCMD_HCRESET	0x0002	/* Host reset */
#define   USBCMD_GRESET		0x0004	/* Global reset */
#define   USBCMD_EGSM		0x0008	/* Global Suspend Mode */
#define   USBCMD_FGR		0x0010	/* Force Global Resume */
#define   USBCMD_SWDBG		0x0020	/* SW Debug mode */
#define   USBCMD_CF		0x0040	/* Config Flag (sw only) */
#define   USBCMD_MAXP		0x0080	/* Max Packet (0 = 32, 1 = 64) */

/* Status register */
#define USBSTS		2
#define   USBSTS_USBINT		0x0001	/* Interrupt due to IOC */
#define   USBSTS_ERROR		0x0002	/* Interrupt due to error */
#define   USBSTS_RD		0x0004	/* Resume Detect */
#define   USBSTS_HSE		0x0008	/* Host System Error - basically PCI problems */
#define   USBSTS_HCPE		0x0010	/* Host Controller Process Error - the scripts were buggy */
#define   USBSTS_HCH		0x0020	/* HC Halted */

/* Interrupt enable register */
#define USBINTR		4
#define   USBINTR_TIMEOUT	0x0001	/* Timeout/CRC error enable */
#define   USBINTR_RESUME	0x0002	/* Resume interrupt enable */
#define   USBINTR_IOC		0x0004	/* Interrupt On Complete enable */
#define   USBINTR_SP		0x0008	/* Short packet interrupt enable */

#define USBFRNUM	6
#define USBFLBASEADD	8
#define USBSOF		12

/* USB port status and control registers */
#define USBPORTSC1	16
#define USBPORTSC2	18
#define   USBPORTSC_CCS		0x0001	/* Current Connect Status ("device present") */
#define   USBPORTSC_CSC		0x0002	/* Connect Status Change */
#define   USBPORTSC_PE		0x0004	/* Port Enable */
#define   USBPORTSC_PEC		0x0008	/* Port Enable Change */
#define   USBPORTSC_LS		0x0030	/* Line Status */
#define   USBPORTSC_RD		0x0040	/* Resume Detect */
#define   USBPORTSC_LSDA	0x0100	/* Low Speed Device Attached */
#define   USBPORTSC_PR		0x0200	/* Port Reset */
#define   USBPORTSC_SUSP	0x1000	/* Suspend */

#define UHCI_NULL_DATA_SIZE	0x7ff	/* for UHCI controller TD */

#define UHCI_PTR_BITS		0x000F
#define UHCI_PTR_TERM		0x0001
#define UHCI_PTR_QH		0x0002
#define UHCI_PTR_DEPTH		0x0004

struct uhci_qh {
	/* Hardware fields */
	__u32 link;				/* Next queue */
	__u32 element;				/* Queue element pointer */

	/* Software fields */
	atomic_t refcnt;			/* Reference counting */
	struct uhci_device *dev;		/* The owning device */
	struct uhci_qh *skel;			/* Skeleton head */

	wait_queue_head_t wakeup;
} __attribute__((aligned(16)));

struct uhci_framelist {
	__u32 frame[1024];
} __attribute__((aligned(4096)));

#define TD_CTRL_SPD		(1 << 29)	/* Short Packet Detect */
#define TD_CTRL_LS		(1 << 26)	/* Low Speed Device */
#define TD_CTRL_IOS		(1 << 25)	/* Isochronous Select */
#define TD_CTRL_IOC		(1 << 24)	/* Interrupt on Complete */
#define TD_CTRL_ACTIVE		(1 << 23)	/* TD Active */
#define TD_CTRL_STALLED		(1 << 22)	/* TD Stalled */
#define TD_CTRL_DBUFERR		(1 << 21)	/* Data Buffer Error */
#define TD_CTRL_BABBLE		(1 << 20)	/* Babble Detected */
#define TD_CTRL_NAK		(1 << 19)	/* NAK Received */
#define TD_CTRL_CRCTIME		(1 << 18)	/* CTC/Time Out Error */
#define TD_CTRL_BITSTUFF	(1 << 17)	/* Bit Stuff Error */

#define uhci_ptr_to_virt(x)	bus_to_virt(x & ~UHCI_PTR_BITS)

#define UHCI_TD_REMOVE		0x0001		/* Remove when done */

/*
 * The documentation says "4 words for hardware, 4 words for software".
 *
 * That's silly, the hardware doesn't care. The hardware only cares that
 * the hardware words are 16-byte aligned, and we can have any amount of
 * sw space after the TD entry as far as I can tell.
 *
 * But let's just go with the documentation, at least for 32-bit machines.
 * On 64-bit machines we probably want to take advantage of the fact that
 * hw doesn't really care about the size of the sw-only area.
 *
 * Alas, not anymore, we have more than 4 words for software, woops
 */
struct uhci_td {
	/* Hardware fields */
	__u32 link;
	__u32 status;
	__u32 info;
	__u32 buffer;

	/* Software fields */
	unsigned int *backptr;		/* Where to remove this from.. */
	struct list_head irq_list;	/* Active interrupt list.. */

	usb_device_irq completed;	/* Completion handler routine */
	void *dev_id;

	atomic_t refcnt;		/* Reference counting */
	struct uhci_device *dev;	/* The owning device */
	struct uhci_qh *qh;		/* QH this TD is a part of (ignored for Isochronous) */
	int flags;			/* Remove, etc */
} __attribute__((aligned(16)));

struct uhci_iso_td {
	int num;			/* Total number of TD's */
	char *data;			/* Beginning of buffer */
	int maxsze;			/* Maximum size of each data block */

	struct uhci_td *td;		/* Pointer to first TD */

	int frame;			/* Beginning frame */
	int endframe;			/* End frame */
};

/*
 * Note the alignment requirements of the entries
 *
 * Each UHCI device has pre-allocated QH and TD entries.
 * You can use more than the pre-allocated ones, but I
 * don't see you usually needing to.
 */
struct uhci;

#if 0
#define UHCI_MAXTD	64

#define UHCI_MAXQH	16
#endif

/* The usb device part must be first! */
struct uhci_device {
	struct usb_device	*usb;

	struct uhci		*uhci;
#if 0
	struct uhci_qh		qh[UHCI_MAXQH];		/* These are the "common" qh's for each device */
	struct uhci_td		td[UHCI_MAXTD];
#endif

	unsigned long		data[16];
};

#define uhci_to_usb(uhci)	((uhci)->usb)
#define usb_to_uhci(usb)	((struct uhci_device *)(usb)->hcpriv)

/*
 * There are various standard queues. We set up several different
 * queues for each of the three basic queue types: interrupt,
 * control, and bulk.
 *
 *  - There are various different interrupt latencies: ranging from
 *    every other USB frame (2 ms apart) to every 256 USB frames (ie
 *    256 ms apart). Make your choice according to how obnoxious you
 *    want to be on the wire, vs how critical latency is for you.
 *  - The control list is done every frame.
 *  - There are 4 bulk lists, so that up to four devices can have a
 *    bulk list of their own and when run concurrently all four lists
 *    will be be serviced.
 *
 * This is a bit misleading, there are various interrupt latencies, but they
 * vary a bit, interrupt2 isn't exactly 2ms, it can vary up to 4ms since the
 * other queues can "override" it. interrupt4 can vary up to 8ms, etc. Minor
 * problem
 *
 * In the case of the root hub, these QH's are just head's of qh's. Don't
 * be scared, it kinda makes sense. Look at this wonderful picture care of
 * Linus:
 *
 *  generic-iso-QH  ->  dev1-iso-QH  ->  generic-irq-QH  ->  dev1-irq-QH  -> ...
 *       |                  |                  |                   |
 *      End             dev1-iso-TD1          End            dev1-irq-TD1
 *                          |
 *                      dev1-iso-TD2
 *                          |
 *                        ....
 *
 * This may vary a bit (the UHCI docs don't explicitly say you can put iso
 * transfers in QH's and all of their pictures don't have that either) but
 * other than that, that is what we're doing now
 *
 * And now we don't put Iso transfers in QH's, so we don't waste one on it
 *
 * To keep with Linus' nomenclature, this is called the QH skeleton. These
 * labels (below) are only signficant to the root hub's QH's
 */
#define UHCI_NUM_SKELQH		10

#define skel_int2_qh		skelqh[0]
#define skel_int4_qh		skelqh[1]
#define skel_int8_qh		skelqh[2]
#define skel_int16_qh		skelqh[3]
#define skel_int32_qh		skelqh[4]
#define skel_int64_qh		skelqh[5]
#define skel_int128_qh		skelqh[6]
#define skel_int256_qh		skelqh[7]

#define skel_control_qh		skelqh[8]

#define skel_bulk_qh		skelqh[9]

/*
 * This describes the full uhci information.
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs.
 */
struct uhci {
	int irq;
	unsigned int io_addr;

	int control_pid;
	int control_running;
	int control_continue;

	struct list_head uhci_list;

	struct usb_bus *bus;

	struct uhci_qh skelqh[UHCI_NUM_SKELQH];	/* Skeleton QH's */

	struct uhci_framelist *fl;		/* Frame list */
	struct list_head interrupt_list;	/* List of interrupt-active TD's for this uhci */

	struct uhci_td *ticktd;
};

/* needed for the debugging code */
struct uhci_td *uhci_link_to_td(unsigned int element);

/* Debugging code */
void show_td(struct uhci_td * td);
void show_status(struct uhci *uhci);
void show_queues(struct uhci *uhci);

#endif

