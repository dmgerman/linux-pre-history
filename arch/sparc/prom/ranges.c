/* $Id: ranges.c,v 1.7 1996/11/13 05:10:12 davem Exp $
 * ranges.c: Handle ranges in newer proms for obio/sbus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/init.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/system.h>

struct linux_prom_ranges promlib_obio_ranges[PROMREG_MAX];
int num_obio_ranges;

/* Adjust register values based upon the ranges parameters. */
void
prom_adjust_regs(struct linux_prom_registers *regp, int nregs,
		 struct linux_prom_ranges *rangep, int nranges)
{
	int regc, rngc;

	for(regc=0; regc < nregs; regc++) {
		for(rngc=0; rngc < nranges; rngc++)
			if(regp[regc].which_io == rangep[rngc].ot_child_space)
				break; /* Fount it */
		if(rngc==nranges) /* oops */
			prom_printf("adjust_regs: Could not find range with matching bus type...\n");
		regp[regc].which_io = rangep[rngc].ot_parent_space;
		regp[regc].phys_addr += rangep[rngc].ot_parent_base;
	}
}

void
prom_adjust_ranges(struct linux_prom_ranges *ranges1, int nranges1,
		   struct linux_prom_ranges *ranges2, int nranges2)
{
	int rng1c, rng2c;

	for(rng1c=0; rng1c < nranges1; rng1c++) {
		for(rng2c=0; rng2c < nranges2; rng2c++)
			if(ranges1[rng1c].ot_child_space ==
			   ranges2[rng2c].ot_child_space) break;
		if(rng2c == nranges2) /* oops */
			prom_printf("adjust_ranges: Could not find matching bus type...\n");
		ranges1[rng1c].ot_parent_space = ranges2[rng2c].ot_parent_space;
		ranges1[rng1c].ot_parent_base += ranges2[rng2c].ot_parent_base;
	}
}

/* Apply probed obio ranges to registers passed, if no ranges return. */
void
prom_apply_obio_ranges(struct linux_prom_registers *regs, int nregs)
{
	if(num_obio_ranges)
		prom_adjust_regs(regs, nregs, promlib_obio_ranges, num_obio_ranges);
}

/* Apply probed sbus ranges to registers passed, if no ranges return. */
void
prom_apply_sbus_ranges(struct linux_sbus *sbus, struct linux_prom_registers *regs, int nregs)
{
	if(sbus->num_sbus_ranges)
	prom_adjust_regs(regs, nregs, sbus->sbus_ranges, sbus->num_sbus_ranges);
}

__initfunc(void prom_ranges_init(void))
{
	int node, obio_node;
	int success;

	num_obio_ranges = 0;

	/* Check for obio and sbus ranges. */
	node = prom_getchild(prom_root_node);
	obio_node = prom_searchsiblings(node, "obio");

	if(obio_node) {
		success = prom_getproperty(obio_node, "ranges",
					   (char *) promlib_obio_ranges,
					   sizeof(promlib_obio_ranges));
		if(success != -1)
			num_obio_ranges = (success/sizeof(struct linux_prom_ranges));
	}

	if(num_obio_ranges)
		prom_printf("PROMLIB: obio_ranges %d\n", num_obio_ranges);

	return;
}

__initfunc(void prom_sbus_ranges_init(int parentnd, struct linux_sbus *sbus))
{
	int success;
	
	sbus->num_sbus_ranges = 0;
	if(sparc_cpu_model == sun4c)
		return;
	success = prom_getproperty(sbus->prom_node, "ranges",
				   (char *) sbus->sbus_ranges,
				   sizeof (sbus->sbus_ranges));
	if (success != -1)
		sbus->num_sbus_ranges = (success/sizeof(struct linux_prom_ranges));
	if (sparc_cpu_model == sun4d) {
		struct linux_prom_ranges iounit_ranges[PROMREG_MAX];
		int num_iounit_ranges;
		
		success = prom_getproperty(parentnd, "ranges",
				   	(char *) iounit_ranges,
				   	sizeof (iounit_ranges));
		if (success != -1) {
			num_iounit_ranges = (success/sizeof(struct linux_prom_ranges));
			prom_adjust_ranges (sbus->sbus_ranges, sbus->num_sbus_ranges, iounit_ranges, num_iounit_ranges);
		}
	}
}

void
prom_apply_generic_ranges (int node, int parent, struct linux_prom_registers *regs, int nregs)
{
	int success;
	int num_ranges;
	struct linux_prom_ranges ranges[PROMREG_MAX];
	
	success = prom_getproperty(node, "ranges",
				   (char *) ranges,
				   sizeof (ranges));
	if (success != -1) {
		num_ranges = (success/sizeof(struct linux_prom_ranges));
		if (parent) {
			struct linux_prom_ranges parent_ranges[PROMREG_MAX];
			int num_parent_ranges;
		
			success = prom_getproperty(parent, "ranges",
				   		   (char *) parent_ranges,
				   		   sizeof (parent_ranges));
			if (success != -1) {
				num_parent_ranges = (success/sizeof(struct linux_prom_ranges));
				prom_adjust_ranges (ranges, num_ranges, parent_ranges, num_parent_ranges);
			}
		}
		prom_adjust_regs(regs, nregs, ranges, num_ranges);
	}
}
