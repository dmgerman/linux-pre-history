/* $Id: srmmu.c,v 1.116 1996/12/12 11:57:59 davem Exp $
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@ithil.mcst.ru)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/kdebug.h>
#include <asm/vaddrs.h>
#include <asm/traps.h>
#include <asm/smp.h>
#include <asm/mbus.h>
#include <asm/cache.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/iommu.h>
#include <asm/asi.h>
#include <asm/msi.h>
#include <asm/a.out.h>

/* Now the cpu specific definitions. */
#include <asm/viking.h>
#include <asm/mxcc.h>
#include <asm/ross.h>
#include <asm/tsunami.h>
#include <asm/swift.h>

enum mbus_module srmmu_modtype;
unsigned int hwbug_bitmask;
int vac_cache_size;
int vac_line_size;
int vac_badbits;

extern unsigned long sparc_iobase_vaddr;

#ifdef __SMP__
extern void smp_capture(void);
extern void smp_release(void);
#else
#define smp_capture()
#define smp_release()
#endif /* !(__SMP__) */

/* #define USE_CHUNK_ALLOC 1 */

static unsigned long (*mmu_getpage)(void);
static void (*ctxd_set)(ctxd_t *ctxp, pgd_t *pgdp);
static void (*pmd_set)(pmd_t *pmdp, pte_t *ptep);

static void (*flush_page_for_dma)(unsigned long page);
static void (*flush_cache_page_to_uncache)(unsigned long page);
static void (*flush_tlb_page_for_cbit)(unsigned long page);
#ifdef __SMP__
static void (*local_flush_page_for_dma)(unsigned long page);
static void (*local_flush_cache_page_to_uncache)(unsigned long page);
static void (*local_flush_tlb_page_for_cbit)(unsigned long page);
#endif

static struct srmmu_stats {
	int invall;
	int invpg;
	int invrnge;
	int invmm;
} module_stats;

static char *srmmu_name;

ctxd_t *srmmu_ctx_table_phys;
ctxd_t *srmmu_context_table;

static struct srmmu_trans {
	unsigned long vbase;
	unsigned long pbase;
	unsigned long size;
} srmmu_map[SPARC_PHYS_BANKS];

static int viking_mxcc_present = 0;

void srmmu_frob_mem_map(unsigned long start_mem)
{
	unsigned long bank_start, bank_end;
	unsigned long addr;
	int i;

	/* First, mark all pages as invalid. */
	for(addr = PAGE_OFFSET; MAP_NR(addr) < max_mapnr; addr += PAGE_SIZE)
		mem_map[MAP_NR(addr)].flags |= (1<<PG_reserved);

	start_mem = PAGE_ALIGN(start_mem);
	for(i = 0; srmmu_map[i].size; i++) {
		bank_start = srmmu_map[i].vbase;
		bank_end = bank_start + srmmu_map[i].size;
		while(bank_start < bank_end) {
			if((bank_start >= KERNBASE) &&
			   (bank_start < start_mem)) {
				bank_start += PAGE_SIZE;
				continue;
			}
			mem_map[MAP_NR(bank_start)].flags &= ~(1<<PG_reserved);
			bank_start += PAGE_SIZE;
		}
	}
}

/* Physical memory can be _very_ non-contiguous on the sun4m, especially
 * the SS10/20 class machines and with the latest openprom revisions.
 * So we have to crunch the free page pool.
 */
static inline unsigned long srmmu_v2p(unsigned long vaddr)
{
	int i;

	for(i=0; srmmu_map[i].size != 0; i++) {
		if(srmmu_map[i].vbase <= vaddr &&
		   (srmmu_map[i].vbase + srmmu_map[i].size > vaddr)) {
			return (vaddr - srmmu_map[i].vbase) + srmmu_map[i].pbase;
		}
	}
	return 0xffffffffUL;
}

static inline unsigned long srmmu_p2v(unsigned long paddr)
{
	int i;

	for(i=0; srmmu_map[i].size != 0; i++) {
		if(srmmu_map[i].pbase <= paddr &&
		   (srmmu_map[i].pbase + srmmu_map[i].size > paddr))
			return (paddr - srmmu_map[i].pbase) + srmmu_map[i].vbase;
	}
	return 0xffffffffUL;
}

/* In general all page table modifications should use the V8 atomic
 * swap instruction.  This insures the mmu and the cpu are in sync
 * with respect to ref/mod bits in the page tables.
 */
static inline unsigned long srmmu_swap(unsigned long *addr, unsigned long value)
{
#if MEM_BUS_SPACE
  /* the AP1000 has its memory on bus 8, not 0 like suns do */
  if (!(value&KERNBASE))
    value |= MEM_BUS_SPACE<<28;
  if (value == MEM_BUS_SPACE<<28) value = 0;
#endif
	__asm__ __volatile__("swap [%2], %0\n\t" :
			     "=&r" (value) :
			     "0" (value), "r" (addr));
	return value;
}

/* Functions really use this, not srmmu_swap directly. */
#define srmmu_set_entry(ptr, newentry) \
        srmmu_swap((unsigned long *) (ptr), (newentry))


/* The very generic SRMMU page table operations. */
static unsigned int srmmu_pmd_align(unsigned int addr) { return SRMMU_PMD_ALIGN(addr); }
static unsigned int srmmu_pgdir_align(unsigned int addr) { return SRMMU_PGDIR_ALIGN(addr); }

static unsigned long srmmu_vmalloc_start(void)
{
	return SRMMU_VMALLOC_START;
}

static unsigned long srmmu_pgd_page(pgd_t pgd)
{ return srmmu_p2v((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pmd_page(pmd_t pmd)
{ return srmmu_p2v((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static inline int srmmu_device_memory(pte_t pte) 
{
	return (pte_val(pte)>>28) != MEM_BUS_SPACE;
}

static unsigned long srmmu_pte_page(pte_t pte)
{ return srmmu_device_memory(pte)?~0:srmmu_p2v((pte_val(pte) & SRMMU_PTE_PMASK) << 4); }

static int srmmu_pte_none(pte_t pte)          { return !pte_val(pte); }
static int srmmu_pte_present(pte_t pte)
{ return ((pte_val(pte) & SRMMU_ET_MASK) == SRMMU_ET_PTE); }

static void srmmu_pte_clear(pte_t *ptep)      { set_pte(ptep, __pte(0)); }

static int srmmu_pmd_none(pmd_t pmd)          { return !pmd_val(pmd); }
static int srmmu_pmd_bad(pmd_t pmd)
{ return (pmd_val(pmd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static int srmmu_pmd_present(pmd_t pmd)
{ return ((pmd_val(pmd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static void srmmu_pmd_clear(pmd_t *pmdp)      { set_pte((pte_t *)pmdp, __pte(0)); }

static int srmmu_pgd_none(pgd_t pgd)          { return !pgd_val(pgd); }
static int srmmu_pgd_bad(pgd_t pgd)
{ return (pgd_val(pgd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static int srmmu_pgd_present(pgd_t pgd)
{ return ((pgd_val(pgd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static void srmmu_pgd_clear(pgd_t * pgdp)     { set_pte((pte_t *)pgdp, __pte(0)); }

static int srmmu_pte_write(pte_t pte)         { return pte_val(pte) & SRMMU_WRITE; }
static int srmmu_pte_dirty(pte_t pte)         { return pte_val(pte) & SRMMU_DIRTY; }
static int srmmu_pte_young(pte_t pte)         { return pte_val(pte) & SRMMU_REF; }

static pte_t srmmu_pte_wrprotect(pte_t pte)   { pte_val(pte) &= ~SRMMU_WRITE; return pte;}
static pte_t srmmu_pte_mkclean(pte_t pte)     { pte_val(pte) &= ~SRMMU_DIRTY; return pte; }
static pte_t srmmu_pte_mkold(pte_t pte)       { pte_val(pte) &= ~SRMMU_REF; return pte; }
static pte_t srmmu_pte_mkwrite(pte_t pte)     { pte_val(pte) |= SRMMU_WRITE; return pte; }
static pte_t srmmu_pte_mkdirty(pte_t pte)     { pte_val(pte) |= SRMMU_DIRTY; return pte; }
static pte_t srmmu_pte_mkyoung(pte_t pte)     { pte_val(pte) |= SRMMU_REF; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t srmmu_mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = ((srmmu_v2p(page)) >> 4) | pgprot_val(pgprot); return pte; }

static pte_t srmmu_mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = ((page) >> 4) | pgprot_val(pgprot); return pte; }

static pte_t srmmu_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	pte_t pte;
	pte_val(pte) = ((page) >> 4) | (space << 28) | pgprot_val(pgprot);
	return pte;
}

static void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pgdp) >> 4)));
}

static void srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pmdp) >> 4)));
}

static void srmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	set_pte((pte_t *)pmdp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) ptep) >> 4)));
}

static pte_t srmmu_pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & SRMMU_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

/* to find an entry in a top-level page table... */
static pgd_t *srmmu_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + ((address >> SRMMU_PGDIR_SHIFT) & (SRMMU_PTRS_PER_PGD - 1));
}

/* Find an entry in the second-level page table.. */
static pmd_t *srmmu_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) srmmu_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
static pte_t *srmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) srmmu_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

/* This must update the context table entry for this process. */
static void srmmu_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(current->mm);
	}
}

