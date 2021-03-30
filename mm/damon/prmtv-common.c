// SPDX-License-Identifier: GPL-2.0
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#include "prmtv-common.h"

/*
 * Get an online page for a pfn if it's in the LRU list.  Otherwise, returns
 * NULL.
 *
 * The body of this function is stolen from the 'page_idle_get_page()'.  We
 * steal rather than reuse it because the code is quite simple.
 */
static struct page *damon_get_page(unsigned long pfn)
{
	struct page *page = pfn_to_online_page(pfn);

	if (!page || !PageLRU(page) || !get_page_unless_zero(page))
		return NULL;

	if (unlikely(!PageLRU(page))) {
		put_page(page);
		page = NULL;
	}
	return page;
}

static void damon_ptep_mkold(pte_t *pte, struct mm_struct *mm,
			     unsigned long addr)
{
	bool referenced = false;
	struct page *page = damon_get_page(pte_pfn(*pte));

	if (!page)
		return;

	if (pte_young(*pte)) {
		referenced = true;
		*pte = pte_mkold(*pte);
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (mmu_notifier_clear_young(mm, addr, addr + PAGE_SIZE))
		referenced = true;
#endif /* CONFIG_MMU_NOTIFIER */

	if (referenced)
		set_page_young(page);

	set_page_idle(page);
	put_page(page);
}

static void damon_pmdp_mkold(pmd_t *pmd, struct mm_struct *mm,
			     unsigned long addr)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	bool referenced = false;
	struct page *page = damon_get_page(pmd_pfn(*pmd));

	if (!page)
		return;

	if (pmd_young(*pmd)) {
		referenced = true;
		*pmd = pmd_mkold(*pmd);
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (mmu_notifier_clear_young(mm, addr,
				addr + ((1UL) << HPAGE_PMD_SHIFT)))
		referenced = true;
#endif /* CONFIG_MMU_NOTIFIER */

	if (referenced)
		set_page_young(page);

	set_page_idle(page);
	put_page(page);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
}

void damon_va_mkold(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = NULL;
	pmd_t *pmd = NULL;
	spinlock_t *ptl;

	if (follow_invalidate_pte(mm, addr, NULL, &pte, &pmd, &ptl))
		return;

	if (pte) {
		damon_ptep_mkold(pte, mm, addr);
		pte_unmap_unlock(pte, ptl);
	} else {
		damon_pmdp_mkold(pmd, mm, addr);
		spin_unlock(ptl);
	}
}

bool damon_va_young(struct mm_struct *mm, unsigned long addr,
			unsigned long *page_sz)
{
	pte_t *pte = NULL;
	pmd_t *pmd = NULL;
	spinlock_t *ptl;
	struct page *page;
	bool young = false;

	if (follow_invalidate_pte(mm, addr, NULL, &pte, &pmd, &ptl))
		return false;

	*page_sz = PAGE_SIZE;
	if (pte) {
		page = damon_get_page(pte_pfn(*pte));
		if (page && (pte_young(*pte) || !page_is_idle(page) ||
					mmu_notifier_test_young(mm, addr)))
			young = true;
		if (page)
			put_page(page);
		pte_unmap_unlock(pte, ptl);
		return young;
	}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	page = damon_get_page(pmd_pfn(*pmd));
	if (page && (pmd_young(*pmd) || !page_is_idle(page) ||
			mmu_notifier_test_young(mm, addr)))
		young = true;
	if (page)
		put_page(page);

	spin_unlock(ptl);
	*page_sz = ((1UL) << HPAGE_PMD_SHIFT);
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */

	return young;
}

static bool __damon_pa_mkold(struct page *page, struct vm_area_struct *vma,
		unsigned long addr, void *arg)
{
	damon_va_mkold(vma->vm_mm, addr);
	return true;
}

void damon_pa_mkold(unsigned long paddr)
{
	struct page *page = damon_get_page(PHYS_PFN(paddr));
	struct rmap_walk_control rwc = {
		.rmap_one = __damon_pa_mkold,
		.anon_lock = page_lock_anon_vma_read,
	};
	bool need_lock;

	if (!page)
		return;

	if (!page_mapped(page) || !page_rmapping(page)) {
		set_page_idle(page);
		put_page(page);
		return;
	}

	need_lock = !PageAnon(page) || PageKsm(page);
	if (need_lock && !trylock_page(page)) {
		put_page(page);
		return;
	}

	rmap_walk(page, &rwc);

	if (need_lock)
		unlock_page(page);
	put_page(page);
}

struct damon_pa_access_chk_result {
	unsigned long page_sz;
	bool accessed;
};

static bool damon_pa_accessed(struct page *page, struct vm_area_struct *vma,
		unsigned long addr, void *arg)
{
	struct damon_pa_access_chk_result *result = arg;

	result->accessed = damon_va_young(vma->vm_mm, addr, &result->page_sz);

	/* If accessed, stop walking */
	return !result->accessed;
}

bool damon_pa_young(unsigned long paddr, unsigned long *page_sz)
{
	struct page *page = damon_get_page(PHYS_PFN(paddr));
	struct damon_pa_access_chk_result result = {
		.page_sz = PAGE_SIZE,
		.accessed = false,
	};
	struct rmap_walk_control rwc = {
		.arg = &result,
		.rmap_one = damon_pa_accessed,
		.anon_lock = page_lock_anon_vma_read,
	};
	bool need_lock;

	if (!page)
		return false;

	if (!page_mapped(page) || !page_rmapping(page)) {
		if (page_is_idle(page))
			result.accessed = false;
		else
			result.accessed = true;
		put_page(page);
		goto out;
	}

	need_lock = !PageAnon(page) || PageKsm(page);
	if (need_lock && !trylock_page(page)) {
		put_page(page);
		return NULL;
	}

	rmap_walk(page, &rwc);

	if (need_lock)
		unlock_page(page);
	put_page(page);

out:
	*page_sz = result.page_sz;
	return result.accessed;
}
