/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org> 
 */

#define __KERNEL_SYSCALLS__

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/head.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/bugs.h>

#include <stdarg.h>


/*
 * Versions of gcc older than that listed below may actually compile
 * and link okay, but the end product can have subtle run time bugs.
 * To avoid associated bogus bug reports, we flatly refuse to compile
 * with a gcc that is known to be too old from the very beginning.
 */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 6)
#error sorry, your GCC is too old. It builds incorrect kernels.
#endif

extern char _stext, _etext;
extern char *linux_banner;

extern int console_loglevel;

static int init(void *);
extern int bdflush(void *);
extern int kswapd(void *);
extern void kswapd_setup(void);

extern void init_IRQ(void);
extern void init_modules(void);
extern long console_init(long, long);
extern void sock_init(void);
extern void uidcache_init(void);
extern unsigned long pci_init(unsigned long, unsigned long);
extern long mca_init(long, long);
extern long sbus_init(long, long);
extern long powermac_init(unsigned long, unsigned long);
extern void sysctl_init(void);
extern void filescache_init(void);
extern void signals_init(void);
extern void dquot_init(void);

extern void smp_setup(char *str, int *ints);
extern void ioapic_pirq_setup(char *str, int *ints);
extern void no_scroll(char *str, int *ints);
extern void panic_setup(char *str, int *ints);
extern void bmouse_setup(char *str, int *ints);
extern void msmouse_setup(char *str, int *ints);
extern void console_setup(char *str, int *ints);
#ifdef CONFIG_PRINTER
extern void lp_setup(char *str, int *ints);
#endif
#ifdef CONFIG_JOYSTICK
extern void js_setup(char *str, int *ints);
#endif
extern void eth_setup(char *str, int *ints);
#ifdef CONFIG_ARCNET_COM20020
extern void com20020_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCNET_RIM_I
extern void arcrimi_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCNET_COM90xxIO
extern void com90io_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCNET_COM90xx
extern void com90xx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DECNET
extern void decnet_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BLK_DEV_XD
extern void xd_setup(char *str, int *ints);
extern void xd_manual_geo_init(char *str, int *ints);
#endif
#ifdef CONFIG_BLK_DEV_IDE
extern void ide_setup(char *);
#endif
#ifdef CONFIG_PARIDE_PD
extern void pd_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PF
extern void pf_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PT
extern void pt_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PCD
extern void pcd_setup(char *str, int *ints);
#endif
extern void floppy_setup(char *str, int *ints);
extern void st_setup(char *str, int *ints);
extern void st0x_setup(char *str, int *ints);
extern void advansys_setup(char *str, int *ints);
extern void tmc8xx_setup(char *str, int *ints);
extern void t128_setup(char *str, int *ints);
extern void pas16_setup(char *str, int *ints);
extern void generic_NCR5380_setup(char *str, int *intr);
extern void generic_NCR53C400_setup(char *str, int *intr);
extern void aha152x_setup(char *str, int *ints);
extern void aha1542_setup(char *str, int *ints);
extern void gdth_setup(char *str, int *ints);
extern void aic7xxx_setup(char *str, int *ints);
extern void AM53C974_setup(char *str, int *ints);
extern void BusLogic_Setup(char *str, int *ints);
extern void ncr53c8xx_setup(char *str, int *ints);
extern void eata2x_setup(char *str, int *ints);
extern void u14_34f_setup(char *str, int *ints);
extern void fdomain_setup(char *str, int *ints);
extern void ibmmca_scsi_setup(char *str, int *ints);
extern void in2000_setup(char *str, int *ints);
extern void NCR53c406a_setup(char *str, int *ints);
extern void wd7000_setup(char *str, int *ints);
extern void scsi_luns_setup(char *str, int *ints);
extern void scsi_logging_setup(char *str, int *ints);
extern void sound_setup(char *str, int *ints);
extern void reboot_setup(char *str, int *ints);
extern void video_setup(char *str, int *ints);
#ifdef CONFIG_CDU31A
extern void cdu31a_setup(char *str, int *ints);
#endif CONFIG_CDU31A
#ifdef CONFIG_BLK_DEV_PS2
extern void ed_setup(char *str, int *ints);
extern void tp720_setup(char *str, int *ints);
#endif CONFIG_BLK_DEV_PS2
#ifdef CONFIG_MCD
extern void mcd_setup(char *str, int *ints);
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
extern void mcdx_setup(char *str, int *ints);
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
extern void sbpcd_setup(char *str, int *ints);
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
extern void aztcd_setup(char *str, int *ints);
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
extern void sonycd535_setup(char *str, int *ints);
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
extern void gscd_setup(char *str, int *ints);
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
extern void cm206_setup(char *str, int *ints);
#endif CONFIG_CM206
#ifdef CONFIG_OPTCD
extern void optcd_setup(char *str, int *ints);
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
extern void sjcd_setup(char *str, int *ints);
#endif CONFIG_SJCD
#ifdef CONFIG_ISP16_CDI
extern void isp16_setup(char *str, int *ints);
#endif CONFIG_ISP16_CDI
#ifdef CONFIG_BLK_DEV_RAM
static void ramdisk_start_setup(char *str, int *ints);
static void load_ramdisk(char *str, int *ints);
static void prompt_ramdisk(char *str, int *ints);
static void ramdisk_size(char *str, int *ints);
#ifdef CONFIG_BLK_DEV_INITRD
static void no_initrd(char *s,int *ints);
#endif
#endif CONFIG_BLK_DEV_RAM
#ifdef CONFIG_ISDN_DRV_ICN
extern void icn_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ISDN_DRV_HISAX
extern void HiSax_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DIGIEPCA
extern void epca_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ISDN_DRV_PCBIT
extern void pcbit_setup(char *str, int *ints);
#endif