static inline void srmmu_uncache_page(unsigned long addr)
{
	pgd_t *pgdp = srmmu_pgd_offset(init_task.mm, addr);
	pmd_t *pmdp;
	pte_t *ptep;

	if((pgd_val(*pgdp) & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
		ptep = (pte_t *) pgdp;
	} else {
		pmdp = srmmu_pmd_offset(pgdp, addr);
		if((pmd_val(*pmdp) & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
			ptep = (pte_t *) pmdp;
		} else {
			ptep = srmmu_pte_offset(pmdp, addr);
		}
	}

	flush_cache_page_to_uncache(addr);
	set_pte(ptep, __pte((pte_val(*ptep) & ~SRMMU_CACHE)));
	flush_tlb_page_for_cbit(addr);
}

static inline void srmmu_recache_page(unsigned long addr)
{
	pgd_t *pgdp = srmmu_pgd_offset(init_task.mm, addr);
	pmd_t *pmdp;
	pte_t *ptep;

	if((pgd_val(*pgdp) & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
		ptep = (pte_t *) pgdp;
	} else {
		pmdp = srmmu_pmd_offset(pgdp, addr);
		if((pmd_val(*pmdp) & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
			ptep = (pte_t *) pmdp;
		} else {
			ptep = srmmu_pte_offset(pmdp, addr);
		}
	}
	set_pte(ptep, __pte((pte_val(*ptep) | SRMMU_CACHE)));
	flush_tlb_page_for_cbit(addr);
}

static unsigned long srmmu_getpage(void)
{
	unsigned long page = get_free_page(GFP_KERNEL);

	return page;
}

static inline void srmmu_putpage(unsigned long page)
{
	free_page(page);
}

#ifdef USE_CHUNK_ALLOC

#define LC_HIGH_WATER	128
#define BC_HIGH_WATER	32

static unsigned long *lcnks = 0;
static unsigned long *bcnks = 0;
static int lcwater = 0;
static int bcwater = 0;
static int chunk_pages = 0;
static int clct_pages = 0;

#define RELAX_JIFFIES	16

static int lcjiffies;
static int bcjiffies;

struct chunk {
	struct chunk *next;
	struct chunk *prev;
	struct chunk *npage;
	struct chunk *ppage;
	int count;
};

static int garbage_calls = 0;

#define OTHER_PAGE(p,q)	(((unsigned long)(p) ^ (unsigned long)(q)) & PAGE_MASK)

static inline int garbage_collect(unsigned long **cnks, int n, int cpp)
{
	struct chunk *root = (struct chunk *)*cnks;
	struct chunk *p, *q, *curr, *next;
	int water = n;

	next = root->next;
	curr = root->prev = root->next = root->npage = root->ppage = root;
	root->count = 1;

	garbage_calls++;

	while (--n) {
		p = next;
		next = next->next;

		if (OTHER_PAGE(p, curr)) {

			q = curr->npage;
			while (q != curr) {
				if (!OTHER_PAGE(p, q))
					break;
				q = q->npage;
			}

			if (q == curr) {

				(p->npage = curr->npage)->ppage = p;
				curr->npage = p;
				p->ppage = curr;

				p->next = p->prev = p;
				p->count = 1;

				curr = p;

				continue;
			}
			curr = q;
		}

		(p->next = curr->next)->prev = p;
		curr->next = p;
		p->prev = curr;

		if (++curr->count == cpp) {

			q = curr->npage;
			if (curr == q) {

				srmmu_putpage((unsigned long)curr & PAGE_MASK);
				water -= cpp;

				clct_pages++;
				chunk_pages--;

				if (--n) {
					p = next;
					next = next->next;

					curr = root->prev =
						root->next = root->npage =
						root->ppage = root = p;
					root->count = 1;

					continue;
				}
				return 0;
			}

			if (curr == root)
				root = q;

			curr->ppage->npage = q;
			q->ppage = curr->ppage;

			srmmu_putpage((unsigned long)curr & PAGE_MASK);
			water -= cpp;

			clct_pages++;
			chunk_pages--;

			curr = q;
		}
	}

	p = root;
	while (p->npage != root) {
		p->prev->next = p->npage;
		p = p->npage;
	}

	*cnks = (unsigned long *)root;
	return water;
}


static inline unsigned long *get_small_chunk(void)
{
	unsigned long *rval;
	unsigned long flags;

	save_and_cli(flags);
	if(lcwater) {
		lcwater--;
		rval = lcnks;
		lcnks = (unsigned long *) *rval;
	} else {
		rval = (unsigned long *) __get_free_page(GFP_KERNEL);

		if(!rval) {
			restore_flags(flags);
			return 0;
		}
		chunk_pages++;

		lcnks = (rval + 64);

		/* Cache stomping, I know... */
		*(rval + 64) = (unsigned long) (rval + 128);
		*(rval + 128) = (unsigned long) (rval + 192);
		*(rval + 192) = (unsigned long) (rval + 256);
		*(rval + 256) = (unsigned long) (rval + 320);
		*(rval + 320) = (unsigned long) (rval + 384);
		*(rval + 384) = (unsigned long) (rval + 448);
		*(rval + 448) = (unsigned long) (rval + 512);
		*(rval + 512) = (unsigned long) (rval + 576);
		*(rval + 576) = (unsigned long) (rval + 640);
		*(rval + 640) = (unsigned long) (rval + 704);
		*(rval + 704) = (unsigned long) (rval + 768);
		*(rval + 768) = (unsigned long) (rval + 832);
		*(rval + 832) = (unsigned long) (rval + 896);
		*(rval + 896) = (unsigned long) (rval + 960);
		*(rval + 960) = 0;
		lcwater = 15;
	}
	lcjiffies = jiffies;
	restore_flags(flags);
	memset(rval, 0, 256);
	return rval;
}

static inline void free_small_chunk(unsigned long *it)
{
	unsigned long flags;

	save_and_cli(flags);
	*it = (unsigned long) lcnks;
	lcnks = it;
	lcwater++;

	if ((lcwater > LC_HIGH_WATER) &&
	    (jiffies > lcjiffies + RELAX_JIFFIES))
		lcwater = garbage_collect(&lcnks, lcwater, 16);

	restore_flags(flags);
}

static inline unsigned long *get_big_chunk(void)
{
	unsigned long *rval;
	unsigned long flags;

	save_and_cli(flags);
	if(bcwater) {
		bcwater--;
		rval = bcnks;
		bcnks = (unsigned long *) *rval;
	} else {
		rval = (unsigned long *) __get_free_page(GFP_KERNEL);

		if(!rval) {
			restore_flags(flags);
			return 0;
		}
		chunk_pages++;

		bcnks = (rval + 256);

		/* Cache stomping, I know... */
		*(rval + 256) = (unsigned long) (rval + 512);
		*(rval + 512) = (unsigned long) (rval + 768);
		*(rval + 768) = 0;
		bcwater = 3;
	}
	bcjiffies = jiffies;
	restore_flags(flags);
	memset(rval, 0, 1024);
	return rval;
}

static inline void free_big_chunk(unsigned long *it)
{
	unsigned long flags;

	save_and_cli(flags);
	*it = (unsigned long) bcnks;
	bcnks = it;
	bcwater++;

	if ((bcwater > BC_HIGH_WATER) &&
	    (jiffies > bcjiffies + RELAX_JIFFIES))
		bcwater = garbage_collect(&bcnks, bcwater, 4);

	restore_flags(flags);
}

#define NEW_PGD() (pgd_t *) get_big_chunk()
#define NEW_PMD() (pmd_t *) get_small_chunk()
#define NEW_PTE() (pte_t *) get_small_chunk()
#define FREE_PGD(chunk) free_big_chunk((unsigned long *)(chunk))
#define FREE_PMD(chunk) free_small_chunk((unsigned long *)(chunk))
#define FREE_PTE(chunk) free_small_chunk((unsigned long *)(chunk))

#else

/* The easy versions. */
#define NEW_PGD() (pgd_t *) mmu_getpage()
#define NEW_PMD() (pmd_t *) mmu_getpage()
#define NEW_PTE() (pte_t *) mmu_getpage()
#define FREE_PGD(chunk) srmmu_putpage((unsigned long)(chunk))
#define FREE_PMD(chunk) srmmu_putpage((unsigned long)(chunk))
#define FREE_PTE(chunk) srmmu_putpage((unsigned long)(chunk))

#endif

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
static void srmmu_pte_free_kernel(pte_t *pte)
{
	FREE_PTE(pte);
}

static pte_t *srmmu_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = NEW_PTE();
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		FREE_PTE(page);
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) srmmu_pmd_page(*pmd) + address;
}

static void srmmu_pmd_free_kernel(pmd_t *pmd)
{
	FREE_PMD(pmd);
}

static pmd_t *srmmu_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page;
		page = NEW_PMD();
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		FREE_PMD(page);
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

static void srmmu_pte_free(pte_t *pte)
{
	FREE_PTE(pte);
}

static pte_t *srmmu_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = NEW_PTE();
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		FREE_PTE(page);
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return ((pte_t *) srmmu_pmd_page(*pmd)) + address;
}

/* Real three-level page tables on SRMMU. */
static void srmmu_pmd_free(pmd_t * pmd)
{
	FREE_PMD(pmd);
}

static pmd_t *srmmu_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page = NEW_PMD();
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		FREE_PMD(page);
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) srmmu_pgd_page(*pgd) + address;
}

static void srmmu_pgd_free(pgd_t *pgd)
{
	FREE_PGD(pgd);
}

static pgd_t *srmmu_pgd_alloc(void)
{
	return NEW_PGD();
}

static void srmmu_pgd_flush(pgd_t *pgdp)
{
}

static void srmmu_set_pte_cacheable(pte_t *ptep, pte_t pteval)
{
	srmmu_set_entry(ptep, pte_val(pteval));
}

static void srmmu_set_pte_nocache_hyper(pte_t *ptep, pte_t pteval)
{
	volatile unsigned long clear;
	unsigned long flags;

	save_and_cli(flags);
	srmmu_set_entry(ptep, pte_val(pteval));
	if(srmmu_hwprobe(((unsigned long)ptep)&PAGE_MASK))
		hyper_flush_cache_page(((unsigned long)ptep) & PAGE_MASK);
	clear = srmmu_get_fstatus();
	restore_flags(flags);
}

static void srmmu_set_pte_nocache_cypress(pte_t *ptep, pte_t pteval)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long line, page;

	srmmu_set_entry(ptep, pte_val(pteval));
	page = ((unsigned long)ptep) & PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

static void srmmu_set_pte_nocache_nomxccvik(pte_t *ptep, pte_t pteval)
{
	unsigned long vaddr;
	int set;
	int i;

	set = ((unsigned long)ptep >> 5) & 0x7f;
	vaddr = (KERNBASE + PAGE_SIZE) | (set << 5);
	srmmu_set_entry(ptep, pteval);
	for (i = 0; i < 8; i++) {
		__asm__ __volatile__ ("ld [%0], %%g0" : : "r" (vaddr));
		vaddr += PAGE_SIZE;
	}
}

static void srmmu_quick_kernel_fault(unsigned long address)
{
#ifdef __SMP__
	printk("CPU[%d]: Kernel faults at addr=0x%08lx\n",
	       smp_processor_id(), address);
	while (1) ;
#else
	extern void die_if_kernel(char *str, struct pt_regs *regs);

	printk("Kernel faults at addr=0x%08lx\n", address);
	printk("PTE=%08lx\n", srmmu_hwprobe((address & PAGE_MASK)));
	die_if_kernel("SRMMU bolixed...", current->tss.kregs);
#endif
}

static inline void alloc_context(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;
	struct ctx_list *ctxp;

#if CONFIG_AP1000
        if (tsk->taskid >= MPP_TASK_BASE) {
		mm->context = MPP_CONTEXT_BASE + (tsk->taskid - MPP_TASK_BASE);
		return;
	}
#endif

	ctxp = ctx_free.next;
	if(ctxp != &ctx_free) {
		remove_from_ctx_list(ctxp);
		add_to_used_ctxlist(ctxp);
		mm->context = ctxp->ctx_number;
		ctxp->ctx_mm = mm;
		return;
	}
	ctxp = ctx_used.next;
	if(ctxp->ctx_mm == current->mm)
		ctxp = ctxp->next;
	if(ctxp == &ctx_used)
		panic("out of mmu contexts");
	flush_cache_mm(ctxp->ctx_mm);
	flush_tlb_mm(ctxp->ctx_mm);
	remove_from_ctx_list(ctxp);
	add_to_used_ctxlist(ctxp);
	ctxp->ctx_mm->context = NO_CONTEXT;
	ctxp->ctx_mm = mm;
	mm->context = ctxp->ctx_number;
}

static inline void free_context(int context)
{
	struct ctx_list *ctx_old;

#if CONFIG_AP1000
	if (context >= MPP_CONTEXT_BASE)
		return; /* nothing to do! */
#endif
	
	ctx_old = ctx_list_pool + context;
	remove_from_ctx_list(ctx_old);
	add_to_free_ctxlist(ctx_old);
}


static void srmmu_switch_to_context(struct task_struct *tsk)
{
	if(tsk->mm->context == NO_CONTEXT) {
		alloc_context(tsk);
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], tsk->mm->pgd);
		flush_tlb_mm(current->mm);
	}
	srmmu_set_context(tsk->mm->context);
}

/* Low level IO area allocation on the SRMMU. */
void srmmu_mapioaddr(unsigned long physaddr, unsigned long virt_addr, int bus_type, int rdonly)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long tmp;

	physaddr &= PAGE_MASK;
	pgdp = srmmu_pgd_offset(init_task.mm, virt_addr);
	pmdp = srmmu_pmd_offset(pgdp, virt_addr);
	ptep = srmmu_pte_offset(pmdp, virt_addr);
	tmp = (physaddr >> 4) | SRMMU_ET_PTE;

	/* I need to test whether this is consistent over all
	 * sun4m's.  The bus_type represents the upper 4 bits of
	 * 36-bit physical address on the I/O space lines...
	 */
	tmp |= (bus_type << 28);
	if(rdonly)
		tmp |= SRMMU_PRIV_RDONLY;
	else
		tmp |= SRMMU_PRIV;
	flush_page_to_ram(virt_addr);
	set_pte(ptep, tmp);
	flush_tlb_all();
}

void srmmu_unmapioaddr(unsigned long virt_addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = srmmu_pgd_offset(init_task.mm, virt_addr);
	pmdp = srmmu_pmd_offset(pgdp, virt_addr);
	ptep = srmmu_pte_offset(pmdp, virt_addr);

	/* No need to flush uncacheable page. */
	set_pte(ptep, pte_val(srmmu_mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED)));
	flush_tlb_all();
}

static char *srmmu_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void srmmu_unlockarea(char *vaddr, unsigned long len)
{
}

/* On the SRMMU we do not have the problems with limited tlb entries
 * for mapping kernel pages, so we just take things from the free page
 * pool.  As a side effect we are putting a little too much pressure
 * on the gfp() subsystem.  This setup also makes the logic of the
 * iommu mapping code a lot easier as we can transparently handle
 * mappings on the kernel stack without any special code as we did
 * need on the sun4c.
 */
struct task_struct *srmmu_alloc_task_struct(void)
{
	return (struct task_struct *) kmalloc(sizeof(struct task_struct), GFP_KERNEL);
}

unsigned long srmmu_alloc_kernel_stack(struct task_struct *tsk)
{
	unsigned long kstk = __get_free_pages(GFP_KERNEL, 1, 0);

	if(!kstk)
		kstk = (unsigned long) vmalloc(PAGE_SIZE << 1);

	return kstk;
}

static void srmmu_free_task_struct(struct task_struct *tsk)
{
	kfree(tsk);
}

static void srmmu_free_kernel_stack(unsigned long stack)
{
	if(stack < VMALLOC_START)
		free_pages(stack, 1);
	else
		vfree((char *)stack);
}

/* Tsunami flushes.  It's page level tlb invalidation is not very
 * useful at all, you must be in the context that page exists in to
 * get a match.
 */
static void tsunami_flush_cache_all(void)
{
	flush_user_windows();
	tsunami_flush_icache();
	tsunami_flush_dcache();
}

