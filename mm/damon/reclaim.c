// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON-based page reclamation
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon-reclaim: " fmt

#include <linux/damon.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "damon_reclaim."

static bool enabled __read_mostly = CONFIG_DAMON_RECLAIM_ENABLE;
module_param(enabled, bool, 0600);

static unsigned long min_age __read_mostly = CONFIG_DAMON_RECLAIM_MIN_AGE;
module_param(min_age, ulong, 0600);

static unsigned long limit_sz __read_mostly = CONFIG_DAMON_RECLAIM_LIMIT_SZ;
module_param(limit_sz, ulong, 0600);

static unsigned long limit_ms __read_mostly = CONFIG_DAMON_RECLAIM_LIMIT_MS;
module_param(limit_ms, ulong, 0600);

static unsigned long wmarks_interval __read_mostly =
		CONFIG_DAMON_RECLAIM_WATERMARK_CHECK_INTERVAL;
module_param(wmarks_interval, ulong, 0600);

static unsigned long wmarks_high __read_mostly =
		CONFIG_DAMON_RECLAIM_WATERMARK_HIGH;
module_param(wmarks_high, ulong, 0600);

static unsigned long wmarks_mid __read_mostly =
		CONFIG_DAMON_RECLAIM_WATERMARK_MID;
module_param(wmarks_mid, ulong, 0600);

static unsigned long wmarks_low __read_mostly =
		CONFIG_DAMON_RECLAIM_WATERMARK_LOW;
module_param(wmarks_low, ulong, 0600);

static unsigned long sample_interval __read_mostly =
		CONFIG_DAMON_RECLAIM_SAMPLING_INTERVAL;
module_param(sample_interval, ulong, 0600);

static unsigned long aggr_interval __read_mostly =
		CONFIG_DAMON_RECLAIM_AGGREGATION_INTERVAL;
module_param(aggr_interval, ulong, 0600);

static unsigned long min_nr_regions __read_mostly =
		CONFIG_DAMON_RECLAIM_MIN_NR_REGIONS;
module_param(min_nr_regions, ulong, 0600);

static unsigned long max_nr_regions __read_mostly =
		CONFIG_DAMON_RECLAIM_MAX_NR_REGIONS;
module_param(max_nr_regions, ulong, 0600);

static unsigned long monitor_region_start;
module_param(monitor_region_start, ulong, 0400);

static unsigned long monitor_region_end;
module_param(monitor_region_end, ulong, 0400);

static struct damon_ctx *ctx;
static struct damon_target *target;

struct damon_reclaim_ram_walk_arg {
	unsigned long start;
	unsigned long end;
};

int walk_system_ram(struct resource *res, void *arg)
{
	struct damon_reclaim_ram_walk_arg *a = arg;

	if (a->end - a->start < res->end - res->start) {
		a->start = res->start;
		a->end = res->end;
	}
	return 0;
}

/*
 * Find biggest 'System RAM' resource and store its start and end address in
 * @start and @end, respectively.  If no System RAM is found, returns false.
 */
static bool get_monitoring_region(unsigned long *start, unsigned long *end)
{
	struct damon_reclaim_ram_walk_arg arg = {};

	walk_system_ram_res(0, ULONG_MAX, &arg, walk_system_ram);

	if (arg.end > arg.start) {
		*start = arg.start;
		*end = arg.end;
		return true;
	}


	return false;
}

static struct damos *damon_reclaim_new_scheme(void)
{
	struct damos_watermarks wmarks = {
		.metric = DAMOS_WMARK_FREE_MEM_RATE,
		.interval = wmarks_interval,
		.high = wmarks_high,
		.mid = wmarks_mid,
		.low = wmarks_low,
	};
	struct damos_speed_limit limit = {
		.sz = limit_sz,
		.ms = limit_ms,
		/* Within the limit, page out older regions first. */
		.weight_sz = 0,
		.weight_nr_accesses = 0,
		.weight_age = 1
	};
	struct damos *scheme = damon_new_scheme(
			/* Find regions having PAGE_SIZE or larger size */
			PAGE_SIZE, ULONG_MAX,
			/* and not accessed at all */
			0, 0,
			/* for min_age or more micro-seconds, and */
			min_age / aggr_interval, UINT_MAX,
			/* page out those, as soon as found */
			DAMOS_PAGEOUT,
			&limit,
			/* Activate this based on the watermarks. */
			&wmarks);

	return scheme;
}

static int damon_reclaim_turn(bool on)
{
	struct damon_region *region;
	struct damos *scheme;
	int err;

	if (!on)
		return damon_stop(&ctx, 1);

	err = damon_set_attrs(ctx, READ_ONCE(sample_interval),
			READ_ONCE(aggr_interval),
			READ_ONCE(aggr_interval) * 100,
			min_nr_regions, max_nr_regions);
	if (err)
		return err;

	if (!get_monitoring_region(&monitor_region_start, &monitor_region_end))
		return -EINVAL;
	/* DAMON will free this on its own when finish monitoring */
	region = damon_new_region(monitor_region_start, monitor_region_end);
	if (!region)
		return -ENOMEM;
	damon_add_region(region, target);

	/* Will be freed by later 'damon_set_schemes()' */
	scheme = damon_reclaim_new_scheme();
	if (!scheme)
		goto free_region_out;
	err = damon_set_schemes(ctx, &scheme, 1);
	if (err)
		goto free_scheme_out;

	err = damon_start(&ctx, 1);
	if (err)
		goto free_scheme_out;
	goto out;

free_scheme_out:
	damon_destroy_scheme(scheme);
free_region_out:
	damon_destroy_region(region);
out:
	return err;
}

#define ENABLE_CHECK_INTERVAL_MS	1000
static struct delayed_work damon_reclaim_timer;
static void damon_reclaim_timer_fn(struct work_struct *work)
{
	static bool last_enabled;
	bool now_enabled;

	now_enabled = enabled;
	if (last_enabled != now_enabled) {
		if (!damon_reclaim_turn(now_enabled))
			last_enabled = now_enabled;
		else
			enabled = last_enabled;
	}

	schedule_delayed_work(&damon_reclaim_timer,
			msecs_to_jiffies(ENABLE_CHECK_INTERVAL_MS));
}
static DECLARE_DELAYED_WORK(damon_reclaim_timer, damon_reclaim_timer_fn);

static int __init damon_reclaim_init(void)
{
	ctx = damon_new_ctx(DAMON_ADAPTIVE_TARGET);
	if (!ctx)
		return -ENOMEM;

	damon_pa_set_primitives(ctx);

	target = damon_new_target(4242);
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);

	schedule_delayed_work(&damon_reclaim_timer, 0);
	return 0;
}

module_init(damon_reclaim_init);
