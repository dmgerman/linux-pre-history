/* $Id: pgtable.h,v 1.1 1996/12/02 00:01:17 davem Exp $
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_PGTABLE_H
#define _SPARC64_PGTABLE_H

/* This file contains the functions and defines necessary to modify and use
 * the SpitFire page tables.
 */

#include <asm/asi.h>
#include <asm/mmu_context.h>
#include <asm/system.h>

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + 2*(PAGE_SHIFT-3))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Entries per page directory level. */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PMD	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PGD	(1UL << (PAGE_SHIFT-3))

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE	(1UL << (PAGE_SHIFT-3))

#define VMALLOC_START		0xFFFFFE0000000000UL
#define VMALLOC_VMADDR(x)	((unsigned long)(x))

/* SpitFire TTE bits. */
#define _PAGE_VALID	0x8000000000000000UL	/* Valid TTE                          */
#define _PAGE_R		0x8000000000000000UL	/* Used to keep ref bit up to date    */
#define _PAGE_SZ4MB	0x6000000000000000UL	/* 4MB Page                           */
#define _PAGE_SZ512K	0x4000000000000000UL	/* 512K Page                          */
#define _PAGE_SZ64K	0x2000000000000000UL	/* 64K Page                           */
#define _PAGE_SZ8K	0x0000000000000000UL	/* 8K Page                            */
#define _PAGE_NFO	0x1000000000000000UL	/* No Fault Only                      */
#define _PAGE_IE	0x0800000000000000UL	/* Invert Endianness                  */
#define _PAGE_SOFT2	0x07FC000000000000UL	/* Second set of software bits        */
#define _PAGE_DIAG	0x0003FE0000000000UL	/* Diagnostic TTE bits                */
#define _PAGE_PADDR	0x000001FFFFFFE000UL	/* Physical Address bits [40:13]      */
#define _PAGE_SOFT	0x0000000000001F80UL	/* First set of software bits         */
#define _PAGE_L		0x0000000000000040UL	/* Locked TTE                         */
#define _PAGE_CP	0x0000000000000020UL	/* Cacheable in Physical Cache        */
#define _PAGE_CV	0x0000000000000010UL	/* Cacheable in Virtual Cache         */
#define _PAGE_E		0x0000000000000008UL    /* side-Effect                        */
#define _PAGE_P		0x0000000000000004UL	/* Privileged Page                    */
#define _PAGE_W		0x0000000000000002UL	/* Writable                           */
#define _PAGE_G		0x0000000000000001UL	/* Global                             */

/* Here are the SpitFire software bits we use in the TTE's. */
#define _PAGE_PRESENT	0x0000000000001000UL	/* Present Page (ie. not swapped out) */
#define _PAGE_MODIFIED	0x0000000000000800UL	/* Modified Page (ie. dirty)          */
#define _PAGE_ACCESSED	0x0000000000000400UL	/* Accessed Page (ie. referenced)     */
#define _PAGE_READ	0x0000000000000200UL	/* Readable SW Bit                    */
#define _PAGE_WRITE	0x0000000000000100UL	/* Writable SW Bit                    */

#define _PAGE_CACHE	(_PAGE_CP | _PAGE_CV)

#define __DIRTY_BITS	(_PAGE_MODIFIED | _PAGE_WRITE | _PAGE_W)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_READ | _PAGE_R)

#define _PFN_MASK	_PAGE_PADDR

#define _PAGE_TABLE	(_PAGE_PRESENT | __DIRTY_BITS | __ACCESS_BITS)
#define _PAGE_CHG_MASK	(_PFN_MASK | _PAGE_MODIFIED | _PAGE_ACCESSED)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_CACHE)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | __ACCESS_BITS | \
				 _PAGE_WRITE | _PAGE_CACHE)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | __ACCESS_BITS | _PAGE_CACHE)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | __ACCESS_BITS | _PAGE_CACHE)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_VALID | _PAGE_W| \
				 _PAGE_CACHE | _PAGE_P | _PAGE_G)

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

extern pte_t __bad_page(void);
extern pmd_t *__bad_pagetable(void);
extern unsigned long __zero_page(void);

#define BAD_PAGETABLE	__bad_pagetable()
#define BAD_PAGE	__bad_page()
#define ZERO_PAGE	__zero_page()

/* Cache and TLB flush operations. */

extern __inline__ void spitfire_put_dcache_tag(unsigned long addr, unsigned long tag)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : /* No outputs */
			     : "r" (tag), "r" (addr), "i" (ASI_DCACHE_TAG));
}

extern __inline__ void spitfire_put_icache_tag(unsigned long addr, unsigned long tag)
{
	__asm__ __volatile__("stxa	%0, [%1] %2"
			     : /* No outputs */
			     : "r" (tag), "r" (addr), "i" (ASI_IC_TAG));
}