static void tsunami_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		tsunami_flush_icache();
		tsunami_flush_dcache();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		tsunami_flush_icache();
		tsunami_flush_dcache();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		tsunami_flush_icache();
		tsunami_flush_dcache();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_cache_page_to_uncache(unsigned long page)
{
	tsunami_flush_dcache();
}

/* Tsunami does not have a Copy-back style virtual cache. */
static void tsunami_flush_page_to_ram(unsigned long page)
{
}

/* However, Tsunami is not IO coherent. */
static void tsunami_flush_page_for_dma(unsigned long page)
{
	tsunami_flush_icache();
	tsunami_flush_dcache();
}

/* Tsunami has harvard style split I/D caches which do not snoop each other,
 * so we have to flush on-stack sig insns.  Only the icache need be flushed
 * since the Tsunami has a write-through data cache.
 */
static void tsunami_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	tsunami_flush_icache();
}

static void tsunami_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void tsunami_flush_tlb_mm(struct mm_struct *mm)
{
	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		srmmu_flush_whole_tlb();
#ifndef __SMP__
        }
#endif
}

static void tsunami_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		srmmu_flush_whole_tlb();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		unsigned long flags;

		save_and_cli(flags);
		octx = srmmu_get_context();

		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
	module_stats.invpg++;
}

static void tsunami_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

/* Swift flushes.  It has the recommended SRMMU specification flushing
 * facilities, so we can do things in a more fine grained fashion than we
 * could on the tsunami.  Let's watch out for HARDWARE BUGS...
 */

static void swift_flush_cache_all(void)
{
	flush_user_windows();
	swift_idflash_clear();
}

static void swift_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		swift_idflash_clear();
#ifndef __SMP__
	}
#endif
}

static void swift_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		swift_idflash_clear();
#ifndef __SMP__
	}
#endif
}

static void swift_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		if(vma->vm_flags & VM_EXEC)
			swift_flush_icache();
		swift_flush_dcache();
#ifndef __SMP__
	}
#endif
}

/* Not copy-back on swift. */
static void swift_flush_page_to_ram(unsigned long page)
{
}

/* But not IO coherent either. */
static void swift_flush_page_for_dma(unsigned long page)
{
	swift_flush_dcache();
}

/* Again, Swift is non-snooping split I/D cache'd just like tsunami,
 * so have to punt the icache for on-stack signal insns.  Only the
 * icache need be flushed since the dcache is write-through.
 */
static void swift_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	swift_flush_icache();
}

static void swift_flush_cache_page_to_uncache(unsigned long page)
{
	swift_flush_dcache();
}

static void swift_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void swift_flush_tlb_mm(struct mm_struct *mm)
{
	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT)
#endif
		srmmu_flush_whole_tlb();
}

static void swift_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT)
#endif
		srmmu_flush_whole_tlb();
}

static void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT)
#endif
		srmmu_flush_whole_tlb();
	module_stats.invpg++;
}

static void swift_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_whole_tlb();
}

/* The following are all MBUS based SRMMU modules, and therefore could
 * be found in a multiprocessor configuration.  On the whole, these
 * chips seems to be much more touchy about DVMA and page tables
 * with respect to cache coherency.
 */

/* Viking flushes.  For Sun's mainline MBUS processor it is pretty much
 * a crappy mmu.  The on-chip I&D caches only have full flushes, no fine
 * grained cache invalidations.  It only has these "flash clear" things
 * just like the MicroSparcI.  Added to this many revs of the chip are
 * teaming with hardware buggery.  Someday maybe we'll do direct
 * diagnostic tag accesses for page level flushes as those should
 * be painless and will increase performance due to the frequency of
 * page level flushes. This is a must to _really_ flush the caches,
 * crazy hardware ;-)
 */

static void viking_flush_cache_all(void)
{
}

static void viking_flush_cache_mm(struct mm_struct *mm)
{
}

static void viking_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
}

static void viking_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
}

/* Non-mxcc vikings are copy-back but are pure-physical so no flushing. */
static void viking_flush_page_to_ram(unsigned long page)
{
}

/* All vikings have an icache which snoops the processor bus and is fully
 * coherent with the dcache, so no flush is necessary at all.
 */
static void viking_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

static void viking_mxcc_flush_page(unsigned long page)
{
	unsigned long ppage = srmmu_v2p(page & PAGE_MASK);
	unsigned long paddr0, paddr1;

	if (ppage == 0xffffffffUL)
		return;

	paddr0 = 0x10;			/* Set cacheable bit. */
	paddr1 = ppage;

	/* Read the page's data through the stream registers,
	 * and write it back to memory. This will issue
	 * coherent write invalidates to all other caches, thus
         * should also be sufficient in an MP system.
	 */
	__asm__ __volatile__ ("or %%g0, %0, %%g2\n\t"
			      "or %%g0, %1, %%g3\n"
			      "1:\n\t"
			      "stda %%g2, [%2] %5\n\t"
			      "stda %%g2, [%3] %5\n\t"
			      "add %%g3, %4, %%g3\n\t"
			      "btst 0xfff, %%g3\n\t"
			      "bne 1b\n\t"
			      "nop\n\t" : :
			      "r" (paddr0), "r" (paddr1),
			      "r" (MXCC_SRCSTREAM),
			      "r" (MXCC_DESSTREAM),
			      "r" (MXCC_STREAM_SIZE),
			      "i" (ASI_M_MXCC) : "g2", "g3");

	/* This was handcoded after a look at the gcc output from
	 *
	 *	do {
	 *		mxcc_set_stream_src(paddr);
	 *		mxcc_set_stream_dst(paddr);
	 *		paddr[1] += MXCC_STREAM_SIZE;
	 *	} while (paddr[1] & ~PAGE_MASK);
	 */
}

static void viking_no_mxcc_flush_page(unsigned long page)
{
	unsigned long ppage = srmmu_v2p(page & PAGE_MASK);
	int set, block;
	unsigned long ptag[2];
	unsigned long vaddr;
	int i;

	if (ppage == 0xffffffffUL)
		return;
	ppage >>= 12;

	for (set = 0; set < 128; set++) {
		for (block = 0; block < 4; block++) {

			viking_get_dcache_ptag(set, block, ptag);

			if (ptag[1] != ppage)
				continue;
			if (!(ptag[0] & VIKING_PTAG_VALID))
				continue;
			if (!(ptag[0] & VIKING_PTAG_DIRTY))
				continue;

			/* There was a great cache from TI
			 * with comfort as much as vi,
			 * 4 pages to flush,
			 * 4 pages, no rush,
			 * since anything else makes him die.
			 */
			vaddr = (KERNBASE + PAGE_SIZE) | (set << 5);
			for (i = 0; i < 8; i++) {
				__asm__ __volatile__ ("ld [%0], %%g2\n\t" : :
						      "r" (vaddr) : "g2");
				vaddr += PAGE_SIZE;
			}

			/* Continue with next set. */
			break;
		}
	}
}

/* Viking is IO cache coherent, but really only on MXCC. */
static void viking_flush_page_for_dma(unsigned long page)
{
}

static unsigned long viking_no_mxcc_getpage(void)
{
	unsigned long page = get_free_page(GFP_KERNEL);

	viking_no_mxcc_flush_page(page);
	return page;
}

static void viking_no_mxcc_pgd_flush(pgd_t *pgdp)
{
	viking_no_mxcc_flush_page((unsigned long)pgdp);
}

static void viking_flush_tlb_all(void)
{
	module_stats.invall++;
	flush_user_windows();
	srmmu_flush_whole_tlb();
}

static void viking_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;
	module_stats.invmm++;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_ctx();
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void viking_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;
	module_stats.invrnge++;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		if((end - start) < SRMMU_PMD_SIZE) {
			start &= PAGE_MASK;
			while(start < end) {
				srmmu_flush_tlb_page(start);
				start += PAGE_SIZE;
			}
		} else if((end - start) < SRMMU_PGDIR_SIZE) {
			start &= SRMMU_PMD_MASK;
			while(start < end) {
				srmmu_flush_tlb_segment(start);
				start += SRMMU_PMD_SIZE;
			}
		} else {
			start &= SRMMU_PGDIR_MASK;
			while(start < end) {
				srmmu_flush_tlb_region(start);
				start += SRMMU_PGDIR_SIZE;
			}
		}
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void viking_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

	module_stats.invpg++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void viking_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

/* Cypress flushes. */
static void cypress_flush_cache_all(void)
{
	volatile unsigned long cypress_sucks;
	unsigned long faddr, tagval;

	flush_user_windows();
	for(faddr = 0; faddr < 0x10000; faddr += 0x20) {
		__asm__ __volatile__("lda [%1 + %2] %3, %0\n\t" :
				     "=r" (tagval) :
				     "r" (faddr), "r" (0x40000),
				     "i" (ASI_M_DATAC_TAG));

		/* If modified and valid, kick it. */
		if((tagval & 0x60) == 0x60)
			cypress_sucks = *(unsigned long *)(0xf0020000 + faddr);
	}
}

static void cypress_flush_cache_mm(struct mm_struct *mm)
{
	unsigned long flags, faddr;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		register unsigned long a, b, c, d, e, f, g;
		flush_user_windows();
		save_and_cli(flags);
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
		faddr = (0x10000 - 0x100);
		goto inside;
		do {
			faddr -= 0x100;
		inside:
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
					     "sta %%g0, [%0 + %2] %1\n\t"
					     "sta %%g0, [%0 + %3] %1\n\t"
					     "sta %%g0, [%0 + %4] %1\n\t"
					     "sta %%g0, [%0 + %5] %1\n\t"
					     "sta %%g0, [%0 + %6] %1\n\t"
					     "sta %%g0, [%0 + %7] %1\n\t"
					     "sta %%g0, [%0 + %8] %1\n\t" : :
					     "r" (faddr), "i" (ASI_M_FLUSH_CTX),
					     "r" (a), "r" (b), "r" (c), "r" (d),
					     "r" (e), "r" (f), "r" (g));
		} while(faddr);
		srmmu_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long flags, faddr;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		register unsigned long a, b, c, d, e, f, g;
		flush_user_windows();
		save_and_cli(flags);
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
		start &= SRMMU_PMD_MASK;
		while(start < end) {
			faddr = (start + (0x10000 - 0x100));
			goto inside;
			do {
				faddr -= 0x100;
			inside:
				__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
						     "sta %%g0, [%0 + %2] %1\n\t"
						     "sta %%g0, [%0 + %3] %1\n\t"
						     "sta %%g0, [%0 + %4] %1\n\t"
						     "sta %%g0, [%0 + %5] %1\n\t"
						     "sta %%g0, [%0 + %6] %1\n\t"
						     "sta %%g0, [%0 + %7] %1\n\t"
						     "sta %%g0, [%0 + %8] %1\n\t" : :
						     "r" (faddr),
						     "i" (ASI_M_FLUSH_SEG),
						     "r" (a), "r" (b), "r" (c), "r" (d),
						     "r" (e), "r" (f), "r" (g));
			} while (faddr != start);
			start += SRMMU_PMD_SIZE;
		}
		srmmu_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags, line;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		register unsigned long a, b, c, d, e, f, g;
		flush_user_windows();
		save_and_cli(flags);
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
		page &= PAGE_MASK;
		line = (page + PAGE_SIZE) - 0x100;
		goto inside;
		do {
			line -= 0x100;
		inside:
				__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
						     "sta %%g0, [%0 + %2] %1\n\t"
						     "sta %%g0, [%0 + %3] %1\n\t"
						     "sta %%g0, [%0 + %4] %1\n\t"
						     "sta %%g0, [%0 + %5] %1\n\t"
						     "sta %%g0, [%0 + %6] %1\n\t"
						     "sta %%g0, [%0 + %7] %1\n\t"
						     "sta %%g0, [%0 + %8] %1\n\t" : :
						     "r" (line),
						     "i" (ASI_M_FLUSH_PAGE),
						     "r" (a), "r" (b), "r" (c), "r" (d),
						     "r" (e), "r" (f), "r" (g));
		} while(line != page);
		srmmu_set_context(octx);
		restore_flags(flags);
#ifndef __SMP__
	}
#endif
}

/* Cypress is copy-back, at least that is how we configure it. */
static void cypress_flush_page_to_ram(unsigned long page)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long line;

	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

/* Cypress is also IO cache coherent. */
static void cypress_flush_page_for_dma(unsigned long page)
{
}

/* Cypress has unified L2 VIPT, from which both instructions and data
 * are stored.  It does not have an onboard icache of any sort, therefore
 * no flush is necessary.
 */
static void cypress_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

static void cypress_flush_page_to_uncache(unsigned long page)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long line;

	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

static unsigned long cypress_getpage(void)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long page = get_free_page(GFP_KERNEL);
	unsigned long line;

	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
	return page;
}

