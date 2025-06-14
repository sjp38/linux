// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON Code for The Physical Address Space
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-pa: " fmt

#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/memory-tiers.h>
#include <linux/mm_inline.h>

#include "../internal.h"
#include "ops-common.h"

static void damon_pa_mkold(unsigned long paddr)
{
	struct folio *folio = damon_get_folio(PHYS_PFN(paddr));

	if (!folio)
		return;

	damon_folio_mkold(folio);
	folio_put(folio);
}

static void __damon_pa_prepare_access_check(struct damon_region *r)
{
	r->sampling_addr = damon_rand(r->ar.start, r->ar.end);

	damon_pa_mkold(r->sampling_addr);
}

static void damon_pa_prepare_access_checks(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t)
			__damon_pa_prepare_access_check(r);
	}
}

#ifdef CONFIG_DAMON_CADDR

/*
 * Page granular cache monitoring.
 *
 * cache line:	as everyone knows.  usually 64 bytes size.
 * cache set:	same to that of textbook. a group of cache lines mapping same
 * 		cache line index data.
 * cache group: a new concept.  a group of cache sets that mapping same pages.
 *
 * In case of 128 MiB (2**27) size cache of 64B cache line, 16 ways,
 * there are 128 MiB / 64 B (cache line size) = 2,097,152 (2**21) cache lines.
 * there are 2**21 / 16 ways = 131,072i (2**17) cache sets
 * A part of page can be mapped to 4 KiB / 64 B = 64 cache sets (a cache group).
 * There are 131,072 cache sets / 64 cache sets per cache group =  2,048 cache groups.
 * cache group #0 can contain content of page #0, #2,048, #4,096, ... (assuming
 * physical address starts from zero and there are no holes)
 *
 * Each cache group may contain content of total memory / total number of cache
 * cgroups data.  For 64 GiB system, 64 GiB / 2,048 = 32 MiB per cache set.
 * cache cgroup #0 contains data of 0B-4K, 32MiB-32MiB4K, 64MiB-64MiB4K, ...
 * cache cgroup #1 contains data of 4K-8K, 32MiB4K-32MiB8K, 64MiB4K-64MiB8K, ...
 *
 * From below, cache address means cache group index.
 */

struct damon_cache_spec {
	unsigned size;
	unsigned sz_line;
	unsigned ways;
	unsigned long min_pa;
	unsigned long max_pa;

	unsigned nr_pg_groups;
	unsigned way_capacity;
	unsigned long max_pways;
};

static struct damon_cache_spec *cache_spec;

static void damon_ca_set_spec_internal(struct damon_cache_spec *spec)
{
	unsigned sz_set = spec->sz_line * spec->ways;
	unsigned nr_sets = spec->size / sz_set;
	unsigned sets_per_group = PAGE_SIZE / spec->sz_line;

	spec->nr_pg_groups = nr_sets / sets_per_group;
	spec->way_capacity = spec->size / spec->ways;
	spec->max_pways = spec->max_pa / spec->way_capacity;
}

static struct damon_cache_spec *damon_ca_mk_cache_spec(
		unsigned size, unsigned sz_line, unsigned ways,
		unsigned long min_pa, unsigned long max_pa)
{
	struct damon_cache_spec *spec;

	spec = kmalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	spec->size = size;
	spec->sz_line = sz_line;
	spec->ways = ways;
	spec->min_pa = min_pa;
	spec->max_pa = max_pa;
	damon_ca_set_spec_internal(spec);
	return spec;
}

/* convert physical address to cache address */
/*
 * This is not really required, since we set sampling address in physical address.
 *
static unsigned long damon_pa_to_ca(unsigned long paddr)
{
	return paddr / PAGE_SIZE % cache_spec->nr_cache_groups;
}
*/

/* convert cache address to a random matching physical address */
static unsigned long damon_ca_to_pa(unsigned long caddr)
{
	unsigned long way = damon_rand(0, cache_spec->max_pways);

	return caddr * PAGE_SIZE + way * cache_spec->way_capacity;
}

static void __damon_ca_prepare_access_check(struct damon_region *r)
{
	r->sampling_addr = damon_ca_to_pa(damon_rand(r->ar.start, r->ar.end));

	damon_pa_mkold(r->sampling_addr);
}

static void damon_ca_prepare_access_checks(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t)
			__damon_ca_prepare_access_check(r);
	}
}

#endif

