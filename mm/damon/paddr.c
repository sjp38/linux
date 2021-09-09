// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON Primitives for The Physical Address Space
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-pa: " fmt

#include "prmtv-common.h"

/*
 * This has no implementations for 'init_target_regions()' and
 * 'update_target_regions()'.  Users should set the initial regions and update
 * regions by themselves in the 'before_start' and 'after_aggregation'
 * callbacks, respectively.  Or, they can implement and use their own version
 * of the primitives.
 */

static void __damon_pa_prepare_access_check(struct damon_ctx *ctx,
					    struct damon_region *r)
{
	r->sampling_addr = damon_rand(r->ar.start, r->ar.end);

	damon_pa_mkold(r->sampling_addr);
}

void damon_pa_prepare_access_checks(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t)
			__damon_pa_prepare_access_check(ctx, r);
	}
}

/*
 * Check whether the region was accessed after the last preparation
 *
 * mm	'mm_struct' for the given virtual address space
 * r	the region of physical address space that needs to be checked
 */
static void __damon_pa_check_access(struct damon_ctx *ctx,
				    struct damon_region *r)
{
	static unsigned long last_addr;
	static unsigned long last_page_sz = PAGE_SIZE;
	static bool last_accessed;

	/* If the region is in the last checked page, reuse the result */
	if (ALIGN_DOWN(last_addr, last_page_sz) ==
				ALIGN_DOWN(r->sampling_addr, last_page_sz)) {
		if (last_accessed)
			r->nr_accesses++;
		return;
	}

	last_accessed = damon_pa_young(r->sampling_addr, &last_page_sz);
	if (last_accessed)
		r->nr_accesses++;

	last_addr = r->sampling_addr;
}

unsigned int damon_pa_check_accesses(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned int max_nr_accesses = 0;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t) {
			__damon_pa_check_access(ctx, r);
			max_nr_accesses = max(r->nr_accesses, max_nr_accesses);
		}
	}

	return max_nr_accesses;
}

bool damon_pa_target_valid(void *t)
{
	return true;
}

void damon_pa_set_primitives(struct damon_ctx *ctx)
{
	ctx->primitive.init = NULL;
	ctx->primitive.update = NULL;
	ctx->primitive.prepare_access_checks = damon_pa_prepare_access_checks;
	ctx->primitive.check_accesses = damon_pa_check_accesses;
	ctx->primitive.reset_aggregated = NULL;
	ctx->primitive.target_valid = damon_pa_target_valid;
	ctx->primitive.cleanup = NULL;
	ctx->primitive.apply_scheme = NULL;
}