static void cypress_pgd_flush(pgd_t *pgdp)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long page = ((unsigned long) pgdp) & PAGE_MASK;
	unsigned long line;

	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

static void cypress_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void cypress_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;

	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_ctx();
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;
	module_stats.invrnge++;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		if((end - start) < SRMMU_PMD_SIZE) {
			start &= PAGE_MASK;
			while(start < end) {
				srmmu_flush_tlb_page(start);
				start += PAGE_SIZE;
			}
		} else if((end - start) < SRMMU_PGDIR_SIZE) {
			start &= SRMMU_PMD_MASK;
			while(start < end) {
				srmmu_flush_tlb_segment(start);
				start += SRMMU_PMD_SIZE;
			}
		} else {
			start &= SRMMU_PGDIR_MASK;
			while(start < end) {
				srmmu_flush_tlb_region(start);
				start += SRMMU_PGDIR_SIZE;
			}
		}
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

	module_stats.invpg++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

/* Hypersparc flushes.  Very nice chip... */
static void hypersparc_flush_cache_all(void)
{
	flush_user_windows();
	hyper_flush_unconditional_combined();
	hyper_flush_whole_icache();
}

static void hypersparc_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		hyper_flush_cache_user();
		hyper_flush_whole_icache();
#ifndef __SMP__
	}
#endif
}

/* Boy was my older implementation inefficient... */
static void hypersparc_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	volatile unsigned long clear;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		octx = srmmu_get_context();
		start &= PAGE_MASK;
		srmmu_set_context(mm->context);
		while(start < end) {
			if(srmmu_hwprobe(start))
				hyper_flush_cache_page(start);
			start += PAGE_SIZE;
		}
		clear = srmmu_get_fstatus();
		srmmu_set_context(octx);
		hyper_flush_whole_icache();
#ifndef __SMP__
	}
#endif
}

/* HyperSparc requires a valid mapping where we are about to flush
 * in order to check for a physical tag match during the flush.
 */
static void hypersparc_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	volatile unsigned long clear;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		flush_user_windows();
		srmmu_set_context(mm->context);
		hyper_flush_whole_icache();
		if(!srmmu_hwprobe(page))
			goto no_mapping;
		hyper_flush_cache_page(page);
	no_mapping:
		clear = srmmu_get_fstatus();
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

/* HyperSparc is copy-back. */
static void hypersparc_flush_page_to_ram(unsigned long page)
{
	volatile unsigned long clear;

	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
}

/* HyperSparc is IO cache coherent. */
static void hypersparc_flush_page_for_dma(unsigned long page)
{
}

/* HyperSparc has unified I/D L2 cache, however it posseses a small on-chip
 * ICACHE which must be flushed for the new style signals.
 */
static void hypersparc_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	hyper_flush_whole_icache();
}

static void hypersparc_flush_cache_page_to_uncache(unsigned long page)
{
	volatile unsigned long clear;

	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
}

static unsigned long hypersparc_getpage(void)
{
	volatile unsigned long clear;
	unsigned long page = get_free_page(GFP_KERNEL);
	unsigned long flags;

	save_and_cli(flags);
	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
	restore_flags(flags);

	return page;
}

static void hypersparc_pgd_flush(pgd_t *pgdp)
{
	volatile unsigned long clear;
	unsigned long page = ((unsigned long) pgdp) & PAGE_MASK;
	unsigned long flags;

	save_and_cli(flags);
	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
	restore_flags(flags);
}

static void hypersparc_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void hypersparc_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;

	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif

		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_ctx();
		srmmu_set_context(octx);

#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;

	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif

		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		if((end - start) < SRMMU_PMD_SIZE) {
			start &= PAGE_MASK;
			while(start < end) {
				srmmu_flush_tlb_page(start);
				start += PAGE_SIZE;
			}
		} else if((end - start) < SRMMU_PGDIR_SIZE) {
			start &= SRMMU_PMD_MASK;
			while(start < end) {
				srmmu_flush_tlb_segment(start);
				start += SRMMU_PMD_SIZE;
			}
		} else {
			start &= SRMMU_PGDIR_MASK;
			while(start < end) {
				srmmu_flush_tlb_region(start);
				start += SRMMU_PGDIR_SIZE;
			}
		}
		srmmu_set_context(octx);

#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	int octx;

	module_stats.invpg++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif

		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);

#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

static void hypersparc_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{
	hyper_flush_whole_icache();
	set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pgdp) >> 4)));
}

static void hypersparc_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(current->mm);
	}
}

static void hypersparc_switch_to_context(struct task_struct *tsk)
{
	hyper_flush_whole_icache();
	if(tsk->mm->context == NO_CONTEXT) {
		alloc_context(tsk);
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], tsk->mm->pgd);
		flush_tlb_mm(current->mm);
	}
	srmmu_set_context(tsk->mm->context);
}

/* IOMMU things go here. */

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

#define IOPERM        (IOPTE_CACHE | IOPTE_WRITE | IOPTE_VALID)
#define MKIOPTE(phys) (((((phys)>>4) & IOPTE_PAGE) | IOPERM) & ~IOPTE_WAZ)

static inline void srmmu_map_dvma_pages_for_iommu(struct iommu_struct *iommu,
						  unsigned long kern_end)
{
	unsigned long first = page_offset;
	unsigned long last = kern_end;
	iopte_t *iopte = iommu->page_table;

	iopte += ((first - iommu->start) >> PAGE_SHIFT);
	while(first <= last) {
		iopte_val(*iopte++) = MKIOPTE(srmmu_v2p(first));
		first += PAGE_SIZE;
	}
}

unsigned long iommu_init(int iommund, unsigned long memory_start,
			 unsigned long memory_end, struct linux_sbus *sbus)
{
	unsigned int impl, vers, ptsize;
	unsigned long tmp;
	struct iommu_struct *iommu;
	struct linux_prom_registers iommu_promregs[PROMREG_MAX];

	memory_start = LONG_ALIGN(memory_start);
	iommu = (struct iommu_struct *) memory_start;
	memory_start += sizeof(struct iommu_struct);
	prom_getproperty(iommund, "reg", (void *) iommu_promregs,
			 sizeof(iommu_promregs));
	iommu->regs = (struct iommu_regs *)
		sparc_alloc_io(iommu_promregs[0].phys_addr, 0, (PAGE_SIZE * 3),
			       "IOMMU registers", iommu_promregs[0].which_io, 0x0);
	if(!iommu->regs)
		panic("Cannot map IOMMU registers.");
	impl = (iommu->regs->control & IOMMU_CTRL_IMPL) >> 28;
	vers = (iommu->regs->control & IOMMU_CTRL_VERS) >> 24;
	tmp = iommu->regs->control;
	tmp &= ~(IOMMU_CTRL_RNGE);
	switch(page_offset & 0xf0000000) {
	case 0xf0000000:
		tmp |= (IOMMU_RNGE_256MB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0xf0000000;
		break;
	case 0xe0000000:
		tmp |= (IOMMU_RNGE_512MB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0xe0000000;
		break;
	case 0xd0000000:
	case 0xc0000000:
		tmp |= (IOMMU_RNGE_1GB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0xc0000000;
		break;
	case 0xb0000000:
	case 0xa0000000:
	case 0x90000000:
	case 0x80000000:
		tmp |= (IOMMU_RNGE_2GB | IOMMU_CTRL_ENAB);
		iommu->plow = iommu->start = 0x80000000;
		break;
	}
	iommu->regs->control = tmp;
	iommu_invalidate(iommu->regs);
	iommu->end = 0xffffffff;

	/* Allocate IOMMU page table */
	ptsize = iommu->end - iommu->start + 1;
	ptsize = (ptsize >> PAGE_SHIFT) * sizeof(iopte_t);

	/* Stupid alignment constraints give me a headache. */
	memory_start = PAGE_ALIGN(memory_start);
	memory_start = (((memory_start) + (ptsize - 1)) & ~(ptsize - 1));
	iommu->lowest = iommu->page_table = (iopte_t *) memory_start;
	memory_start += ptsize;

	/* Initialize new table. */
	flush_cache_all();
	memset(iommu->page_table, 0, ptsize);
	srmmu_map_dvma_pages_for_iommu(iommu, memory_end);
	if(viking_mxcc_present) {
		unsigned long start = (unsigned long) iommu->page_table;
		unsigned long end = (start + ptsize);
		while(start < end) {
			viking_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	} else if(flush_page_for_dma == viking_no_mxcc_flush_page) {
		unsigned long start = (unsigned long) iommu->page_table;
		unsigned long end = (start + ptsize);
		while(start < end) {
			viking_no_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	flush_tlb_all();
	iommu->regs->base = srmmu_v2p((unsigned long) iommu->page_table) >> 4;
	iommu_invalidate(iommu->regs);

	sbus->iommu = iommu;
	printk("IOMMU: impl %d vers %d page table at %p of size %d bytes\n",
	       impl, vers, iommu->page_table, ptsize);
	return memory_start;
}

void iommu_sun4d_init(int sbi_node, struct linux_sbus *sbus)
{
	u32 *iommu;
	struct linux_prom_registers iommu_promregs[PROMREG_MAX];

	prom_getproperty(sbi_node, "reg", (void *) iommu_promregs,
			 sizeof(iommu_promregs));
	iommu = (u32 *)
		sparc_alloc_io(iommu_promregs[2].phys_addr, 0, (PAGE_SIZE * 16),
			       "XPT", iommu_promregs[2].which_io, 0x0);
	if(!iommu)
		panic("Cannot map External Page Table.");

	/* Initialize new table. */
	flush_cache_all();
	memset(iommu, 0, 16 * PAGE_SIZE);
	if(viking_mxcc_present) {
		unsigned long start = (unsigned long) iommu;
		unsigned long end = (start + 16 * PAGE_SIZE);
		while(start < end) {
			viking_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	} else if(flush_page_for_dma == viking_no_mxcc_flush_page) {
		unsigned long start = (unsigned long) iommu;
		unsigned long end = (start + 16 * PAGE_SIZE);
		while(start < end) {
			viking_no_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	flush_tlb_all();

	sbus->iommu = (struct iommu_struct *)iommu;
}

static char *srmmu_get_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	unsigned long page = ((unsigned long) vaddr) & PAGE_MASK;

	while(page < ((unsigned long)(vaddr + len))) {
		flush_page_for_dma(page);
		page += PAGE_SIZE;
	}
	return vaddr;
}

static void srmmu_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
        unsigned long page;

	while(sz >= 0) {
		page = ((unsigned long) sg[sz].addr) & PAGE_MASK;
		while(page < (unsigned long)(sg[sz].addr + sg[sz].len)) {
			flush_page_for_dma(page);
			page += PAGE_SIZE;
		}
		sg[sz].dvma_addr = (char *) (sg[sz].addr);
		sz--;
	}
}

static void srmmu_release_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
}

static void srmmu_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
}

static unsigned long mempool;

/* NOTE: All of this startup code assumes the low 16mb (approx.) of
 *       kernel mappings are done with one single contiguous chunk of
 *       ram.  On small ram machines (classics mainly) we only get
 *       around 8mb mapped for us.
 */

static unsigned long kbpage;

/* Some dirty hacks to abstract away the painful boot up init. */
static inline unsigned long srmmu_early_paddr(unsigned long vaddr)
{
	return ((vaddr - KERNBASE) + kbpage);
}

static inline void srmmu_early_pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{
	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (srmmu_early_paddr((unsigned long) pmdp) >> 4)));
}

static inline void srmmu_early_pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	set_pte((pte_t *)pmdp, (SRMMU_ET_PTD | (srmmu_early_paddr((unsigned long) ptep) >> 4)));
}

static inline unsigned long srmmu_early_pgd_page(pgd_t pgd)
{
	return (((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4) - kbpage) + KERNBASE;
}

static inline unsigned long srmmu_early_pmd_page(pmd_t pmd)
{
	return (((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4) - kbpage) + KERNBASE;
}

static inline pmd_t *srmmu_early_pmd_offset(pgd_t *dir, unsigned long address)
{
	return (pmd_t *) srmmu_early_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

static inline pte_t *srmmu_early_pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *) srmmu_early_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

static inline void srmmu_allocate_ptable_skeleton(unsigned long start, unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = sparc_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_early_pmd_set(pmdp, ptep);
		}
		start = (start + SRMMU_PMD_SIZE) & SRMMU_PMD_MASK;
	}
}

/* This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
void srmmu_inherit_prom_mappings(unsigned long start,unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int what = 0; /* 0 = normal-pte, 1 = pmd-level pte, 2 = pgd-level pte */
	unsigned long prompte;

	while(start <= end) {
		if (start == 0)
			break; /* probably wrap around */
		if(start == 0xfef00000)
			start = KADB_DEBUGGER_BEGVM;
		if(!(prompte = srmmu_hwprobe(start))) {
			start += PAGE_SIZE;
			continue;
		}
    
		/* A red snapper, see what it really is. */
		what = 0;
    
		if(!(start & ~(SRMMU_PMD_MASK))) {
			if(srmmu_hwprobe((start-PAGE_SIZE) + SRMMU_PMD_SIZE) == prompte)
				what = 1;
		}
    
		if(!(start & ~(SRMMU_PGDIR_MASK))) {
			if(srmmu_hwprobe((start-PAGE_SIZE) + SRMMU_PGDIR_SIZE) ==
			   prompte)
				what = 2;
		}
    
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		if(what == 2) {
			pgd_val(*pgdp) = prompte;
			start += SRMMU_PGDIR_SIZE;
			continue;
		}
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = sparc_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		if(what == 1) {
			pmd_val(*pmdp) = prompte;
			start += SRMMU_PMD_SIZE;
			continue;
		}
		if(srmmu_pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_early_pmd_set(pmdp, ptep);
		}
		ptep = srmmu_early_pte_offset(pmdp, start);
		pte_val(*ptep) = prompte;
		start += PAGE_SIZE;
	}
}

static void srmmu_map_dma_area(unsigned long addr, int len)
{
	unsigned long page, end;
	pgprot_t dvma_prot;
	struct iommu_struct *iommu = SBus_chain->iommu;
	iopte_t *iopte = iommu->page_table;
	iopte_t *iopte_first = iopte;

	if(viking_mxcc_present)
		dvma_prot = __pgprot(SRMMU_CACHE | SRMMU_ET_PTE | SRMMU_PRIV);
	else
		dvma_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV);

	iopte += ((addr - iommu->start) >> PAGE_SHIFT);
	end = PAGE_ALIGN((addr + len));
	while(addr < end) {
		page = get_free_page(GFP_KERNEL);
		if(!page) {
			prom_printf("alloc_dvma: Cannot get a dvma page\n");
			prom_halt();
		} else {
			pgd_t *pgdp;
			pmd_t *pmdp;
			pte_t *ptep;

			pgdp = srmmu_pgd_offset(init_task.mm, addr);
			pmdp = srmmu_pmd_offset(pgdp, addr);
			ptep = srmmu_pte_offset(pmdp, addr);

			set_pte(ptep, pte_val(srmmu_mk_pte(page, dvma_prot)));

			iopte_val(*iopte++) = MKIOPTE(srmmu_v2p(page));
		}
		addr += PAGE_SIZE;
	}
	flush_cache_all();
	if(viking_mxcc_present) {
		unsigned long start = ((unsigned long) iopte_first) & PAGE_MASK;
		unsigned long end = PAGE_ALIGN(((unsigned long) iopte));
		while(start < end) {
			viking_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	} else if(flush_page_for_dma == viking_no_mxcc_flush_page) {
		unsigned long start = ((unsigned long) iopte_first) & PAGE_MASK;
		unsigned long end = PAGE_ALIGN(((unsigned long) iopte));
		while(start < end) {
			viking_no_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	flush_tlb_all();
	iommu_invalidate(iommu->regs);
}

/* #define DEBUG_MAP_KERNEL */

#ifdef DEBUG_MAP_KERNEL
#define MKTRACE(foo) prom_printf foo
#else
#define MKTRACE(foo)
#endif

static int lots_of_ram = 0;
static int large_pte_optimize = 1;

#define KERNEL_PTE(page_shifted) ((page_shifted)|SRMMU_CACHE|SRMMU_PRIV|SRMMU_VALID)

/* Create a third-level SRMMU 16MB page mapping. */
static inline void do_large_mapping(unsigned long vaddr, unsigned long phys_base)
{
	pgd_t *pgdp = srmmu_pgd_offset(init_task.mm, vaddr);
	unsigned long big_pte;

	MKTRACE(("dlm[v<%08lx>-->p<%08lx>]", vaddr, phys_base));
	big_pte = KERNEL_PTE(phys_base >> 4);
	pgd_val(*pgdp) = big_pte;
}

/* Create second-level SRMMU 256K medium sized page mappings. */
static inline void do_medium_mapping(unsigned long vaddr, unsigned long vend,
				     unsigned long phys_base)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	unsigned long medium_pte;

	MKTRACE(("dmm[v<%08lx,%08lx>-->p<%08lx>]", vaddr, vend, phys_base));
	while(vaddr < vend) {
		pgdp = srmmu_pgd_offset(init_task.mm, vaddr);
		pmdp = srmmu_early_pmd_offset(pgdp, vaddr);
		medium_pte = KERNEL_PTE(phys_base >> 4);
		pmd_val(*pmdp) = medium_pte;
		phys_base += SRMMU_PMD_SIZE;
		vaddr += SRMMU_PMD_SIZE;
	}
}

/* Create a normal set of SRMMU page mappings for the virtual range
 * START to END, using physical pages beginning at PHYS_BASE.
 */
static inline void do_small_mapping(unsigned long start, unsigned long end,
				     unsigned long phys_base)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	MKTRACE(("dsm[v<%08lx,%08lx>-->p<%08lx>]", start, end, phys_base));
	while(start < end) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		ptep = srmmu_early_pte_offset(pmdp, start);

		pte_val(*ptep) = KERNEL_PTE(phys_base >> 4);
		phys_base += PAGE_SIZE;
		start += PAGE_SIZE;
	}
}

