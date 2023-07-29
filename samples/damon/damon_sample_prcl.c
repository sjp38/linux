// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "ksdemo: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init ksdemo_init(void)
{
	pr_info("Hello Kernel Summit 2021\n");
	return 0;
}

static void __exit ksdemo_exit(void)
{
	pr_info("Goodbye Kernel Summit 2021\n");
}

module_init(ksdemo_init);
module_exit(ksdemo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SeongJae Park");
MODULE_DESCRIPTION("Kernel Summit 2021 live coding demo");