#ifdef CONFIG_ATARIMOUSE
extern void atari_mouse_setup (char *str, int *ints);
#endif
#ifdef CONFIG_DMASOUND
extern void dmasound_setup (char *str, int *ints);
#endif
#ifdef CONFIG_ATARI_SCSI
extern void atari_scsi_setup (char *str, int *ints);
#endif
extern void wd33c93_setup (char *str, int *ints);
extern void gvp11_setup (char *str, int *ints);
extern void ncr53c7xx_setup (char *str, int *ints);

#ifdef CONFIG_CYCLADES
extern void cy_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DIGI
extern void pcxx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_RISCOM8
extern void riscom8_setup(char *str, int *ints);
#endif
#ifdef CONFIG_SPECIALIX
extern void specialix_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DMASCC
extern void dmascc_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BAYCOM_PAR
extern void baycom_par_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BAYCOM_SER_FDX
extern void baycom_ser_fdx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BAYCOM_SER_HDX
extern void baycom_ser_hdx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_SOUNDMODEM
extern void sm_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PMAC_CONSOLE
extern void pmac_cons_setup(char *str, int *ints);
extern void pmac_vmode_setup(char *str, int *ints);
#endif
#ifdef CONFIG_WDT
extern void wdt_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARPORT
extern void parport_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PLIP
extern void plip_setup(char *str, int *ints);
#endif
#ifdef CONFIG_HFMODEM
extern void hfmodem_setup(char *str, int *ints);
#endif
#ifdef CONFIG_IP_PNP
extern void ip_auto_config_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ROOT_NFS
extern void nfs_root_setup(char *str, int *ints);
#endif
#ifdef CONFIG_FTAPE
extern void ftape_setup(char *str, int *ints);
#endif

#if defined(CONFIG_SYSVIPC)
extern void ipc_init(void);
#endif

#ifdef CONFIG_MD_BOOT
extern void md_setup(char *str,int *ints) __init;
#endif

#ifdef __sparc__
extern int serial_console;
#endif

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 8
#define MAX_INIT_ENVS 8

extern void time_init(void);

static unsigned long memory_start = 0;
static unsigned long memory_end = 0;

int rows, cols;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_size;		/* Size of the ramdisk(s) */
extern int rd_image_start;	/* starting block # of image */
#ifdef CONFIG_BLK_DEV_INITRD
kdev_t real_root_dev;
#endif
#endif

int root_mountflags = MS_RDONLY;
char *execute_command = NULL;

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
static char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };

char *get_options(char *str, int *ints)
{
	char *cur = str;
	int i=1;

	while (cur && isdigit(*cur) && i <= 10) {
		ints[i++] = simple_strtoul(cur,NULL,0);
		if ((cur = strchr(cur,',')) != NULL)
			cur++;
	}
	ints[0] = i-1;
	return(cur);
}

