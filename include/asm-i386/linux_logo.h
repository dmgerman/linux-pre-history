/* $Id: linux_logo.h,v 1.3 1998/06/07 21:49:54 geert Exp $
 * include/asm-i386/linux_logo.h: This is a linux logo
 *                                to be displayed on boot.
 *
 * Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * You can put anything here, but:
 * LINUX_LOGO_COLORS has to be less than 224
 * image size has to be 80x80
 * values have to start from 0x20
 * (i.e. RGB(linux_logo_red[0],
 *	     linux_logo_green[0],
 *	     linux_logo_blue[0]) is color 0x20)
 * BW image has to be 80x80 as well, with MS bit
 * on the left
 * Serial_console ascii image can be any size,
 * but should contain %s to display the version
 */
 
#include <linux/init.h>
#include <linux/version.h>

#define linux_logo_banner "Linux/ia32 version " UTS_RELEASE

#define LINUX_LOGO_COLORS 221

#ifdef INCLUDE_LINUX_LOGO_DATA

#define INCLUDE_LINUX_LOGO16
#include <linux/linux_logo.h>

/* Painted by Johnny Stenback <jst@uwasa.fi> */

unsigned char *linux_serial_image __initdata = "\n"
"         .u$e.\n"
"       .$$$$$:S\n"
"       $\"*$/\"*$$\n"
"       $.`$ . ^F\n"
"       4k+#+T.$F\n"
"       4P+++\"$\"$\n"
"       :R\"+  t$$B\n"
"    ___#       $$$\n"
"    |  |       R$$k\n"
"   dd. | Linux  $!$\n"
"   ddd |  ia32  $9$F\n"
" '!!!!!$       !!#!`\n"
"  !!!!!*     .!!!!!`\n"
"'!!!!!!!W..e$$!!!!!!`\n"
" \"~^^~         ^~~^\n"
"\n";

/* The following created by Andrew Apted, May 1998 */

unsigned char *linux_mda_image __initdata = "\n"
"LINUX/IA32..........................\n"
"::::::::::::::        ::::::::::::::\n"
"::::::::::::             :::::::::::\n"
"::::::::::::  ##   ##    :::::::::::\n"
":::::::::::: # xxxx ##   :::::::::::\n"
":::::::::::: xxxxxxxxx    ::::::::::\n"
":::::::::::: ##xxx####     :::::::::\n"
"::::::::::: ###########     ::::::::\n"
":::::::::  #############      ::::::\n"
"::::::::  ###############      :::::\n"
"::::::   #################      ::::\n"
"::::::  ##################      ::::\n"
":::::xxx##################      ::::\n"
":::xxxxxx  #############xxx   xxx:::\n"
"xxxxxxxxxxx   ##########xxxxxxxxxx::\n"
"xxxxxxxxxxxx ########## xxxxxxxxxxxx\n"
"xxxxxxxxxxxxx#######    xxxxxxxxxxx:\n"
":::::xxxxxxxx:::::::::::xxxxxx::::::\n\n";

#else

/* prototypes only */
extern unsigned char linux_logo_red[];
extern unsigned char linux_logo_green[];
extern unsigned char linux_logo_blue[];
extern unsigned char linux_logo[];
extern unsigned char linux_logo_bw[];
extern unsigned char linux_logo16_red[];
extern unsigned char linux_logo16_green[];
extern unsigned char linux_logo16_blue[];
extern unsigned char linux_logo16[];
extern unsigned char *linux_serial_image;
extern unsigned char *linux_mda_image;

extern int (*console_show_logo)(void);

#endif
