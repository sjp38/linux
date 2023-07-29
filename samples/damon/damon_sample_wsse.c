// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "damon_sample_wsse: " fmt

#include <linux/damon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int target_pid __read_mostly;
module_param(target_pid, int, 0600);

static struct damon_ctx *ctx;
static struct pid *target_pidp;

static int damon_sample_wsse_after_aggregate(struct damon_ctx *c)
{
	struct damon_target *t;

	damon_for_each_target(t, c) {
		struct damon_region *r;
		unsigned long wss = 0;

		damon_for_each_region(r, t) {
			if (r->nr_accesses > 0)
				wss += r->ar.end - r->ar.start;
		}
		pr_info("wss: %lu\n", wss);
	}
	return 0;
}

static int __init damon_sample_wsse_init(void)
{
	struct damon_target *target;

	pr_info("Hello\n");

	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;
	if (damon_select_ops(ctx, DAMON_OPS_VADDR)) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}

	target = damon_new_target();
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);
	target_pidp = find_get_pid(target_pid);
	if (!target_pidp) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}
	target->pid = target_pidp;

	ctx->callback.after_aggregation = damon_sample_wsse_after_aggregate;
	return damon_start(&ctx, 1, true);
}

static void __exit damon_sample_wsse_exit(void)
{
	pr_info("Goodbye\n");
	if (ctx) {
		damon_stop(&ctx, 1);
		damon_destroy_ctx(ctx);
	}
	if (target_pidp)
		put_pid(target_pidp);
}

module_init(damon_sample_wsse_init);
module_exit(damon_sample_wsse_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park");
MODULE_DESCRIPTION("DAMON sample module for working set size estimation");