/* Look in the sp_bank for the given physical page, return the
 * index number the entry was found in, or -1 for not found.
 */
static inline int find_in_spbanks(unsigned long phys_page)
{
	int entry;

	for(entry = 0; sp_banks[entry].num_bytes; entry++) {
		unsigned long start = sp_banks[entry].base_addr;
		unsigned long end = start + sp_banks[entry].num_bytes;

		if((start <= phys_page) && (phys_page < end))
			return entry;
	}
	return -1;
}

/* Find an spbank entry not mapped as of yet, TAKEN_VECTOR is an
 * array of char's, each member indicating if that spbank is mapped
 * yet or not.
 */
static inline int find_free_spbank(char *taken_vector)
{
	int entry;

	for(entry = 0; sp_banks[entry].num_bytes; entry++)
		if(!taken_vector[entry])
			break;
	return entry;
}

/* Same as above, but with a given bank size limit BLIMIT. */
static inline int find_free_spbank_limited(char *taken_vector, unsigned long limit)
{
	int entry;

	for(entry = 0; sp_banks[entry].num_bytes; entry++)
		if(!taken_vector[entry] &&
		   (sp_banks[entry].num_bytes < limit))
			break;
	return entry;
}

/* Map sp_bank entry SP_ENTRY, starting at virtual address VBASE.
 * This routine is expected to update the srmmu_map and try as
 * hard as possible to use 16MB level-one SRMMU pte's when at all
 * possible to get short termination and faster translations.
 */
static inline unsigned long map_spbank(unsigned long vbase, int sp_entry)
{
	unsigned long pstart = sp_banks[sp_entry].base_addr;
	unsigned long vstart = vbase;
	unsigned long vend = vbase + sp_banks[sp_entry].num_bytes;
	static int srmmu_bank = 0;

	/* If physically not aligned on 16MB boundry, just shortcut
	 * right here by mapping them with 4k normal pages, and bumping
	 * the next virtual address to the next 16MB boundry.  You can
	 * get this with various RAM configurations due to the way in
	 * which the PROM carves out it's own chunks of memory.
	 */
	if(pstart & ~SRMMU_PGDIR_MASK) {
		do_small_mapping(vstart, vend, pstart);
		vstart = SRMMU_PGDIR_ALIGN(vend);
		goto finish_up;
	}
	while(vstart < vend) {
		unsigned long coverage, next_aligned;
		if(vstart & ~SRMMU_PMD_MASK) {
			next_aligned = SRMMU_PMD_ALIGN(vstart);
			if(next_aligned <= vend) {
				coverage = (next_aligned - vstart);
				do_small_mapping(vstart, next_aligned, pstart);
			} else {
				coverage = (vend - vstart);
				do_small_mapping(vstart, vend, pstart);
			}
		} else if(vstart & ~SRMMU_PGDIR_MASK) {
			next_aligned = SRMMU_PGDIR_ALIGN(vstart);
			if(next_aligned <= vend) {
				coverage = (next_aligned - vstart);
				do_medium_mapping(vstart, next_aligned, pstart);
			} else {
				coverage = (vend - vstart);
				do_small_mapping(vstart, vend, pstart);
			}
		} else {
			coverage = SRMMU_PGDIR_SIZE;
			if(large_pte_optimize || ((vstart+coverage)<=vend)) {
				do_large_mapping(vstart, pstart);
			} else {
				coverage = (vend - vstart);
				do_small_mapping(vstart, vend, pstart);
			}
		}
		vstart += coverage; pstart += coverage;
	}
finish_up:
	srmmu_map[srmmu_bank].vbase = vbase;
	srmmu_map[srmmu_bank].pbase = sp_banks[sp_entry].base_addr;
	srmmu_map[srmmu_bank].size = sp_banks[sp_entry].num_bytes;
	MKTRACE(("SRMMUBANK[v<%08lx>p<%08lx>s<%08lx>]", vbase, sp_banks[sp_entry].base_addr, sp_banks[sp_entry].num_bytes));
	srmmu_bank++;
	return vstart;
}

static inline void memprobe_error(char *msg)
{
	prom_printf(msg);
	prom_printf("Halting now...\n");
	prom_halt();
}

/* Assumptions: The bank given to the kernel from the prom/bootloader
 * is part of a full bank which is at least 4MB in size and begins at
 * 0xf0000000 (ie. KERNBASE).
 */