static bool damon_pa_young(unsigned long paddr, unsigned long *folio_sz)
{
	struct folio *folio = damon_get_folio(PHYS_PFN(paddr));
	bool accessed;

	if (!folio)
		return false;

	accessed = damon_folio_young(folio);
	*folio_sz = folio_size(folio);
	folio_put(folio);
	return accessed;
}

static void __damon_pa_check_access(struct damon_region *r,
		struct damon_attrs *attrs)
{
	static unsigned long last_addr;
	static unsigned long last_folio_sz = PAGE_SIZE;
	static bool last_accessed;

	/* If the region is in the last checked page, reuse the result */
	if (ALIGN_DOWN(last_addr, last_folio_sz) ==
				ALIGN_DOWN(r->sampling_addr, last_folio_sz)) {
		damon_update_region_access_rate(r, last_accessed, attrs);
		return;
	}

	last_accessed = damon_pa_young(r->sampling_addr, &last_folio_sz);
	damon_update_region_access_rate(r, last_accessed, attrs);

	last_addr = r->sampling_addr;
}

static unsigned int damon_pa_check_accesses(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned int max_nr_accesses = 0;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t) {
			__damon_pa_check_access(r, &ctx->attrs);
			max_nr_accesses = max(r->nr_accesses, max_nr_accesses);
		}
	}

	return max_nr_accesses;
}

#ifdef CONFIG_DAMON_CADDR

/* caddr's access check can reuse almost every pa's access check logic, since
 * r->sampling_addr is a physical address */

static void __damon_ca_check_access(struct damon_region *r,
		struct damon_attrs *attrs)
{
	static unsigned long last_folio_sz = PAGE_SIZE;
	static bool accessed;

	accessed = damon_pa_young(r->sampling_addr, &last_folio_sz);
	damon_update_region_access_rate(r, accessed, attrs);
}

static unsigned int damon_ca_check_accesses(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned int max_nr_accesses = 0;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t) {
			__damon_ca_check_access(r, &ctx->attrs);
			max_nr_accesses = max(r->nr_accesses, max_nr_accesses);
		}
	}

	return max_nr_accesses;
}

#endif

/*
 * damos_pa_filter_out - Return true if the page should be filtered out.
 */
static bool damos_pa_filter_out(struct damos *scheme, struct folio *folio)
{
	struct damos_filter *filter;

	if (scheme->core_filters_allowed)
		return false;

	damos_for_each_ops_filter(filter, scheme) {
		if (damos_folio_filter_match(filter, folio))
			return !filter->allow;
	}
	return scheme->ops_filters_default_reject;
}

static bool damon_pa_invalid_damos_folio(struct folio *folio, struct damos *s)
{
	if (!folio)
		return true;
	if (folio == s->last_applied) {
		folio_put(folio);
		return true;
	}
	return false;
}

static unsigned long damon_pa_pageout(struct damon_region *r, struct damos *s,
		unsigned long *sz_filter_passed)
{
	unsigned long addr, applied;
	LIST_HEAD(folio_list);
	bool install_young_filter = true;
	struct damos_filter *filter;
	struct folio *folio;

	/* check access in page level again by default */
	damos_for_each_ops_filter(filter, s) {
		if (filter->type == DAMOS_FILTER_TYPE_YOUNG) {
			install_young_filter = false;
			break;
		}
	}
	if (install_young_filter) {
		filter = damos_new_filter(
				DAMOS_FILTER_TYPE_YOUNG, true, false);
		if (!filter)
			return 0;
		damos_add_filter(s, filter);
	}

	addr = r->ar.start;
	while (addr < r->ar.end) {
		folio = damon_get_folio(PHYS_PFN(addr));
		if (damon_pa_invalid_damos_folio(folio, s)) {
			addr += PAGE_SIZE;
			continue;
		}

		if (damos_pa_filter_out(s, folio))
			goto put_folio;
		else
			*sz_filter_passed += folio_size(folio);

		folio_clear_referenced(folio);
		folio_test_clear_young(folio);
		if (!folio_isolate_lru(folio))
			goto put_folio;
		if (folio_test_unevictable(folio))
			folio_putback_lru(folio);
		else
			list_add(&folio->lru, &folio_list);
put_folio:
		addr += folio_size(folio);
		folio_put(folio);
	}
	if (install_young_filter)
		damos_destroy_filter(filter);
	applied = reclaim_pages(&folio_list);
	cond_resched();
	s->last_applied = folio;
	return applied * PAGE_SIZE;
}

