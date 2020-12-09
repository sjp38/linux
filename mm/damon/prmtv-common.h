/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#include <linux/damon.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32_max(r - l))

void damon_va_mkold(struct mm_struct *mm, unsigned long addr);
bool damon_va_young(struct mm_struct *mm, unsigned long addr,
			unsigned long *page_sz);