static void map_kernel(void)
{
	unsigned long raw_pte, physpage;
	unsigned long vaddr, tally, low_base;
	char etaken[SPARC_PHYS_BANKS];
	int entry;

	/* Step 1: Clear out sp_banks taken map. */
	MKTRACE(("map_kernel: clearing etaken vector... "));
	for(entry = 0; entry < SPARC_PHYS_BANKS; entry++)
		etaken[entry] = 0;

	low_base = KERNBASE;

	/* Step 2: Calculate 'lots_of_ram'. */
	tally = 0;
	for(entry = 0; sp_banks[entry].num_bytes; entry++)
		tally += sp_banks[entry].num_bytes;
	if(tally >= (0xfd000000 - KERNBASE))
		lots_of_ram = 1;
	else
		lots_of_ram = 0;
	MKTRACE(("tally=%08lx lots_of_ram<%d>\n", tally, lots_of_ram));

	/* Step 3: Fill in KERNBASE base pgd.  Lots of sanity checking here. */
	raw_pte = srmmu_hwprobe(KERNBASE + PAGE_SIZE);
	if((raw_pte & SRMMU_ET_MASK) != SRMMU_ET_PTE)
		memprobe_error("Wheee, kernel not mapped at all by boot loader.\n");
	physpage = (raw_pte & SRMMU_PTE_PMASK) << 4;
	physpage -= PAGE_SIZE;
	if(physpage & ~(SRMMU_PGDIR_MASK))
		memprobe_error("Wheee, kernel not mapped on 16MB physical boundry.\n");
	entry = find_in_spbanks(physpage);
	if(entry == -1 || (sp_banks[entry].base_addr != physpage))
		memprobe_error("Kernel mapped in non-existant memory.\n");
	MKTRACE(("map_kernel: map_spbank(vbase=%08x, entry<%d>)[%08lx,%08lx]\n", KERNBASE, entry, sp_banks[entry].base_addr, sp_banks[entry].num_bytes));
	if(((KERNBASE + (sp_banks[entry].num_bytes)) > 0xfd000000) ||
	   ((KERNBASE + (sp_banks[entry].num_bytes)) < KERNBASE)) {
		unsigned long orig_base = sp_banks[entry].base_addr;
		unsigned long orig_len = sp_banks[entry].num_bytes;
		unsigned long can_map = (0xfd000000 - KERNBASE);
		
		/* Map a partial bank in this case, adjust the base
		 * and the length, but don't mark it used.
		 */
		sp_banks[entry].num_bytes = can_map;
		MKTRACE(("wheee really big mapping [%08lx,%08lx]", orig_base, can_map));
		vaddr = map_spbank(KERNBASE, entry);
		MKTRACE(("vaddr now %08lx ", vaddr));
		sp_banks[entry].base_addr = orig_base + can_map;
		sp_banks[entry].num_bytes = orig_len - can_map;
		MKTRACE(("adjust[%08lx,%08lx]\n", (orig_base + can_map), (orig_len - can_map)));
		MKTRACE(("map_kernel: skipping first loop\n"));
		goto loop_skip;
	}
	vaddr = map_spbank(KERNBASE, entry);
	etaken[entry] = 1;

	/* Step 4: Map what we can above KERNBASE. */
	MKTRACE(("map_kernel: vaddr=%08lx, entering first loop\n", vaddr));
	for(;;) {
		unsigned long bank_size;

		MKTRACE(("map_kernel: ffsp()"));
		entry = find_free_spbank(&etaken[0]);
		bank_size = sp_banks[entry].num_bytes;
		MKTRACE(("<%d> base=%08lx bs=%08lx ", entry, sp_banks[entry].base_addr, bank_size));
		if(!bank_size)
			break;
		if(((vaddr + bank_size) >= 0xfd000000) ||
		   ((vaddr + bank_size) < KERNBASE)) {
			unsigned long orig_base = sp_banks[entry].base_addr;
			unsigned long orig_len = sp_banks[entry].num_bytes;
			unsigned long can_map = (0xfd000000 - vaddr);

			/* Map a partial bank in this case, adjust the base
			 * and the length, but don't mark it used.
			 */
			sp_banks[entry].num_bytes = can_map;
			MKTRACE(("wheee really big mapping [%08lx,%08lx]", orig_base, can_map));
			vaddr = map_spbank(vaddr, entry);
			MKTRACE(("vaddr now %08lx ", vaddr));
			sp_banks[entry].base_addr = orig_base + can_map;
			sp_banks[entry].num_bytes = orig_len - can_map;
			MKTRACE(("adjust[%08lx,%08lx]\n", (orig_base + can_map), (orig_len - can_map)));
			break;
		}
		if(!bank_size)
			break;

		/* Ok, we can map this one, do it. */
		MKTRACE(("map_spbank(%08lx,entry<%d>) ", vaddr, entry));
		vaddr = map_spbank(vaddr, entry);
		etaken[entry] = 1;
		MKTRACE(("vaddr now %08lx\n", vaddr));
	}
	MKTRACE(("\n"));
	/* If not lots_of_ram, assume we did indeed map it all above. */
loop_skip:
	if(!lots_of_ram)
		goto check_and_return;
	
	/* Step 5: Map the rest (if any) right below KERNBASE. */
	MKTRACE(("map_kernel: doing low mappings... "));
	tally = 0;
	for(entry = 0; sp_banks[entry].num_bytes; entry++) {
		if(!etaken[entry])
			tally += SRMMU_PGDIR_ALIGN(sp_banks[entry].num_bytes);
	}
	if(!tally)
		memprobe_error("Whee, lots_of_ram yet no low pages to map.\n");
	low_base = (KERNBASE - tally);
	MKTRACE(("tally=%08lx low_base=%08lx\n", tally, low_base));

	/* Ok, now map 'em. */
	MKTRACE(("map_kernel: Allocate pt skeleton (%08lx, %08x)\n",low_base,KERNBASE));
	srmmu_allocate_ptable_skeleton(low_base, KERNBASE);
	vaddr = low_base;
	MKTRACE(("map_kernel: vaddr=%08lx Entering second loop for low maps.\n", vaddr));
	for(;;) {
		unsigned long bank_size;

		entry = find_free_spbank(&etaken[0]);
		bank_size = sp_banks[entry].num_bytes;
		MKTRACE(("map_kernel: e<%d> base=%08lx bs=%08lx ", entry, sp_banks[entry].base_addr, bank_size));
		if(!bank_size)
			break;
		if((vaddr + bank_size) > KERNBASE)
			memprobe_error("Wheee, kernel low mapping overflow.\n");
		MKTRACE(("map_spbank(%08lx, %d) ", vaddr, entry));
		vaddr = map_spbank(vaddr, entry);
		etaken[entry] = 1;
		tally -= SRMMU_PGDIR_ALIGN(bank_size);
		MKTRACE(("Now, vaddr=%08lx tally=%08lx\n", vaddr, tally));
	}
	MKTRACE(("\n"));
	if(tally)
		memprobe_error("Wheee, did not map all of low mappings.\n");
check_and_return:
	/* Step 6: Sanity check, make sure we did it all. */
	MKTRACE(("check_and_return: "));
	for(entry = 0; sp_banks[entry].num_bytes; entry++) {
		MKTRACE(("e[%d]=%d ", entry, etaken[entry]));
		if(!etaken[entry]) {
			MKTRACE(("oops\n"));
			memprobe_error("Some bank did not get mapped.\n");
		}
	}
	MKTRACE(("success\n"));
	init_task.mm->mmap->vm_start = page_offset = low_base;
	stack_top = page_offset - PAGE_SIZE;
	return; /* SUCCESS! */
}

unsigned long srmmu_endmem_fixup(unsigned long mem_end_now)
{
	unsigned long tally = 0;
	int i;

	for(i = 0; sp_banks[i].num_bytes; i++)
		tally += SRMMU_PGDIR_ALIGN(sp_banks[i].num_bytes);
	if(tally < (0x0d000000UL)) {
		return KERNBASE + tally;
	} else {
		return 0xfd000000UL;
	}
}

/* Paging initialization on the Sparc Reference MMU. */
extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);

extern int physmem_mapped_contig;
extern int linux_num_cpus;

void (*poke_srmmu)(void);

unsigned long srmmu_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long ptables_start;
	int i, cpunode;
	char node_str[128];

	sparc_iobase_vaddr = 0xfd000000;    /* 16MB of IOSPACE on all sun4m's. */
	physmem_mapped_contig = 0;	    /* for init.c:taint_real_pages()   */

#if CONFIG_AP1000
        num_contexts = AP_NUM_CONTEXTS;
#else
	/* Find the number of contexts on the srmmu. */
	cpunode = prom_getchild(prom_root_node);
	num_contexts = 0;
	while((cpunode = prom_getsibling(cpunode)) != 0) {
		prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
		if(!strcmp(node_str, "cpu")) {
			num_contexts = prom_getintdefault(cpunode, "mmu-nctx", 0x8);
			break;
		}
	}
#endif
	if(!num_contexts) {
		prom_printf("Something wrong, can't find cpu node in paging_init.\n");
		prom_halt();
	}
		
	ptables_start = mempool = PAGE_ALIGN(start_mem);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	kbpage = srmmu_hwprobe(KERNBASE + PAGE_SIZE);
	kbpage = (kbpage & SRMMU_PTE_PMASK) << 4;
	kbpage -= PAGE_SIZE;

	srmmu_allocate_ptable_skeleton(KERNBASE, end_mem);
#if CONFIG_SUN_IO
	srmmu_allocate_ptable_skeleton(sparc_iobase_vaddr, IOBASE_END);
	srmmu_allocate_ptable_skeleton(DVMA_VADDR, DVMA_END);
#endif

	mempool = PAGE_ALIGN(mempool);
#if CONFIG_AP1000
        ap_inherit_mappings();
#else
        srmmu_inherit_prom_mappings(0xfe400000,(LINUX_OPPROM_ENDVM-PAGE_SIZE));
#endif
	map_kernel();
#if CONFIG_AP1000
	/* the MSC wants this aligned on a 16k boundary */
	srmmu_context_table = 
	  sparc_init_alloc(&mempool, 
			   num_contexts*sizeof(ctxd_t)<0x4000?
			   0x4000:
			   num_contexts*sizeof(ctxd_t));
#else
	srmmu_context_table = sparc_init_alloc(&mempool, num_contexts*sizeof(ctxd_t));
#endif
	srmmu_ctx_table_phys = (ctxd_t *) srmmu_v2p((unsigned long) srmmu_context_table);
	for(i = 0; i < num_contexts; i++)
		ctxd_set(&srmmu_context_table[i], swapper_pg_dir);

	start_mem = PAGE_ALIGN(mempool);

	flush_cache_all();
	if(flush_page_for_dma == viking_no_mxcc_flush_page) {
		unsigned long start = ptables_start;
		unsigned long end = start_mem;

		while(start < end) {
			viking_no_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	srmmu_set_ctable_ptr((unsigned long) srmmu_ctx_table_phys);
	flush_tlb_all();
	poke_srmmu();

#if CONFIG_AP1000
	/* on the AP we don't put the top few contexts into the free
	   context list as these are reserved for parallel tasks */
	start_mem = sparc_context_init(start_mem, MPP_CONTEXT_BASE);
#else
	start_mem = sparc_context_init(start_mem, num_contexts);
#endif
	start_mem = free_area_init(start_mem, end_mem);

	return PAGE_ALIGN(start_mem);
}

static char srmmuinfo[512];

static char *srmmu_mmu_info(void)
{
	sprintf(srmmuinfo, "MMU type\t: %s\n"
		"invall\t\t: %d\n"
		"invmm\t\t: %d\n"
		"invrnge\t\t: %d\n"
		"invpg\t\t: %d\n"
		"contexts\t: %d\n"
#ifdef USE_CHUNK_ALLOC
		"big chunks\t: %d\n"
		"little chunks\t: %d\n"
		"chunk pages\t: %d\n"
		"garbage\t\t: %d\n"
		"garbage hits\t: %d\n"
#endif
		, srmmu_name,
		module_stats.invall,
		module_stats.invmm,
		module_stats.invrnge,
		module_stats.invpg,
		num_contexts
#ifdef USE_CHUNK_ALLOC
		, bcwater, lcwater,
		chunk_pages,
		garbage_calls,
		clct_pages
#endif
		);
	return srmmuinfo;
}

static void srmmu_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
}

static void srmmu_exit_hook(void)
{
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT && mm->count == 1) {
		flush_cache_mm(mm);
		ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		flush_tlb_mm(mm);
		free_context(mm->context);
		mm->context = NO_CONTEXT;
	}
}

static void srmmu_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		alloc_context(current);
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[current->mm->context], current->mm->pgd);
		flush_tlb_mm(current->mm);
		srmmu_set_context(current->mm->context);
	}
}

static void srmmu_vac_update_mmu_cache(struct vm_area_struct * vma,
				       unsigned long address, pte_t pte)
{
	unsigned long offset, vaddr;
	unsigned long start;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	if((vma->vm_flags & (VM_WRITE|VM_SHARED)) == (VM_WRITE|VM_SHARED)) {
		struct vm_area_struct *vmaring;
		struct inode *inode;
		unsigned long flags;
		int alias_found = 0;

		save_and_cli(flags);

		inode = vma->vm_inode;
		if (!inode)
			goto done;
		offset = (address & PAGE_MASK) - vma->vm_start;
		vmaring = inode->i_mmap; 
		do {
			vaddr = vmaring->vm_start + offset;

			if ((vaddr ^ address) & vac_badbits) {
				alias_found++;
				start = vmaring->vm_start;
				while (start < vmaring->vm_end) {
					pgdp = srmmu_pgd_offset(vmaring->vm_mm, start);
					if(!pgdp) goto next;
					pmdp = srmmu_pmd_offset(pgdp, start);
					if(!pmdp) goto next;
					ptep = srmmu_pte_offset(pmdp, start);
					if(!ptep) goto next;

					if((pte_val(*ptep) & SRMMU_ET_MASK) == SRMMU_VALID) {
#if 1
						printk("Fixing USER/USER alias [%d:%08lx]\n",
						       vmaring->vm_mm->context, start);
#endif
						flush_cache_page(vmaring, start);
						set_pte(ptep, __pte((pte_val(*ptep) &
								     ~SRMMU_CACHE)));
						flush_tlb_page(vmaring, start);
					}
				next:
					start += PAGE_SIZE;
				}
			}
		} while ((vmaring = vmaring->vm_next_share) != inode->i_mmap);

		if(alias_found && !(pte_val(pte) & _SUN4C_PAGE_NOCACHE)) {
			pgdp = srmmu_pgd_offset(vma->vm_mm, address);
			ptep = srmmu_pte_offset((pmd_t *) pgdp, address);
			flush_cache_page(vma, address);
			pte_val(*ptep) = (pte_val(*ptep) | _SUN4C_PAGE_NOCACHE);
			flush_tlb_page(vma, address);
		}
	done:
		restore_flags(flags);
	}
}

static void hypersparc_exit_hook(void)
{
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT && mm->count == 1) {
		/* HyperSparc is copy-back, any data for this
		 * process in a modified cache line is stale
		 * and must be written back to main memory now
		 * else we eat shit later big time.
		 */
		flush_cache_mm(mm);
		ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		flush_tlb_mm(mm);
		free_context(mm->context);
		mm->context = NO_CONTEXT;
	}
}

static void hypersparc_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		alloc_context(current);
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[current->mm->context], current->mm->pgd);
		flush_tlb_mm(current->mm);
		srmmu_set_context(current->mm->context);
	}
}

/* Init various srmmu chip types. */
__initfunc(static void srmmu_is_bad(void))
{
	prom_printf("Could not determine SRMMU chip type.\n");
	prom_halt();
}

