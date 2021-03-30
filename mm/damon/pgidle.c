// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON Primitives for Page Granularity Idleness Monitoring
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon-pgi: " fmt

#include <linux/rmap.h>

#include "prmtv-common.h"

#include <trace/events/damon.h>

bool damon_pgi_is_idle(unsigned long pfn, unsigned long *pg_size)
{
	return damon_pa_young(PFN_PHYS(pfn), pg_size);
}

/*
 * This has no implementations for 'init()' and 'update()'.  Users should set
 * the initial regions and update regions by themselves in the 'before_start'
 * and 'after_aggregation' callbacks, respectively.  Or, they can implement and
 * use their own version of the primitives.
 */

void damon_pgi_prepare_access_checks(struct damon_ctx *ctx)
{
	struct damon_pfns_range *target = ctx->arbitrary_target;
	unsigned long pfn;

	for (pfn = target->start; pfn < target->end; pfn++)
		damon_pa_mkold(PFN_PHYS(pfn));
}

unsigned int damon_pgi_check_accesses(struct damon_ctx *ctx)
{
	struct damon_pfns_range *target = ctx->arbitrary_target;
	unsigned long pfn;
	unsigned long pg_size = 0;

	for (pfn = target->start; pfn < target->end; pfn++) {
		pg_size = 0;
		trace_damon_pgi(pfn, damon_pa_young(PFN_PHYS(pfn), &pg_size));
		if (pg_size > PAGE_SIZE)
			pfn += pg_size / PAGE_SIZE - 1;
	}

	return 0;
}

bool damon_pgi_target_valid(void *target)
{
	return true;
}

void damon_pgi_set_primitives(struct damon_ctx *ctx)
{
	ctx->primitive.init = NULL;
	ctx->primitive.update = NULL;
	ctx->primitive.prepare_access_checks = damon_pgi_prepare_access_checks;
	ctx->primitive.check_accesses = damon_pgi_check_accesses;
	ctx->primitive.reset_aggregated = NULL;
	ctx->primitive.target_valid = damon_pgi_target_valid;
	ctx->primitive.cleanup = NULL;
	ctx->primitive.apply_scheme = NULL;
}