#ifdef CONFIG_PROFILE
__initfunc(static void profile_setup(char *str, int *ints))
{
	if (ints[0] > 0)
		prof_shift = (unsigned long) ints[1];
	else
#ifdef CONFIG_PROFILE_SHIFT
		prof_shift = CONFIG_PROFILE_SHIFT;
#else
		prof_shift = 2;
#endif
}
#endif


static struct dev_name_struct {
	const char *name;
	const int num;
} root_dev_names[] __initdata = {
#ifdef CONFIG_ROOT_NFS
	{ "nfs",     0x00ff },
#endif
#ifdef CONFIG_BLK_DEV_IDE
	{ "hda",     0x0300 },
	{ "hdb",     0x0340 },
	{ "hdc",     0x1600 },
	{ "hdd",     0x1640 },
	{ "hde",     0x2100 },
	{ "hdf",     0x2140 },
	{ "hdg",     0x2200 },
	{ "hdh",     0x2240 },
#endif
#ifdef CONFIG_BLK_DEV_SD
	{ "sda",     0x0800 },
	{ "sdb",     0x0810 },
	{ "sdc",     0x0820 },
	{ "sdd",     0x0830 },
	{ "sde",     0x0840 },
#endif
#ifdef CONFIG_ATARI_ACSI
	{ "ada",     0x1c00 },
	{ "adb",     0x1c10 },
	{ "adc",     0x1c20 },
	{ "add",     0x1c30 },
	{ "ade",     0x1c40 },
#endif
#ifdef CONFIG_BLK_DEV_FD
	{ "fd",      0x0200 },
#endif
#ifdef CONFIG_MD_BOOT
	{ "md",      0x0900 },	     
#endif     
#ifdef CONFIG_BLK_DEV_XD
	{ "xda",     0x0d00 },
	{ "xdb",     0x0d40 },
#endif
#ifdef CONFIG_BLK_DEV_RAM
	{ "ram",     0x0100 },
#endif
#ifdef CONFIG_BLK_DEV_SR
	{ "scd",     0x0b00 },
#endif
#ifdef CONFIG_MCD
	{ "mcd",     0x1700 },
#endif
#ifdef CONFIG_CDU535
	{ "cdu535",  0x1800 },
	{ "sonycd",  0x1800 },
#endif
#ifdef CONFIG_AZTCD
	{ "aztcd",   0x1d00 },
#endif
#ifdef CONFIG_CM206
	{ "cm206cd", 0x2000 },
#endif
#ifdef CONFIG_GSCD
	{ "gscd",    0x1000 },
#endif
#ifdef CONFIG_SBPCD
	{ "sbpcd",   0x1900 },
#endif
#ifdef CONFIG_BLK_DEV_PS2
	{ "eda",     0x2400 },
#endif
#if CONFIG_APBLOCK
	{ "apblock", APBLOCK_MAJOR << 8},
#endif
#if CONFIG_DDV
	{ "ddv", DDV_MAJOR << 8},
#endif
	{ NULL, 0 }
};

__initfunc(kdev_t name_to_kdev_t(char *line))
{
	int base = 0;
	if (strncmp(line,"/dev/",5) == 0) {
		struct dev_name_struct *dev = root_dev_names;
		line += 5;
		do {
			int len = strlen(dev->name);
			if (strncmp(line,dev->name,len) == 0) {
				line += len;
				base = dev->num;
				break;
			}
			dev++;
		} while (dev->name);
	}
	return to_kdev_t(base + simple_strtoul(line,NULL,base?10:16));
}

__initfunc(static void root_dev_setup(char *line, int *num))
{
	ROOT_DEV = name_to_kdev_t(line);
}

/*
 * List of kernel command line parameters. The first table lists parameters
 * which are subject to values parsing (leading numbers are converted to
 * an array of ints and chopped off the string), the second table contains
 * the few exceptions which obey their own syntax rules.
 */

struct kernel_param {
	const char *str;
	void (*setup_func)(char *, int *);
};