__initfunc(static void init_vac_layout(void))
{
	int nd, cache_lines;
	char node_str[128];
#ifdef __SMP__
	int cpu = 0;
	unsigned long max_size = 0;
	unsigned long min_line_size = 0x10000000;
#endif

	nd = prom_getchild(prom_root_node);
	while((nd = prom_getsibling(nd)) != 0) {
		prom_getstring(nd, "device_type", node_str, sizeof(node_str));
		if(!strcmp(node_str, "cpu")) {
			vac_line_size = prom_getint(nd, "cache-line-size");
			if (vac_line_size == -1) {
				prom_printf("can't determine cache-line-size, "
					    "halting.\n");
				prom_halt();
			}
			cache_lines = prom_getint(nd, "cache-nlines");
			if (cache_lines == -1) {
				prom_printf("can't determine cache-nlines, halting.\n");
				prom_halt();
			}

			vac_cache_size = cache_lines * vac_line_size;
			vac_badbits = (vac_cache_size - 1) & PAGE_MASK;
#ifdef __SMP__
			if(vac_cache_size > max_size)
				max_size = vac_cache_size;
			if(vac_line_size < min_line_size)
				min_line_size = vac_line_size;
			cpu++;
			if(cpu == smp_num_cpus)
				break;
#else
			break;
#endif
		}
	}
	if(nd == 0) {
		prom_printf("No CPU nodes found, halting.\n");
		prom_halt();
	}
#ifdef __SMP__
	vac_cache_size = max_size;
	vac_line_size = min_line_size;
	vac_badbits = (vac_cache_size - 1) & PAGE_MASK;
#endif
	printk("SRMMU: Using VAC size of %d bytes, line size %d bytes.\n",
	       (int)vac_cache_size, (int)vac_line_size);
}

static void poke_hypersparc(void)
{
	volatile unsigned long clear;
	unsigned long mreg = srmmu_get_mmureg();

	hyper_flush_unconditional_combined();

	mreg &= ~(HYPERSPARC_CWENABLE);
	mreg |= (HYPERSPARC_CENABLE | HYPERSPARC_WBENABLE);
	mreg |= (HYPERSPARC_CMODE);

	srmmu_set_mmureg(mreg);
	hyper_clear_all_tags();

	put_ross_icr(HYPERSPARC_ICCR_FTD | HYPERSPARC_ICCR_ICE);
	hyper_flush_whole_icache();
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();
}

__initfunc(static void init_hypersparc(void))
{
	srmmu_name = "ROSS HyperSparc";

	init_vac_layout();

	set_pte = srmmu_set_pte_nocache_hyper;
	mmu_getpage = hypersparc_getpage;
	flush_cache_all = hypersparc_flush_cache_all;
	flush_cache_mm = hypersparc_flush_cache_mm;
	flush_cache_range = hypersparc_flush_cache_range;
	flush_cache_page = hypersparc_flush_cache_page;

	flush_tlb_all = hypersparc_flush_tlb_all;
	flush_tlb_mm = hypersparc_flush_tlb_mm;
	flush_tlb_range = hypersparc_flush_tlb_range;
	flush_tlb_page = hypersparc_flush_tlb_page;

	flush_page_to_ram = hypersparc_flush_page_to_ram;
	flush_sig_insns = hypersparc_flush_sig_insns;
	flush_page_for_dma = hypersparc_flush_page_for_dma;
	flush_cache_page_to_uncache = hypersparc_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = hypersparc_flush_tlb_page_for_cbit;
	pgd_flush = hypersparc_pgd_flush;

	ctxd_set = hypersparc_ctxd_set;
	switch_to_context = hypersparc_switch_to_context;
	mmu_exit_hook = hypersparc_exit_hook;
	mmu_flush_hook = hypersparc_flush_hook;
	update_mmu_cache = srmmu_vac_update_mmu_cache;
	sparc_update_rootmmu_dir = hypersparc_update_rootmmu_dir;
	poke_srmmu = poke_hypersparc;
}

static void poke_cypress(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	unsigned long faddr;
	volatile unsigned long clear;

	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();

	for(faddr = 0x0; faddr < 0x10000; faddr += 20) {
		__asm__ __volatile__("sta %%g0, [%0 + %1] %2\n\t"
				     "sta %%g0, [%0] %2\n\t" : :
				     "r" (faddr), "r" (0x40000),
				     "i" (ASI_M_DATAC_TAG));
	}

	/* And one more, for our good neighbor, Mr. Broken Cypress. */
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();

	mreg |= (CYPRESS_CENABLE | CYPRESS_CMODE);
	srmmu_set_mmureg(mreg);
}

__initfunc(static void init_cypress_common(void))
{
	init_vac_layout();

	set_pte = srmmu_set_pte_nocache_cypress;
	mmu_getpage = cypress_getpage;
	flush_cache_all = cypress_flush_cache_all;
	flush_cache_mm = cypress_flush_cache_mm;
	flush_cache_range = cypress_flush_cache_range;
	flush_cache_page = cypress_flush_cache_page;

	flush_tlb_all = cypress_flush_tlb_all;
	flush_tlb_mm = cypress_flush_tlb_mm;
	flush_tlb_page = cypress_flush_tlb_page;
	flush_tlb_range = cypress_flush_tlb_range;

	flush_page_to_ram = cypress_flush_page_to_ram;
	flush_sig_insns = cypress_flush_sig_insns;
	flush_page_for_dma = cypress_flush_page_for_dma;
	flush_cache_page_to_uncache = cypress_flush_page_to_uncache;
	flush_tlb_page_for_cbit = cypress_flush_tlb_page_for_cbit;
	pgd_flush = cypress_pgd_flush;

	update_mmu_cache = srmmu_vac_update_mmu_cache;
	poke_srmmu = poke_cypress;
}

__initfunc(static void init_cypress_604(void))
{
	srmmu_name = "ROSS Cypress-604(UP)";
	srmmu_modtype = Cypress;
	init_cypress_common();
}

__initfunc(static void init_cypress_605(unsigned long mrev))
{
	srmmu_name = "ROSS Cypress-605(MP)";
	if(mrev == 0xe) {
		srmmu_modtype = Cypress_vE;
		hwbug_bitmask |= HWBUG_COPYBACK_BROKEN;
	} else {
		if(mrev == 0xd) {
			srmmu_modtype = Cypress_vD;
			hwbug_bitmask |= HWBUG_ASIFLUSH_BROKEN;
		} else {
			srmmu_modtype = Cypress;
		}
	}
	init_cypress_common();
}

static void poke_swift(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Clear any crap from the cache or else... */
	swift_idflash_clear();
	mreg |= (SWIFT_IE | SWIFT_DE); /* I & D caches on */

	/* The Swift branch folding logic is completely broken.  At
	 * trap time, if things are just right, if can mistakenly
	 * think that a trap is coming from kernel mode when in fact
	 * it is coming from user mode (it mis-executes the branch in
	 * the trap code).  So you see things like crashme completely
	 * hosing your machine which is completely unacceptable.  Turn
	 * this shit off... nice job Fujitsu.
	 */
	mreg &= ~(SWIFT_BF);
	srmmu_set_mmureg(mreg);
}

#define SWIFT_MASKID_ADDR  0x10003018
__initfunc(static void init_swift(void))
{
	unsigned long swift_rev;

	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     "srl %0, 0x18, %0\n\t" :
			     "=r" (swift_rev) :
			     "r" (SWIFT_MASKID_ADDR), "i" (ASI_M_BYPASS));
	srmmu_name = "Fujitsu Swift";
	switch(swift_rev) {
	case 0x11:
	case 0x20:
	case 0x23:
	case 0x30:
		srmmu_modtype = Swift_lots_o_bugs;
		hwbug_bitmask |= (HWBUG_KERN_ACCBROKEN | HWBUG_KERN_CBITBROKEN);
		/* Gee george, I wonder why Sun is so hush hush about
		 * this hardware bug... really braindamage stuff going
		 * on here.  However I think we can find a way to avoid
		 * all of the workaround overhead under Linux.  Basically,
		 * any page fault can cause kernel pages to become user
		 * accessible (the mmu gets confused and clears some of
		 * the ACC bits in kernel ptes).  Aha, sounds pretty
		 * horrible eh?  But wait, after extensive testing it appears
		 * that if you use pgd_t level large kernel pte's (like the
		 * 4MB pages on the Pentium) the bug does not get tripped
		 * at all.  This avoids almost all of the major overhead.
		 * Welcome to a world where your vendor tells you to,
		 * "apply this kernel patch" instead of "sorry for the
		 * broken hardware, send it back and we'll give you
		 * properly functioning parts"
		 */
		break;
	case 0x25:
	case 0x31:
		srmmu_modtype = Swift_bad_c;
		hwbug_bitmask |= HWBUG_KERN_CBITBROKEN;
		/* You see Sun allude to this hardware bug but never
		 * admit things directly, they'll say things like,
		 * "the Swift chip cache problems" or similar.
		 */
		break;
	default:
		srmmu_modtype = Swift_ok;
		break;
	};

	flush_cache_all = swift_flush_cache_all;
	flush_cache_mm = swift_flush_cache_mm;
	flush_cache_page = swift_flush_cache_page;
	flush_cache_range = swift_flush_cache_range;

	flush_tlb_all = swift_flush_tlb_all;
	flush_tlb_mm = swift_flush_tlb_mm;
	flush_tlb_page = swift_flush_tlb_page;
	flush_tlb_range = swift_flush_tlb_range;

	flush_page_to_ram = swift_flush_page_to_ram;
	flush_sig_insns = swift_flush_sig_insns;
	flush_page_for_dma = swift_flush_page_for_dma;
	flush_cache_page_to_uncache = swift_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = swift_flush_tlb_page_for_cbit;

	/* Are you now convinced that the Swift is one of the
	 * biggest VLSI abortions of all time?  Bravo Fujitsu!
	 * Fujitsu, the !#?!%$'d up processor people.  I bet if
	 * you examined the microcode of the Swift you'd find
	 * XXX's all over the place.
	 */
	poke_srmmu = poke_swift;
}

static void poke_tsunami(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	tsunami_flush_icache();
	tsunami_flush_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
}

__initfunc(static void init_tsunami(void))
{
	/* Tsunami's pretty sane, Sun and TI actually got it
	 * somewhat right this time.  Fujitsu should have
	 * taken some lessons from them.
	 */

	srmmu_name = "TI Tsunami";
	srmmu_modtype = Tsunami;

	flush_cache_all = tsunami_flush_cache_all;
	flush_cache_mm = tsunami_flush_cache_mm;
	flush_cache_page = tsunami_flush_cache_page;
	flush_cache_range = tsunami_flush_cache_range;

	flush_tlb_all = tsunami_flush_tlb_all;
	flush_tlb_mm = tsunami_flush_tlb_mm;
	flush_tlb_page = tsunami_flush_tlb_page;
	flush_tlb_range = tsunami_flush_tlb_range;

	flush_page_to_ram = tsunami_flush_page_to_ram;
	flush_sig_insns = tsunami_flush_sig_insns;
	flush_page_for_dma = tsunami_flush_page_for_dma;
	flush_cache_page_to_uncache = tsunami_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = tsunami_flush_tlb_page_for_cbit;

	poke_srmmu = poke_tsunami;
}

static void poke_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	static int smp_catch = 0;

	if(viking_mxcc_present) {
		unsigned long mxcc_control = mxcc_get_creg();

		mxcc_control |= (MXCC_CTL_ECE | MXCC_CTL_PRE | MXCC_CTL_MCE);
		mxcc_control &= ~(MXCC_CTL_RRC);
		mxcc_set_creg(mxcc_control);

		/* We don't need memory parity checks.
		 * XXX This is a mess, have to dig out later. ecd.
		viking_mxcc_turn_off_parity(&mreg, &mxcc_control);
		 */

		/* We do cache ptables on MXCC. */
		mreg |= VIKING_TCENABLE;
	} else {
		unsigned long bpreg;

		mreg &= ~(VIKING_TCENABLE);
		if(smp_catch++) {
			/* Must disable mixed-cmd mode here for
			 * other cpu's.
			 */
			bpreg = viking_get_bpreg();
			bpreg &= ~(VIKING_ACTION_MIX);
			viking_set_bpreg(bpreg);

			/* Just in case PROM does something funny. */
			msi_set_sync();
		}
	}

	mreg |= VIKING_SPENABLE;
	mreg |= (VIKING_ICENABLE | VIKING_DCENABLE);
	mreg |= VIKING_SBENABLE;
	mreg &= ~(VIKING_ACENABLE);
#if CONFIG_AP1000
        mreg &= ~(VIKING_SBENABLE);
#endif
	srmmu_set_mmureg(mreg);

