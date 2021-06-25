/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#include <linux/damon.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/highmem.h>
#include <linux/page_idle.h>
#include <linux/pagemap.h>
#include <linux/random.h>
#include <linux/rmap.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32_max(r - l))

struct page *damon_get_page(unsigned long pfn);

void damon_va_mkold(struct mm_struct *mm, unsigned long addr);
bool damon_va_young(struct mm_struct *mm, unsigned long addr,
			unsigned long *page_sz);

void damon_pa_mkold(unsigned long paddr);
bool damon_pa_young(unsigned long paddr, unsigned long *page_sz);

int damon_pageout_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s);