static struct kernel_param cooked_params[] __initdata = {
/* FIXME: make PNP just become reserve_setup */
#ifndef CONFIG_KERNEL_PNP_RESOURCE
	{ "reserve=", reserve_setup },
#else
	{ "reserve=", pnp_reserve_setup },
#endif
#ifdef CONFIG_PROFILE
	{ "profile=", profile_setup },
#endif
#ifdef __SMP__
	{ "nosmp", smp_setup },
	{ "maxcpus=", smp_setup },
	{ "pirq=", ioapic_pirq_setup },
#endif
#ifdef CONFIG_BLK_DEV_RAM
	{ "ramdisk_start=", ramdisk_start_setup },
	{ "load_ramdisk=", load_ramdisk },
	{ "prompt_ramdisk=", prompt_ramdisk },
	{ "ramdisk=", ramdisk_size },
	{ "ramdisk_size=", ramdisk_size },
#ifdef CONFIG_BLK_DEV_INITRD
	{ "noinitrd", no_initrd },
#endif
#endif
#if defined (CONFIG_AMIGA) || defined (CONFIG_ATARI)
	{ "video=", video_setup },
#endif
	{ "panic=", panic_setup },
	{ "console=", console_setup },
#ifdef CONFIG_VT
	{ "no-scroll", no_scroll },
#endif
#ifdef CONFIG_BUGi386
	{ "no-hlt", no_halt },
	{ "no387", no_387 },
	{ "reboot=", reboot_setup },
#endif
#ifdef CONFIG_INET
	{ "ether=", eth_setup },
#endif
#ifdef CONFIG_ARCNET_COM20020
	{ "com20020=", com20020_setup },
#endif
#ifdef CONFIG_ARCNET_RIM_I
	{ "arcrimi=", arcrimi_setup },
#endif
#ifdef CONFIG_ARCNET_COM90xxIO
	{ "com90io=", com90io_setup },
#endif
#ifdef CONFIG_ARCNET_COM90xx
	{ "com90xx=", com90xx_setup },
#endif
#ifdef CONFIG_DECNET
	{ "decnet=", decnet_setup },
#endif
#ifdef CONFIG_PRINTER
        { "lp=", lp_setup },
#endif
#ifdef CONFIG_JOYSTICK
	{ "js=", js_setup },
#endif
#ifdef CONFIG_SCSI
	{ "max_scsi_luns=", scsi_luns_setup },
	{ "scsi_logging=", scsi_logging_setup },
#endif
#ifdef CONFIG_SCSI_ADVANSYS
	{ "advansys=", advansys_setup },
#endif
#if defined(CONFIG_BLK_DEV_HD)
	{ "hd=", hd_setup },
#endif
#ifdef CONFIG_CHR_DEV_ST
	{ "st=", st_setup },
#endif
#ifdef CONFIG_BUSMOUSE
	{ "bmouse=", bmouse_setup },
#endif
#ifdef CONFIG_MS_BUSMOUSE
	{ "msmouse=", msmouse_setup },
#endif
#ifdef CONFIG_SCSI_SEAGATE
	{ "st0x=", st0x_setup },
	{ "tmc8xx=", tmc8xx_setup },
#endif
#ifdef CONFIG_SCSI_T128
	{ "t128=", t128_setup },
#endif
#ifdef CONFIG_SCSI_PAS16
	{ "pas16=", pas16_setup },
#endif
#ifdef CONFIG_SCSI_GENERIC_NCR5380
	{ "ncr5380=", generic_NCR5380_setup },
	{ "ncr53c400=", generic_NCR53C400_setup },
#endif
#ifdef CONFIG_SCSI_AHA152X
	{ "aha152x=", aha152x_setup},
#endif
#ifdef CONFIG_SCSI_AHA1542
	{ "aha1542=", aha1542_setup},
#endif
#ifdef CONFIG_SCSI_GDTH
	{ "gdth=", gdth_setup},
#endif
#ifdef CONFIG_SCSI_AIC7XXX
	{ "aic7xxx=", aic7xxx_setup},
#endif
#ifdef CONFIG_SCSI_BUSLOGIC
	{ "BusLogic=", BusLogic_Setup},
#endif
#ifdef CONFIG_SCSI_NCR53C8XX
	{ "ncr53c8xx=", ncr53c8xx_setup},
#endif
#ifdef CONFIG_SCSI_EATA
	{ "eata=", eata2x_setup},
#endif
#ifdef CONFIG_SCSI_U14_34F
	{ "u14-34f=", u14_34f_setup},
#endif
#ifdef CONFIG_SCSI_AM53C974
        { "AM53C974=", AM53C974_setup},
#endif
#ifdef CONFIG_SCSI_NCR53C406A
	{ "ncr53c406a=", NCR53c406a_setup},
#endif
#ifdef CONFIG_SCSI_FUTURE_DOMAIN
	{ "fdomain=", fdomain_setup},
#endif
#ifdef CONFIG_SCSI_IN2000
	{ "in2000=", in2000_setup},
#endif
#ifdef CONFIG_SCSI_7000FASST
	{ "wd7000=", wd7000_setup},
#endif
#ifdef CONFIG_SCSI_IBMMCA
        { "ibmmcascsi=", ibmmca_scsi_setup },
#endif
#ifdef CONFIG_BLK_DEV_XD
	{ "xd=", xd_setup },
	{ "xd_geo=", xd_manual_geo_init },
#endif
#if defined(CONFIG_BLK_DEV_FD) || defined(CONFIG_AMIGA_FLOPPY) || defined(CONFIG_ATARI_FLOPPY)
	{ "floppy=", floppy_setup },
#endif
#ifdef CONFIG_BLK_DEV_PS2
	{ "ed=", ed_setup },
	{ "tp720=", tp720_setup },
#endif
#ifdef CONFIG_CDU31A
	{ "cdu31a=", cdu31a_setup },
#endif CONFIG_CDU31A
#ifdef CONFIG_MCD
	{ "mcd=", mcd_setup },
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
	{ "mcdx=", mcdx_setup },
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
	{ "sbpcd=", sbpcd_setup },
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
	{ "aztcd=", aztcd_setup },
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
	{ "sonycd535=", sonycd535_setup },
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
	{ "gscd=", gscd_setup },
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
	{ "cm206=", cm206_setup },
#endif CONFIG_CM206
#ifdef CONFIG_OPTCD
	{ "optcd=", optcd_setup },
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
	{ "sjcd=", sjcd_setup },
#endif CONFIG_SJCD
#ifdef CONFIG_ISP16_CDI
	{ "isp16=", isp16_setup },
#endif CONFIG_ISP16_CDI
#ifdef CONFIG_SOUND
	{ "sound=", sound_setup },
#endif
#ifdef CONFIG_ISDN_DRV_ICN
	{ "icn=", icn_setup },
#endif
#ifdef CONFIG_ISDN_DRV_HISAX
       { "hisax=", HiSax_setup },
       { "HiSax=", HiSax_setup },
#endif
#ifdef CONFIG_ISDN_DRV_PCBIT
	{ "pcbit=", pcbit_setup },
#endif
#ifdef CONFIG_ATARIMOUSE
	{ "atamouse=", atari_mouse_setup },
#endif
#ifdef CONFIG_DMASOUND
	{ "dmasound=", dmasound_setup },
#endif
#ifdef CONFIG_ATARI_SCSI
	{ "atascsi=", atari_scsi_setup },
#endif
#if defined(CONFIG_A4000T_SCSI) || defined(CONFIG_WARPENGINE_SCSI) || defined(CONFIG_A4091_SCSI)
        { "53c7xx=", ncr53c7xx_setup },
#endif
#if defined(CONFIG_A3000_SCSI) || defined(CONFIG_A2091_SCSI) \
	    || defined(CONFIG_GVP11_SCSI)
	{ "wd33c93=", wd33c93_setup },
#endif
#if defined(CONFIG_GVP11_SCSI)
	{ "gvp11=", gvp11_setup },
#endif
#ifdef CONFIG_CYCLADES
	{ "cyclades=", cy_setup },
#endif
#ifdef CONFIG_DIGI
	{ "digi=", pcxx_setup },
#endif
#ifdef CONFIG_DIGIEPCA
	{ "digiepca=", epca_setup },
#endif
#ifdef CONFIG_RISCOM8
	{ "riscom8=", riscom8_setup },
#endif
#ifdef CONFIG_DMASCC
	{ "dmascc=", dmascc_setup },
#endif
#ifdef CONFIG_SPECIALIX
	{ "specialix=", specialix_setup },
#endif
#ifdef CONFIG_BAYCOM_PAR
	{ "baycom_par=", baycom_par_setup },
#endif
#ifdef CONFIG_BAYCOM_SER_FDX
	{ "baycom_ser_fdx=", baycom_ser_fdx_setup },
#endif
#ifdef CONFIG_BAYCOM_SER_HDX
	{ "baycom_ser_hdx=", baycom_ser_hdx_setup },
#endif
#ifdef CONFIG_SOUNDMODEM
	{ "soundmodem=", sm_setup },
#endif
#ifdef CONFIG_WDT
	{ "wdt=", wdt_setup },
#endif
#ifdef CONFIG_PARPORT
	{ "parport=", parport_setup },
#endif
#ifdef CONFIG_PLIP
	{ "plip=", plip_setup },
#endif
#ifdef CONFIG_HFMODEM
	{ "hfmodem=", hfmodem_setup },
#endif
#ifdef CONFIG_PMAC_CONSOLE
	{ "console=", pmac_cons_setup },
	{ "vmode=", pmac_vmode_setup },
#endif
#ifdef CONFIG_FTAPE
	{ "ftape=", ftape_setup},
#endif
#ifdef CONFIG_MD_BOOT
	{ "md=", md_setup},
#endif
	{ 0, 0 }
};