/* This is a bit tricky to do most efficiently.  The I-CACHE on the
 * SpitFire will snoop stores from _other_ processors and changes done
 * by DMA, but it does _not_ snoop stores on the local processor.
 * Also, even if the I-CACHE snoops the store from someone else correctly,
 * you can still lose if the instructions are in the pipeline already.
 * A big issue is that this cache is only 16K in size, using a pseudo
 * 2-set associative scheme.  A full flush of the cache is far too much
 * for me to accept, especially since most of the time when we get to
 * running this code the icache data we want to flush is not even in
 * the cache.  Thus the following seems to be the best method.
 */
extern __inline__ void spitfire_flush_icache_page(unsigned long page)
{
	unsigned long temp;

	/* Commit all potential local stores to the instruction space
	 * on this processor before the flush.
	 */
	membar("#StoreStore");

	/* Actually perform the flush. */
	__asm__ __volatile__("
1:
	flush		%0 + 0x00
	flush		%0 + 0x08
	flush		%0 + 0x10
	flush		%0 + 0x18
	flush		%0 + 0x20
	flush		%0 + 0x28
	flush		%0 + 0x30
	flush		%0 + 0x38
	subcc		%1, 0x40, %1
	bge,pt		%icc, 1b
	 add		%2, %1, %0
"	: "=&r" (page), "=&r" (temp),
	: "r" (page), "0" (page + PAGE_SIZE - 0x40), "1" (PAGE_SIZE - 0x40));
}

extern __inline__ void flush_cache_all(void)
{
	unsigned long addr;

	flushw_all();
	for(addr = 0; addr < (PAGE_SIZE << 1); addr += 32) {
		spitfire_put_dcache_tag(addr, 0x0UL);
		spitfire_put_icache_tag(addr, 0x0UL);
		membar("#Sync");
	}
}

extern __inline__ void flush_cache_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT) {
		unsigned long addr;

		flushw_user();
		for(addr = 0; addr < (PAGE_SIZE << 1); addr += 32) {
			spitfire_put_dcache_tag(addr, 0x0UL);
			spitfire_put_icache_tag(addr, 0x0UL);
			membar("#Sync");
		}
	}
}

extern __inline__ void flush_cache_range(struct mm_struct *mm, unsigned long start,
					 unsigned long end)
{
	if(mm->context != NO_CONTEXT) {
		unsigned long addr;

		flushw_user();
		for(addr = 0; addr < (PAGE_SIZE << 1); addr += 32) {
			spitfire_put_icache_tag(addr, 0x0UL);
			membar("#Sync");
		}
	}
}

extern __inline__ void flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT && (vma->vm_flags & VM_EXEC)) {
		int ctx;

		ctx = spitfire_get_primary_context();
		flushw_user();
		spitfire_set_primary_context(mm->context);
		spitfire_flush_icache_page(page & PAGE_MASK);
		spitfire_set_primary_context(ctx);
	}
}

/* This operation in unnecessary on the SpitFire since D-CACHE is write-through. */
#define flush_page_to_ram(page)		do { } while (0)

extern __inline__ void flush_tlb_all(void)
{
	int entry;

	/* Invalidate all non-locked TTE's in both the dtlb and itlb. */
	for(entry = 0; entry < 64; entry++) {
		unsigned long dtag, itag;

		dtag = spitfire_get_dtlb_tag(entry);
		itag = spitfire_get_itlb_tag(entry);
		if(!(dtag & _PAGE_L))
			spitfire_put_dtlb_tag(entry, 0x0UL);
		if(!(itag & _PAGE_L))
			spitfire_put_itlb_tag(entry, 0x0UL);
	}
}

extern __inline__ void flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT) {
		spitfire_set_secondary_context(mm->context);
		spitfire_flush_dtlb_secondary_context();
		spitfire_flush_itlb_secondary_context();
	}
}

extern __inline__ void flush_tlb_range(struct mm_struct *mm, unsigned long start,
				       unsigned long end)
{
	if(mm->context != NO_CONTEXT) {
		start &= PAGE_MASK;
		spitfire_set_secondary_context(mm->context);
		while(start < end) {
			spitfire_flush_dtlb_secondary_page(start);
			spitfire_flush_itlb_secondary_page(start);
			start += PAGE_SIZE;
		}
	}
}

extern __inline__ void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT) {
		spitfire_set_secondary_context(mm->context);
		if(vma->vm_flags & VM_EXEC)
			spitfire_flush_itlb_secondary_page(page);
		spitfire_flush_dtlb_secondary_page(page);
	}
}

extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ return __pte((page - PAGE_OFFSET) | pgprot_val(pgprot)); }

