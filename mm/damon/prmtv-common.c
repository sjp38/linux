// SPDX-License-Identifier: GPL-2.0
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#include "prmtv-common.h"

static void damon_ptep_mkold(pte_t *pte, struct mm_struct *mm,
			     unsigned long addr)
{
	bool referenced = false;
	struct page *page = pte_page(*pte);

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
}

static void damon_pmdp_mkold(pmd_t *pmd, struct mm_struct *mm,
			     unsigned long addr)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	bool referenced = false;
	struct page *page = pmd_page(*pmd);

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
	bool young = false;

	if (follow_invalidate_pte(mm, addr, NULL, &pte, &pmd, &ptl))
		return false;

	*page_sz = PAGE_SIZE;
	if (pte) {
		if (pte_young(*pte) || !page_is_idle(pte_page(*pte)) ||
				mmu_notifier_test_young(mm, addr))
			young = true;
		pte_unmap_unlock(pte, ptl);
		return young;
	}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (pmd_young(*pmd) || !page_is_idle(pmd_page(*pmd)) ||
			mmu_notifier_test_young(mm, addr))
		young = true;
	spin_unlock(ptl);
	*page_sz = ((1UL) << HPAGE_PMD_SHIFT);
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */

	return young;
}