static struct kernel_param raw_params[] __initdata = {
	{ "root=", root_dev_setup },
#ifdef CONFIG_ROOT_NFS
	{ "nfsroot=", nfs_root_setup },
	{ "nfsaddrs=", ip_auto_config_setup },
#endif
#ifdef CONFIG_IP_PNP
	{ "ip=", ip_auto_config_setup },
#endif
#ifdef CONFIG_PARIDE_PD
	{ "pd.", pd_setup },
#endif
#ifdef CONFIG_PARIDE_PCD
	{ "pcd.", pcd_setup },
#endif
#ifdef CONFIG_PARIDE_PF
	{ "pf.", pf_setup },
#endif
#ifdef CONFIG_PARIDE_PT
        { "pt.", pt_setup },
#endif
	{ 0, 0 }
};

#ifdef CONFIG_BLK_DEV_RAM
__initfunc(static void ramdisk_start_setup(char *str, int *ints))
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_image_start = ints[1];
}

__initfunc(static void load_ramdisk(char *str, int *ints))
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_doload = ints[1] & 1;
}

__initfunc(static void prompt_ramdisk(char *str, int *ints))
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_prompt = ints[1] & 1;
}

__initfunc(static void ramdisk_size(char *str, int *ints))
{
	if (ints[0] > 0 && ints[1] >= 0)
		rd_size = ints[1];
}
#endif

