// SPDX-License-Identifier: GPL-2.0
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#include <linux/hugetlb.h>
#include <linux/pagewalk.h>

#include "prmtv-common.h"

/*
 * Get an online page for a pfn if it's in the LRU list.  Otherwise, returns
 * NULL.
 *
 * The body of this function is stolen from the 'page_idle_get_page()'.  We
 * steal rather than reuse it because the code is quite simple.
 */
struct page *damon_get_page(unsigned long pfn)
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

static int damon_mkold_pmd_entry(pmd_t *pmd, unsigned long addr,
		unsigned long next, struct mm_walk *walk)
{
	pte_t *pte;
	spinlock_t *ptl;

	if (pmd_huge(*pmd)) {
		ptl = pmd_lock(walk->mm, pmd);
		if (pmd_huge(*pmd)) {
			damon_pmdp_mkold(pmd, walk->mm, addr);
			spin_unlock(ptl);
			return 0;
		}
		spin_unlock(ptl);
	}

	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		return 0;
	pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	if (!pte_present(*pte))
		goto out;
	damon_ptep_mkold(pte, walk->mm, addr);
out:
	pte_unmap_unlock(pte, ptl);
	return 0;
}

static struct mm_walk_ops damon_mkold_ops = {
	.pmd_entry = damon_mkold_pmd_entry,
};

void damon_va_mkold(struct mm_struct *mm, unsigned long addr)
{
	mmap_read_lock(mm);
	walk_page_range(mm, addr, addr + 1, &damon_mkold_ops, NULL);
	mmap_read_unlock(mm);
}

struct damon_young_walk_private {
	unsigned long *page_sz;
	bool young;
};

static int damon_young_pmd_entry(pmd_t *pmd, unsigned long addr,
		unsigned long next, struct mm_walk *walk)
{
	pte_t *pte;
	spinlock_t *ptl;
	struct page *page;
	struct damon_young_walk_private *priv = walk->private;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (pmd_huge(*pmd)) {
		ptl = pmd_lock(walk->mm, pmd);
		if (!pmd_huge(*pmd)) {
			spin_unlock(ptl);
			goto regular_page;
		}
		page = damon_get_page(pmd_pfn(*pmd));
		if (!page)
			goto huge_out;
		if (pmd_young(*pmd) || !page_is_idle(page) ||
					mmu_notifier_test_young(walk->mm,
						addr)) {
			*priv->page_sz = ((1UL) << HPAGE_PMD_SHIFT);
			priv->young = true;
		}
		put_page(page);
huge_out:
		spin_unlock(ptl);
		return 0;
	}

regular_page:
#endif	/* CONFIG_TRANSPARENT_HUGEPAGE */

	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		return -EINVAL;
	pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	if (!pte_present(*pte))
		goto out;
	page = damon_get_page(pte_pfn(*pte));
	if (!page)
		goto out;
	if (pte_young(*pte) || !page_is_idle(page) ||
			mmu_notifier_test_young(walk->mm, addr)) {
		*priv->page_sz = PAGE_SIZE;
		priv->young = true;
	}
	put_page(page);
out:
	pte_unmap_unlock(pte, ptl);
	return 0;
}

static struct mm_walk_ops damon_young_ops = {
	.pmd_entry = damon_young_pmd_entry,
};

bool damon_va_young(struct mm_struct *mm, unsigned long addr,
		unsigned long *page_sz)
{
	struct damon_young_walk_private arg = {
		.page_sz = page_sz,
		.young = false,
	};

	mmap_read_lock(mm);
	walk_page_range(mm, addr, addr + 1, &damon_young_ops, &arg);
	mmap_read_unlock(mm);
	return arg.young;
}
