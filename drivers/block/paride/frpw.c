/* 
	frpw.c	(c) 1997  Grant R. Guenther <grant@torque.net>
		          Under the terms of the GNU public license

	frpw.c is a low-level protocol driver for the Freecom "Power"
	parallel port IDE adapter.
	
*/

#define	FRPW_VERSION	"1.0" 

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>

#include "paride.h"

#define cec4		w2(0xc);w2(0xe);w2(0xe);w2(0xc);w2(4);w2(4);w2(4);
#define j44(l,h)	(((l>>4)&0x0f)|(h&0xf0))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x08, 0x10 };

static int frpw_read_regr( PIA *pi, int cont, int regr )

{	int	h,l,r;

	r = regr + cont_map[cont];

	w2(4);
	w0(r); cec4;
	w2(6); l = r1();
	w2(4); h = r1();
	w2(4); 

	return j44(l,h);

}

static void frpw_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

        r = regr + cont_map[cont];

	w2(4); w0(r); cec4; 
	w0(val);
	w2(5);w2(7);w2(5);w2(4);
}

static void frpw_read_block_int( PIA *pi, char * buf, int count, int regr )

{       int     h, l, k, ph;

        switch(pi->mode) {

        case 0: w2(4); w0(regr); cec4;
                for (k=0;k<count;k++) {
                        w2(6); l = r1();
                        w2(4); h = r1();
                        buf[k] = j44(l,h);
                }
                w2(4);
                break;

        case 1: ph = 2;
                w2(4); w0(regr + 0xc0); cec4;
                w0(0xff);
                for (k=0;k<count;k++) {
                        w2(0xa4 + ph); 
                        buf[k] = r0();
                        ph = 2 - ph;
                } 
                w2(0xac); w2(0xa4); w2(4);
                break;

        case 2: w2(4); w0(regr + 0x80); cec4;
                for (k=0;k<count;k++) buf[k] = r4();
                w2(0xac); w2(0xa4);
                w2(4);
                break;

	case 3: w2(4); w0(regr + 0x80); cec4;
		for (k=0;k<count-2;k++) buf[k] = r4();
		w2(0xac); w2(0xa4);
		buf[count-2] = r4();
		buf[count-1] = r4();
		w2(4);
		break;

        }
}

static void frpw_read_block( PIA *pi, char * buf, int count)

{	frpw_read_block_int(pi,buf,count,0x08);
}

static void frpw_write_block( PIA *pi, char * buf, int count )
 
{	int	k;

	switch(pi->mode) {

	case 0:
	case 1:
	case 2: w2(4); w0(8); cec4; w2(5);
        	for (k=0;k<count;k++) {
			w0(buf[k]);
			w2(7);w2(5);
		}
		w2(4);
		break;

	case 3: w2(4); w0(0xc8); cec4; w2(5);
		for (k=0;k<count;k++) w4(buf[k]);
		w2(4);
		break;
	}
}

static void frpw_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	w2(4);
}

static void frpw_disconnect ( PIA *pi )

{       w2(4); w0(0x20); cec4;
	w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

/* Stub logic to see if PNP string is available - used to distinguish
   between the Xilinx and ASIC implementations of the Freecom adapter.
*/

static int frpw_test_pnp ( PIA *pi )

{	int olddelay, a, b;

	olddelay = pi->delay;
	pi->delay = 10;

	pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	
	w2(4); w0(4); w2(6); w2(7);
	a = r1() & 0xff; w2(4); b = r1() & 0xff;
	w2(0xc); w2(0xe); w2(4);

	pi->delay = olddelay;
        w0(pi->saved_r0);
        w2(pi->saved_r2);

	return ((~a&0x40) && (b&0x40));
} 

/*  We use pi->private to record the chip type:  
	0 = untested, 2 = Xilinx, 3 = ASIC
*/  

static int frpw_test_proto( PIA *pi, char * scratch, int verbose )

{       int     k, r;

	if (!pi->private) pi->private = frpw_test_pnp(pi) + 2;

	if ((pi->private == 2) && (pi->mode > 2)) {
	   if (verbose) 
		printk("%s: frpw: Xilinx does not support mode %d\n",
			pi->device, pi->mode);
	   return 1;
	}

	if ((pi->private == 3) && (pi->mode == 2)) {
	   if (verbose)
		printk("%s: frpw: ASIC does not support mode 2\n",
			pi->device);
	   return 1;
	}

	frpw_connect(pi);
        frpw_read_block_int(pi,scratch,512,0x10);
        r = 0;
        for (k=0;k<128;k++) if (scratch[k] != k) r++;
	frpw_disconnect(pi);

        if (verbose)  {
            printk("%s: frpw: port 0x%x, mode %d, test=%d\n",
                   pi->device,pi->port,pi->mode,r);
        }

        return r;
}


static void frpw_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[4] = {"4-bit","8-bit","EPP-X","EPP-A"};

        printk("%s: frpw %s, Freecom (%s) adapter at 0x%x, ", pi->device,
		FRPW_VERSION,(pi->private == 2)?"Xilinx":"ASIC",pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static void frpw_inc_use ( void )

{       MOD_INC_USE_COUNT;
}

static void frpw_dec_use ( void )

{       MOD_DEC_USE_COUNT;
}

struct pi_protocol frpw = {"frpw",0,4,2,2,1,
                           frpw_write_regr,
                           frpw_read_regr,
                           frpw_write_block,
                           frpw_read_block,
                           frpw_connect,
                           frpw_disconnect,
                           0,
                           0,
                           frpw_test_proto,
                           frpw_log_adapter,
                           frpw_inc_use, 
                           frpw_dec_use 
                          };


#ifdef MODULE

int     init_module(void)

{       return pi_register( &frpw ) - 1;
}

void    cleanup_module(void)

{       pi_unregister( &frpw );
}

#endif

/* end of frpw.c */