__initfunc(static int checksetup(char *line))
{
	int i, ints[11];

#ifdef CONFIG_BLK_DEV_IDE
	/* ide driver needs the basic string, rather than pre-processed values */
	if (!strncmp(line,"ide",3) || (!strncmp(line,"hd",2) && line[2] != '=')) {
		ide_setup(line);
		return 1;
	}
#endif
	for (i=0; raw_params[i].str; i++) {
		int n = strlen(raw_params[i].str);
		if (!strncmp(line,raw_params[i].str,n)) {
			raw_params[i].setup_func(line+n, NULL);
			return 1;
		}
	}
	for (i=0; cooked_params[i].str; i++) {
		int n = strlen(cooked_params[i].str);
		if (!strncmp(line,cooked_params[i].str,n)) {
			cooked_params[i].setup_func(get_options(line+n, ints), ints);
			return 1;
		}
	}
	return 0;
}

/* this should be approx 2 Bo*oMips to start (note initial shift), and will
   still work even if initially too large, it will just take slightly longer */
unsigned long loops_per_sec = (1<<12);

/* This is the number of bits of precision for the loops_per_second.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

__initfunc(void calibrate_delay(void))
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	loops_per_sec = (1<<12);

	printk("Calibrating delay loop... ");
	while (loops_per_sec <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_sec);
		ticks = jiffies - ticks;
		if (ticks)
			break;
		}

/* Do a binary approximation to get loops_per_second set to equal one clock
   (up to lps_precision bits) */
	loops_per_sec >>= 1;
	loopbit = loops_per_sec;
	while ( lps_precision-- && (loopbit >>= 1) ) {
		loops_per_sec |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_sec);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_sec &= ~loopbit;
	}

/* finally, adjust loops per second in terms of seconds instead of clocks */	
	loops_per_sec *= HZ;
/* Round the value and print it */	
	printk("%lu.%02lu BogoMIPS\n",
		(loops_per_sec+2500)/500000,
		((loops_per_sec+2500)/5000) % 100);
}

/*
 * This is a simple kernel command line parsing function: it parses
 * the command line, and fills in the arguments/environment to init
 * as appropriate. Any cmd-line option is taken to be an environment
 * variable if it contains the character '='.
 *
 * This routine also checks for options meant for the kernel.
 * These options are not given to init - they are for internal kernel use only.
 */