static inline unsigned long damon_pa_de_activate(
		struct damon_region *r, struct damos *s, bool activate,
		unsigned long *sz_filter_passed)
{
	unsigned long addr, applied = 0;
	struct folio *folio;

	addr = r->ar.start;
	while (addr < r->ar.end) {
		folio = damon_get_folio(PHYS_PFN(addr));
		if (damon_pa_invalid_damos_folio(folio, s)) {
			addr += PAGE_SIZE;
			continue;
		}

		if (damos_pa_filter_out(s, folio))
			goto put_folio;
		else
			*sz_filter_passed += folio_size(folio);

		if (activate)
			folio_activate(folio);
		else
			folio_deactivate(folio);
		applied += folio_nr_pages(folio);
put_folio:
		addr += folio_size(folio);
		folio_put(folio);
	}
	s->last_applied = folio;
	return applied * PAGE_SIZE;
}

static unsigned long damon_pa_activate_pages(struct damon_region *r,
	struct damos *s, unsigned long *sz_filter_passed)
{
	return damon_pa_de_activate(r, s, true, sz_filter_passed);
}

static unsigned long damon_pa_deactivate_pages(struct damon_region *r,
	struct damos *s, unsigned long *sz_filter_passed)
{
	return damon_pa_de_activate(r, s, false, sz_filter_passed);
}

static unsigned long damon_pa_migrate(struct damon_region *r, struct damos *s,
		unsigned long *sz_filter_passed)
{
	unsigned long addr, applied;
	LIST_HEAD(folio_list);
	struct folio *folio;

	addr = r->ar.start;
	while (addr < r->ar.end) {
		folio = damon_get_folio(PHYS_PFN(addr));
		if (damon_pa_invalid_damos_folio(folio, s)) {
			addr += PAGE_SIZE;
			continue;
		}

		if (damos_pa_filter_out(s, folio))
			goto put_folio;
		else
			*sz_filter_passed += folio_size(folio);

		if (!folio_isolate_lru(folio))
			goto put_folio;
		list_add(&folio->lru, &folio_list);
put_folio:
		addr += folio_size(folio);
		folio_put(folio);
	}
	applied = damon_migrate_pages(&folio_list, s->target_nid);
	cond_resched();
	s->last_applied = folio;
	return applied * PAGE_SIZE;
}

static bool damon_pa_scheme_has_filter(struct damos *s)
{
	struct damos_filter *f;

	damos_for_each_ops_filter(f, s)
		return true;
	return false;
}

static unsigned long damon_pa_stat(struct damon_region *r, struct damos *s,
		unsigned long *sz_filter_passed)
{
	unsigned long addr;
	struct folio *folio;

	if (!damon_pa_scheme_has_filter(s))
		return 0;

	addr = r->ar.start;
	while (addr < r->ar.end) {
		folio = damon_get_folio(PHYS_PFN(addr));
		if (damon_pa_invalid_damos_folio(folio, s)) {
			addr += PAGE_SIZE;
			continue;
		}

		if (!damos_pa_filter_out(s, folio))
			*sz_filter_passed += folio_size(folio);
		addr += folio_size(folio);
		folio_put(folio);
	}
	s->last_applied = folio;
	return 0;
}

#ifdef CONFIG_ACMA

static bool damon_pa_preempted(unsigned long pfn)
{
	/* todo: implement */
}

/* always success for preempted=false */
static int damon_pa_set_preempted(unsigned long pfn, bool preempted)
{
	/* todo: implement */
}

/*
 * Return ownership of the memory to the system.  At the moment, only user of
 * this function is virtio-balloon.  They could use page fault-based mechanisms
 * to catch returned ownership.  Therefore this function doesn't notify this
 * event to the report subscribers.  In future, we could add some notification
 * system of this event for more users such as contig memory allocator.
 */
static int damon_pa_free(unsigned long pfn, struct damos *scheme)
{
	if (!damon_pa_preemted(pfn))
		return -EINVAL;

	free_contig_range(pfn, DAMON_MEM_PREEMPT_PAGES);
	damon_pa_set_preempted(pfn, false);
	/*
	 * We intentionally do not report this event to the preempted memory
	 * report subscriber.  They could use page fault handler like
	 * mechanisms.
	 */
	return 0;
}

/*
 * Pass ownership of the memory to page reporting subscribers.  The subscribers
 * can use the reported memory for their purpose, e.g., letting Host
 * re-allocate it to other guest, or use as contig allocation memory pool.
 */
