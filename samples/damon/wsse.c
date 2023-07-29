// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "damon_sample_wsse: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init damon_sample_wsse_init(void)
{
	pr_info("Hello\n");
	return 0;
}

static void __exit damon_sample_wsse_exit(void)
{
	pr_info("Goodbye\n");
}

module_init(damon_sample_wsse_init);
module_exit(damon_sample_wsse_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park");
MODULE_DESCRIPTION("DAMON sample module for working set size estimation");