__initfunc(static void parse_options(char *line))
{
	char *next;
	int args, envs;

	if (!*line)
		return;
	args = 0;
	envs = 1;	/* TERM is set to 'linux' by default */
	next = line;
	while ((line = next) != NULL) {
		if ((next = strchr(line,' ')) != NULL)
			*next++ = 0;
		/*
		 * check for kernel options first..
		 */
		if (!strcmp(line,"ro")) {
			root_mountflags |= MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"rw")) {
			root_mountflags &= ~MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"debug")) {
			console_loglevel = 10;
			continue;
		}
		if (!strncmp(line,"init=",5)) {
			line += 5;
			execute_command = line;
			/* In case LILO is going to boot us with default command line,
			 * it prepends "auto" before the whole cmdline which makes
			 * the shell think it should execute a script with such name.
			 * So we ignore all arguments entered _before_ init=... [MJ]
			 */
			args = 0;
			continue;
		}
		if (checksetup(line))
			continue;
		
		/*
		 * Then check if it's an environment variable or
		 * an option.
		 */
		if (strchr(line,'=')) {
			if (envs >= MAX_INIT_ENVS)
				break;
			envp_init[++envs] = line;
		} else {
			if (args >= MAX_INIT_ARGS)
				break;
			argv_init[++args] = line;
		}
	}
	argv_init[args+1] = NULL;
	envp_init[envs+1] = NULL;
}


extern void setup_arch(char **, unsigned long *, unsigned long *);

#ifndef __SMP__

/*
 *	Uniprocessor idle thread
 */
 
int cpu_idle(void *unused)
{
	for(;;)
		idle();
}

#else

/*
 *	Multiprocessor idle thread is in arch/...
 */
 
extern int cpu_idle(void * unused);

/* Called by boot processor to activate the rest. */
__initfunc(static void smp_init(void))
{
	/* Get other processors into their bootup holding patterns. */
	smp_boot_cpus();
}		

/*
 *	The autoprobe routines assume CPU#0 on the i386
 *	so we don't actually set the game in motion until
 *	they are finished.
 */
 
__initfunc(static void smp_begin(void))
{
	smp_threads_ready=1;
	smp_commence();
}
	
#endif

extern void initialize_secondary(void);

/*
 *	Activate the first processor.
 */
 
__initfunc(asmlinkage void start_kernel(void))
{
	char * command_line;

#ifdef __SMP__
	static int boot_cpu = 1;
	/* "current" has been set up, we need to load it now */
	if (!boot_cpu)
		initialize_secondary();
	boot_cpu = 0;
#endif

/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	printk(linux_banner);
	setup_arch(&command_line, &memory_start, &memory_end);
	memory_start = paging_init(memory_start,memory_end);
	trap_init();
	init_IRQ();
	sched_init();
	time_init();
	parse_options(command_line);
	memory_start = console_init(memory_start,memory_end);
#ifdef CONFIG_MODULES
	init_modules();
#endif
#ifdef CONFIG_PROFILE
	if (prof_shift) {
		prof_buffer = (unsigned int *) memory_start;
		/* only text is profiled */
		prof_len = (unsigned long) &_etext - (unsigned long) &_stext;
		prof_len >>= prof_shift;
		memory_start += prof_len * sizeof(unsigned int);
		memset(prof_buffer, 0, prof_len * sizeof(unsigned int));
	}
#endif
#ifdef CONFIG_SBUS
	memory_start = sbus_init(memory_start,memory_end);
#endif
#if defined(CONFIG_PMAC) || defined(CONFIG_CHRP)
	memory_start = powermac_init(memory_start, memory_end);
#endif
#if defined(CONFIG_PCI) && defined(CONFIG_PCI_CONSOLE)
	memory_start = pci_init(memory_start,memory_end);
#endif
#if HACK
	memory_start = console_init(memory_start,memory_end);
#endif
#if defined(CONFIG_PCI) && !defined(CONFIG_PCI_CONSOLE)
	memory_start = pci_init(memory_start,memory_end);
#endif
#ifdef CONFIG_MCA
	memory_start = mca_init(memory_start,memory_end);
#endif
	memory_start = kmem_cache_init(memory_start, memory_end);
	sti();
	calibrate_delay();
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok && initrd_start < memory_start) {
		printk(KERN_CRIT "initrd overwritten (0x%08lx < 0x%08lx) - "
		    "disabling it.\n",initrd_start,memory_start);
		initrd_start = 0;
	}
