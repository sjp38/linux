// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "ksdemo: " fmt

#include <linux/damon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pid.h>

static int target_pid __read_mostly;
module_param(target_pid, int, 0600);

struct damon_ctx *ctx;
struct pid *target_pidp;

static int ksdemo_after_aggregation(struct damon_ctx *c)
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

static int __init ksdemo_init(void)
{
	struct damon_target *target;

	pr_info("Hello Kernel Summit 2021\n");

	/* allocate context */
	ctx = damon_new_ctx(DAMON_ADAPTIVE_TARGET);
	if (!ctx)
		return -ENOMEM;
	/* specify that we want to monitor virtual address space */
	damon_va_set_primitives(ctx);
	/* specify what process's virtual address space we want to monitor */
	target_pidp = find_get_pid(target_pid);
	if (!target_pidp)
		return -EINVAL;
	target = damon_new_target((unsigned long)target_pidp);
	if (!target)
		return -ENOMEM;
	damon_add_target(ctx, target);
	/* register callback for reading results */
	ctx->callback.after_aggregation = ksdemo_after_aggregation;
	/* start the monitoring */
	return damon_start(&ctx, 1);
}

static void __exit ksdemo_exit(void)
{
	if (ctx) {
		damon_stop(&ctx, 1);
		damon_destroy_ctx(ctx);
	}
	if (target_pidp)
		put_pid(target_pidp);
	pr_info("Goodbye Kernel Summit 2021\n");
}

module_init(ksdemo_init);
module_exit(ksdemo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park");
MODULE_DESCRIPTION("Kernel Summit 2021 live coding demo");