extern inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{ return __pte(physpage | pgprot_val(pgprot)); }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pmd_set(pmd_t *pmdp, pte_t *ptep)
{ pmd_val(*pmdp) = _PAGE_TABLE | ((unsigned long) ptep); }

extern inline void pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{ pgd_val(*pgdp) = _PAGE_TABLE | ((unsigned long) pmdp); }

extern inline unsigned long pte_page(pte_t pte)
{ return PAGE_OFFSET + (pte_val(pte) & _PFN_MASK); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return (pmd_val(pmd) & PAGE_MASK); }

extern inline unsigned long pgd_page(pgd_t pgd)
{ return (pgd_val(pgd) & PAGE_MASK); }

extern inline int pte_none(pte_t pte) 		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_cleat(pte_t *pte)	{ pte_val(*pte) = 0; }

extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~PAGE_MASK) != _PAGE_TABLE; }
extern inline int pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _PAGE_PRESENT; }
extern inline void pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = 0; }

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & ~PAGE_MASK) != _PAGE_TABLE; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_PRESENT; }
extern inline void pgd_clear(pgd_t *pgdp)	{ pgd_val(*pgdp) = 0; }

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_READ; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_WRITE; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_MODIFIED; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline pte_t pte_wrprotect(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_WRITE|_PAGE_W)); }

extern inline pte_t pte_rdprotect(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_READ|_PAGE_R)); }

extern inline pte_t pte_mkclean(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_MODIFIED | _PAGE_W); }

extern inline pte_t pte_mkold(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_ACCESSED | _PAGE_R)); }

extern inline pte_t pte_mkwrite(pte_t pte)
{
	if(pte_val(pte) & _PAGE_MODIFIED)
		return __pte(pte_val(pte) | (_PAGE_WRITE | _PAGE_W));
	else
		return __pte(pte_val(pte) | (_PAGE_WRITE));
}

extern inline pte_t pte_mkdirty(pte_t pte)
{
	if(pte_val(pte) & _PAGE_WRITE)
		return __pte(pte_val(pte) | (_PAGE_MODIFIED | _PAGE_W));
	else
		return __pte(pte_val(pte) | _PAGE_MODIFIED);
}

extern inline pte_t pte_mkyoung(pte_t pte)
{
	if(pte_val(pte) & _PAGE_READ)
		return __pte(pte_val(pte) | (_PAGE_ACCESSED | _PAGE_R));
	else
		return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

extern inline void SET_PAGE_DIR(struct task_struct *tsk, pgd_t *pgdir)
{ /* XXX */ }

/* to find an entry in a page-table-directory. */
extern inline pgd_t *pgd_offset(struct mm_struct *mm, unsigned long address)
{ return mm->pgd + ((address >> PGDIR_SHIFT) & (PTRS_PER_PAGE - 1)); }

/* Find an entry in the second-level page table.. */
extern inline pmd_t *pmd_offset(pgd_t *dir, unsigned long address)
{ return (pmd_t *) pgd_page(*dir) + ((address >> PMD_SHIFT) & (PTRS_PER_PAGE - 1)); }

/* Find an entry in the third-level page table.. */
extern inline pte_t *pte_offset(pmd_t *dir, unsigned long address)
{ return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PAGE - 1)); }

/* Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on supervisor
 * bits if any.
 */
extern inline void pte_free_kernel(pte_t *pte)
{ free_page((unsigned long)pte); }

extern inline pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline void pmd_free_kernel(pmd_t *pmd)
{ free_page((unsigned long) pmd); }

extern inline pmd_t * pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *) get_free_page(GFP_KERNEL);
		if (pgd_none(*pgd)) {
			if (page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc_kernel: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pte_free(pte_t * pte)
{ free_page((unsigned long)pte); }

extern inline pte_t * pte_alloc(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline void pmd_free(pmd_t * pmd)
{ free_page((unsigned long) pmd); }

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *) get_free_page(GFP_KERNEL);
		if (pgd_none(*pgd)) {
			if (page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pgd_free(pgd_t * pgd)
{ free_page((unsigned long)pgd); }

extern inline pgd_t * pgd_alloc(void)
{ return (pgd_t *) get_free_page(GFP_KERNEL); }

#define pgd_flush(pgd)	do { } while (0)

extern pgd_t swapper_pg_dir[1024];	/* XXX */

extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{ /* XXX */ }

/* Make a non-present pseudo-TTE. */
extern inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type) | (offset << 8); return pte; }

#define SWP_TYPE(entry)		(((entry) & 0xff))
#define SWP_OFFSET(entry)	((entry) >> 8)
#define SWP_ENTRY(type,offset)	pte_val(mk_swap_pte((type),(offset)))

#endif /* _SPARC64_PGTABLE_H */