#endif
	mem_init(memory_start,memory_end);
	kmem_cache_sizes_init();
#ifdef CONFIG_PROC_FS
	proc_root_init();
#endif
	uidcache_init();
	filescache_init();
	dcache_init();
	vma_init();
	buffer_init();
	signals_init();
	inode_init();
	file_table_init();
	sock_init();
#if defined(CONFIG_SYSVIPC)
	ipc_init();
#endif
	dquot_init();
	check_bugs();

	printk("POSIX conformance testing by UNIFIX\n");
#ifdef __SMP__
	smp_init();
#endif
#ifdef CONFIG_SYSCTL
	sysctl_init();
#endif
	/* 
	 *	We count on the initial thread going ok 
	 *	Like idlers init is an unlocked kernel thread, which will
	 *	make syscalls (and thus be locked).
	 */
	kernel_thread(init, NULL, 0);
/*
 * task[0] is meant to be used as an "idle" task: it may not sleep, but
 * it might do some general things like count free pages or it could be
 * used to implement a reasonable LRU algorithm for the paging routines:
 * anything that can be useful, but shouldn't take time from the real
 * processes.
 *
 * Right now task[0] just does an infinite idle loop.
 */
 	cpu_idle(NULL);
}

#ifdef CONFIG_BLK_DEV_INITRD
static int do_linuxrc(void * shell)
{
	static char *argv[] = { "linuxrc", NULL, };

	close(0);close(1);close(2);
	setsid();
	(void) open("/dev/console",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	return execve(shell, argv, envp_init);
}

__initfunc(static void no_initrd(char *s,int *ints))
{
	mount_initrd = 0;
}
#endif

static int init(void * unused)
{
#ifdef CONFIG_BLK_DEV_INITRD
	int real_root_mountflags;
#endif

	/* Launch bdflush from here, instead of the old syscall way. */
	kernel_thread(bdflush, NULL, 0);
	/* Start the background pageout daemon. */
	kswapd_setup();
	kernel_thread(kswapd, NULL, 0);

#if CONFIG_AP1000
	/* Start the async paging daemon. */
	{
	  extern int asyncd(void *);	 
	  kernel_thread(asyncd, NULL, 0);
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	real_root_dev = ROOT_DEV;
	real_root_mountflags = root_mountflags;
	if (initrd_start && mount_initrd) root_mountflags &= ~MS_RDONLY;
	else mount_initrd =0;
#endif
	setup(0);

#ifdef __SMP__
	/*
	 *	With the devices probed and setup we can
	 *	now enter SMP mode.
	 */
	
	smp_begin();
#endif	

#ifdef CONFIG_KMOD
	{
		extern int kmod_init(void);
		kmod_init();
	}
#endif

#ifdef CONFIG_UMSDOS_FS
	{
		/*
			When mounting a umsdos fs as root, we detect
			the pseudo_root (/linux) and initialise it here.
			pseudo_root is defined in fs/umsdos/inode.c
		*/
		extern struct inode *pseudo_root;
		if (pseudo_root != NULL){
			current->fs->root = pseudo_root;
			current->fs->pwd  = pseudo_root;
		}
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	root_mountflags = real_root_mountflags;
	if (mount_initrd && ROOT_DEV != real_root_dev
	    && MAJOR(ROOT_DEV) == RAMDISK_MAJOR && MINOR(ROOT_DEV) == 0) {
		int error;
		int i, pid;

		pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
		if (pid>0)
			while (pid != wait(&i));
		if (MAJOR(real_root_dev) != RAMDISK_MAJOR
		     || MINOR(real_root_dev) != 0) {
			error = change_root(real_root_dev,"/initrd");
			if (error)
				printk(KERN_ERR "Change root to /initrd: "
				    "error %d\n",error);
		}
	}
#endif

	setup(1);
	
	if (open("/dev/console", O_RDWR, 0) < 0)
		printk("Warning: unable to open an initial console.\n");

	(void) dup(0);
	(void) dup(0);
	
	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are 
	 * trying to recover a really broken machine.
	 */

	if (execute_command)
		execve(execute_command,argv_init,envp_init);
	execve("/sbin/init",argv_init,envp_init);
	execve("/etc/init",argv_init,envp_init);
	execve("/bin/init",argv_init,envp_init);
	execve("/bin/sh",argv_init,envp_init);
	panic("No init found.  Try passing init= option to kernel.");
}