#ifdef __SMP__
	/* Avoid unnecessary cross calls. */
	flush_cache_all = local_flush_cache_all;
	flush_page_to_ram = local_flush_page_to_ram;
	flush_sig_insns = local_flush_sig_insns;
	flush_page_for_dma = local_flush_page_for_dma;
	if (viking_mxcc_present) {
		flush_cache_page_to_uncache = local_flush_cache_page_to_uncache;
	}
#endif
}

__initfunc(static void init_viking(void))
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */

	if(mreg & VIKING_MMODE) {
		unsigned long bpreg;

		srmmu_name = "TI Viking";
		viking_mxcc_present = 0;

		bpreg = viking_get_bpreg();
		bpreg &= ~(VIKING_ACTION_MIX);
		viking_set_bpreg(bpreg);

		msi_set_sync();

		mmu_getpage = viking_no_mxcc_getpage;
		set_pte = srmmu_set_pte_nocache_nomxccvik;
		pgd_flush = viking_no_mxcc_pgd_flush;

		flush_cache_page_to_uncache = viking_no_mxcc_flush_page;

		/* We need this to make sure old viking takes no hits
		 * on it's cache for dma snoops to workaround the
		 * "load from non-cacheable memory" interrupt bug.
		 * This is only necessary because of the new way in
		 * which we use the IOMMU.
		 */
		flush_page_for_dma = viking_no_mxcc_flush_page;
	} else {
		srmmu_name = "TI Viking/MXCC";
		viking_mxcc_present = 1;
		flush_cache_page_to_uncache = viking_mxcc_flush_page;

		/* MXCC vikings lack the DMA snooping bug. */
		flush_page_for_dma = viking_flush_page_for_dma;
	}

	flush_cache_all = viking_flush_cache_all;
	flush_cache_mm = viking_flush_cache_mm;
	flush_cache_page = viking_flush_cache_page;
	flush_cache_range = viking_flush_cache_range;

	flush_tlb_all = viking_flush_tlb_all;
	flush_tlb_mm = viking_flush_tlb_mm;
	flush_tlb_page = viking_flush_tlb_page;
	flush_tlb_range = viking_flush_tlb_range;

	flush_page_to_ram = viking_flush_page_to_ram;
	flush_sig_insns = viking_flush_sig_insns;
	flush_tlb_page_for_cbit = viking_flush_tlb_page_for_cbit;

	poke_srmmu = poke_viking;
}

/* Probe for the srmmu chip version. */
__initfunc(static void get_srmmu_type(void))
{
	unsigned long mreg, psr;
	unsigned long mod_typ, mod_rev, psr_typ, psr_vers;

	srmmu_modtype = SRMMU_INVAL_MOD;
	hwbug_bitmask = 0;

	mreg = srmmu_get_mmureg(); psr = get_psr();
	mod_typ = (mreg & 0xf0000000) >> 28;
	mod_rev = (mreg & 0x0f000000) >> 24;
	psr_typ = (psr >> 28) & 0xf;
	psr_vers = (psr >> 24) & 0xf;

	/* First, check for HyperSparc or Cypress. */
	if(mod_typ == 1) {
		switch(mod_rev) {
		case 7:
			/* UP or MP Hypersparc */
			init_hypersparc();
			break;
		case 0:
			/* Uniprocessor Cypress */
			init_cypress_604();
			break;
		case 10:
		case 11:
		case 12:
			/* _REALLY OLD_ Cypress MP chips... */
		case 13:
		case 14:
		case 15:
			/* MP Cypress mmu/cache-controller */
			init_cypress_605(mod_rev);
			break;
		default:
			/* Some other Cypress revision, assume a 605. */
			init_cypress_605(mod_rev);
			break;
		};
		return;
	}

	/* Next check for Fujitsu Swift. */
	if(psr_typ == 0 && psr_vers == 4) {
		init_swift();
		return;
	}

	/* Now the Viking family of srmmu. */
	if(psr_typ == 4 &&
	   ((psr_vers == 0) ||
	    ((psr_vers == 1) && (mod_typ == 0) && (mod_rev == 0)))) {
		init_viking();
		return;
	}

	/* Finally the Tsunami. */
	if(psr_typ == 4 && psr_vers == 1 && (mod_typ || mod_rev)) {
		init_tsunami();
		return;
	}

	/* Oh well */
	srmmu_is_bad();
}

extern unsigned long spwin_mmu_patchme, fwin_mmu_patchme,
	tsetup_mmu_patchme, rtrap_mmu_patchme;

extern unsigned long spwin_srmmu_stackchk, srmmu_fwin_stackchk,
	tsetup_srmmu_stackchk, srmmu_rett_stackchk;

#ifdef __SMP__
extern unsigned long rirq_mmu_patchme, srmmu_reti_stackchk;
#endif

extern unsigned long srmmu_fault;

#define PATCH_BRANCH(insn, dest) do { \
		iaddr = &(insn); \
		daddr = &(dest); \
		*iaddr = SPARC_BRANCH((unsigned long) daddr, (unsigned long) iaddr); \
        } while(0);

__initfunc(static void patch_window_trap_handlers(void))
{
	unsigned long *iaddr, *daddr;
	
	PATCH_BRANCH(spwin_mmu_patchme, spwin_srmmu_stackchk);
	PATCH_BRANCH(fwin_mmu_patchme, srmmu_fwin_stackchk);
	PATCH_BRANCH(tsetup_mmu_patchme, tsetup_srmmu_stackchk);
	PATCH_BRANCH(rtrap_mmu_patchme, srmmu_rett_stackchk);
#ifdef __SMP__
	PATCH_BRANCH(rirq_mmu_patchme, srmmu_reti_stackchk);
#endif
	PATCH_BRANCH(sparc_ttable[SP_TRAP_TFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DACC].inst_three, srmmu_fault);
}

#ifdef __SMP__
/* Local cross-calls. */
static void smp_flush_page_for_dma(unsigned long page)
{
	xc1((smpfunc_t) local_flush_page_for_dma, page);
}

static void smp_flush_cache_page_to_uncache(unsigned long page)
{
	xc1((smpfunc_t) local_flush_cache_page_to_uncache, page);
}

static void smp_flush_tlb_page_for_cbit(unsigned long page)
{
	xc1((smpfunc_t) local_flush_tlb_page_for_cbit, page);
}
#endif

/* Load up routines and constants for sun4m mmu */
__initfunc(void ld_mmu_srmmu(void))
{
	/* First the constants */
	pmd_shift = SRMMU_PMD_SHIFT;
	pmd_size = SRMMU_PMD_SIZE;
	pmd_mask = SRMMU_PMD_MASK;
	pgdir_shift = SRMMU_PGDIR_SHIFT;
	pgdir_size = SRMMU_PGDIR_SIZE;
	pgdir_mask = SRMMU_PGDIR_MASK;

	ptrs_per_pte = SRMMU_PTRS_PER_PTE;
	ptrs_per_pmd = SRMMU_PTRS_PER_PMD;
	ptrs_per_pgd = SRMMU_PTRS_PER_PGD;

	page_none = SRMMU_PAGE_NONE;
	page_shared = SRMMU_PAGE_SHARED;
	page_copy = SRMMU_PAGE_COPY;
	page_readonly = SRMMU_PAGE_RDONLY;
	page_kernel = SRMMU_PAGE_KERNEL;
	pg_iobits = SRMMU_VALID | SRMMU_WRITE | SRMMU_REF;
	    
	/* Functions */
	mmu_getpage = srmmu_getpage;
	set_pte = srmmu_set_pte_cacheable;
	switch_to_context = srmmu_switch_to_context;
	pmd_align = srmmu_pmd_align;
	pgdir_align = srmmu_pgdir_align;
	vmalloc_start = srmmu_vmalloc_start;

	pte_page = srmmu_pte_page;
	pmd_page = srmmu_pmd_page;
	pgd_page = srmmu_pgd_page;

	sparc_update_rootmmu_dir = srmmu_update_rootmmu_dir;

	pte_none = srmmu_pte_none;
	pte_present = srmmu_pte_present;
	pte_clear = srmmu_pte_clear;

	pmd_none = srmmu_pmd_none;
	pmd_bad = srmmu_pmd_bad;
	pmd_present = srmmu_pmd_present;
	pmd_clear = srmmu_pmd_clear;

	pgd_none = srmmu_pgd_none;
	pgd_bad = srmmu_pgd_bad;
	pgd_present = srmmu_pgd_present;
	pgd_clear = srmmu_pgd_clear;

	mk_pte = srmmu_mk_pte;
	mk_pte_phys = srmmu_mk_pte_phys;
	pgd_set = srmmu_pgd_set;
	mk_pte_io = srmmu_mk_pte_io;
	pte_modify = srmmu_pte_modify;
	pgd_offset = srmmu_pgd_offset;
	pmd_offset = srmmu_pmd_offset;
	pte_offset = srmmu_pte_offset;
	pte_free_kernel = srmmu_pte_free_kernel;
	pmd_free_kernel = srmmu_pmd_free_kernel;
	pte_alloc_kernel = srmmu_pte_alloc_kernel;
	pmd_alloc_kernel = srmmu_pmd_alloc_kernel;
	pte_free = srmmu_pte_free;
	pte_alloc = srmmu_pte_alloc;
	pmd_free = srmmu_pmd_free;
	pmd_alloc = srmmu_pmd_alloc;
	pgd_free = srmmu_pgd_free;
	pgd_alloc = srmmu_pgd_alloc;
	pgd_flush = srmmu_pgd_flush;

	pte_write = srmmu_pte_write;
	pte_dirty = srmmu_pte_dirty;
	pte_young = srmmu_pte_young;
	pte_wrprotect = srmmu_pte_wrprotect;
	pte_mkclean = srmmu_pte_mkclean;
	pte_mkold = srmmu_pte_mkold;
	pte_mkwrite = srmmu_pte_mkwrite;
	pte_mkdirty = srmmu_pte_mkdirty;
	pte_mkyoung = srmmu_pte_mkyoung;
	update_mmu_cache = srmmu_update_mmu_cache;
	mmu_exit_hook = srmmu_exit_hook;
	mmu_flush_hook = srmmu_flush_hook;
	mmu_lockarea = srmmu_lockarea;
	mmu_unlockarea = srmmu_unlockarea;

	mmu_get_scsi_one = srmmu_get_scsi_one;
	mmu_get_scsi_sgl = srmmu_get_scsi_sgl;
	mmu_release_scsi_one = srmmu_release_scsi_one;
	mmu_release_scsi_sgl = srmmu_release_scsi_sgl;

	mmu_map_dma_area = srmmu_map_dma_area;

	mmu_info = srmmu_mmu_info;
        mmu_v2p = srmmu_v2p;
        mmu_p2v = srmmu_p2v;

	/* Task struct and kernel stack allocating/freeing. */
	alloc_kernel_stack = srmmu_alloc_kernel_stack;
	alloc_task_struct = srmmu_alloc_task_struct;
	free_kernel_stack = srmmu_free_kernel_stack;
	free_task_struct = srmmu_free_task_struct;

	quick_kernel_fault = srmmu_quick_kernel_fault;

	/* SRMMU specific. */
	ctxd_set = srmmu_ctxd_set;
	pmd_set = srmmu_pmd_set;

	get_srmmu_type();
	patch_window_trap_handlers();

#ifdef __SMP__
	/* El switcheroo... */

	local_flush_cache_all = flush_cache_all;
	local_flush_cache_mm = flush_cache_mm;
	local_flush_cache_range = flush_cache_range;
	local_flush_cache_page = flush_cache_page;
	local_flush_tlb_all = flush_tlb_all;
	local_flush_tlb_mm = flush_tlb_mm;
	local_flush_tlb_range = flush_tlb_range;
	local_flush_tlb_page = flush_tlb_page;
	local_flush_page_to_ram = flush_page_to_ram;
	local_flush_sig_insns = flush_sig_insns;
	local_flush_page_for_dma = flush_page_for_dma;
	local_flush_cache_page_to_uncache = flush_cache_page_to_uncache;
	local_flush_tlb_page_for_cbit = flush_tlb_page_for_cbit;

	flush_cache_all = smp_flush_cache_all;
	flush_cache_mm = smp_flush_cache_mm;
	flush_cache_range = smp_flush_cache_range;
	flush_cache_page = smp_flush_cache_page;
	flush_tlb_all = smp_flush_tlb_all;
	flush_tlb_mm = smp_flush_tlb_mm;
	flush_tlb_range = smp_flush_tlb_range;
	flush_tlb_page = smp_flush_tlb_page;
	flush_page_to_ram = smp_flush_page_to_ram;
	flush_sig_insns = smp_flush_sig_insns;
	flush_page_for_dma = smp_flush_page_for_dma;
	flush_cache_page_to_uncache = smp_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = smp_flush_tlb_page_for_cbit;
#endif
}