static int damon_pa_alloc(unsigned long pfn, struct damos *scheme)
{
	int err;

	if (damon_pa_preempted(pfn))
		return -EINVAL;
	if (alloc_contig_range(pfn, pfn + DAMON_MEM_PREEMPT_PAGES,
				MIGRATE_MOVABLE, GFP_KERNEL))
		return -ENOMEM;
	err = damon_pa_set_preempted(pfn, true);
	if (err) {
		free_contig_range(pfn, DAMON_MEM_PREEMPT_PAGES);
		return err;
	}
	if (!scheme->alloc_callback)
		return 0;
	err = scheme->alloc_callback(PFN_PHYS(pfn));
	if (err) {
		damon_pa_free(pfn);
		return err;
	}
	return 0;
}

/* Preempt or yield memory regions from system */
static unsigned long damon_pa_alloc_or_free(
		struct damon_region *r, struct damos *s, bool alloc)
{
	unsigned long pfn;
	unsigned long applied = 0;

	for (pfn = PHYS_PFN(r->start); pfn < PHYS_PFN(r->end);
			pfn += DAMON_MEM_PREEMPT_PAGES) {
		if (alloc) {
			if (damon_pa_alloc(pfn, s))
				continue;
		} else {
			if (damon_pa_free(pfn, s))
				continue;
		}
		applied += 1;
	}
	return applied * PAGE_SIZE * DAMON_MEM_PREEMPT_PAGES;
}

#endif

static unsigned long damon_pa_apply_scheme(struct damon_ctx *ctx,
		struct damon_target *t, struct damon_region *r,
		struct damos *scheme, unsigned long *sz_filter_passed)
{
	switch (scheme->action) {
	case DAMOS_PAGEOUT:
		return damon_pa_pageout(r, scheme, sz_filter_passed);
	case DAMOS_LRU_PRIO:
		return damon_pa_activate_pages(r, scheme, sz_filter_passed);
	case DAMOS_LRU_DEPRIO:
		return damon_pa_deactivate_pages(r, scheme, sz_filter_passed);
	case DAMOS_MIGRATE_HOT:
	case DAMOS_MIGRATE_COLD:
		return damon_pa_migrate(r, scheme, sz_filter_passed);
#ifdef CONFIG_ACMA
	case DAMOS_ALLOC:
		return damon_pa_alloc_or_free(r, scheme, true);
	case DAMOS_FREE:
		return damon_pa_alloc_or_free(r, scheme, false);
#endif
	case DAMOS_STAT:
		return damon_pa_stat(r, scheme, sz_filter_passed);
	default:
		/* DAMOS actions that not yet supported by 'paddr'. */
		break;
	}
	return 0;
}

static int damon_pa_scheme_score(struct damon_ctx *context,
		struct damon_target *t, struct damon_region *r,
		struct damos *scheme)
{
	switch (scheme->action) {
	case DAMOS_PAGEOUT:
		return damon_cold_score(context, r, scheme);
	case DAMOS_LRU_PRIO:
		return damon_hot_score(context, r, scheme);
	case DAMOS_LRU_DEPRIO:
		return damon_cold_score(context, r, scheme);
	case DAMOS_MIGRATE_HOT:
		return damon_hot_score(context, r, scheme);
	case DAMOS_MIGRATE_COLD:
		return damon_cold_score(context, r, scheme);
	default:
		break;
	}

	return DAMOS_MAX_SCORE;
}

static int __init damon_pa_initcall(void)
{
	struct damon_operations ops = {
		.id = DAMON_OPS_PADDR,
		.init = NULL,
		.update = NULL,
		.prepare_access_checks = damon_pa_prepare_access_checks,
		.check_accesses = damon_pa_check_accesses,
		.target_valid = NULL,
		.cleanup = NULL,
		.apply_scheme = damon_pa_apply_scheme,
		.get_scheme_score = damon_pa_scheme_score,
	};
#ifdef CONFIG_DAMON_CADDR
	struct damon_operations cops = {
		.id = DAMON_OPS_CADDR,
		.prepare_access_checks = damon_ca_prepare_access_checks,
		.check_accesses = damon_ca_check_accesses,
	};

	cache_spec = damon_ca_mk_cache_spec(
			128 * 1024 * 1024, 64, 16,
			4UL * 1024 * 1024 * 1024, 64UL * 1024 * 1024 * 1024);
	damon_register_ops(&cops);
#endif

	return damon_register_ops(&ops);
};

subsys_initcall(damon_pa_initcall);
