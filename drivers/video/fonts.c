/*
 * linux/drivers/video/fonts.c -- `Soft' font definitions
 *
 *    Created 1995 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */


#include <linux/config.h> /* for CONFIG_AMIGA */
#include <linux/types.h>
#include <linux/string.h>
#ifdef __mc68000__
#include <asm/setup.h>
#endif
#include "font.h"


   /*
    *    External Font Definitions
    */

/* VGA8x8 */
extern char fontname_8x8[];
extern int fontwidth_8x8, fontheight_8x8;
extern u_char fontdata_8x8[];

/* VGA8x16 */
extern char fontname_8x16[];
extern int fontwidth_8x16, fontheight_8x16;
extern u_char fontdata_8x16[];

/* PEARL8x8 */
extern char fontname_pearl8x8[];
extern int fontwidth_pearl8x8, fontheight_pearl8x8;
extern u_char fontdata_pearl8x8[];


   /*
    *    Font Descriptor Array
    */

struct softfontdesc {
   char *name;
   int *width;
   int *height;
   u_char *data;
};

#define VGA8x8_IDX	0
#define VGA8x16_IDX	1
#define PEARL8x8_IDX	2

static struct softfontdesc softfonts[] = {
   { fontname_8x8, &fontwidth_8x8, &fontheight_8x8, fontdata_8x8 },
   { fontname_8x16, &fontwidth_8x16, &fontheight_8x16, fontdata_8x16 },
   { fontname_pearl8x8, &fontwidth_pearl8x8, &fontheight_pearl8x8,
     fontdata_pearl8x8 },
};

static unsigned int numsoftfonts = sizeof(softfonts)/sizeof(*softfonts);


   /*
    *    Find a font with a specific name
    */

int findsoftfont(char *name, int *width, int *height, u_char *data[])
{
   unsigned int i;

   for (i = 0; i < numsoftfonts; i++)
      if (!strcmp(softfonts[i].name, name)) {
         if (width)
            *width = *softfonts[i].width;
         if (height)
            *height = *softfonts[i].height;
         if (data)
            *data = softfonts[i].data;
			return(1);
      }
	return(0);
}


   /*
    *    Get the default font for a specific screen size
    */

void getdefaultfont(int xres, int yres, char *name[], int *width, int *height,
                    u_char *data[])
{
    int i;
    
    if (yres < 400) {
	i = VGA8x8_IDX;
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA)
	    i = PEARL8x8_IDX;
#endif
    } else
	i = VGA8x16_IDX;

    if (name)
	*name = softfonts[i].name;
    if (width)
	*width = *softfonts[i].width;
    if (height)
	*height = *softfonts[i].height;
    if (data)
	*data = softfonts[i].data;
}
