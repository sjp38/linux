// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/kobject.h>
#include <linux/slab.h>

struct damon_kobject {
	struct kobject kobj;
	bool monitor_on;
};

static ssize_t monitor_on_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_kobject *damon_kobj = container_of(kobj,
			struct damon_kobject, kobj);

	return sysfs_emit(buf, "%s\n", damon_kobj->monitor_on ? "on" : "off");
}

static ssize_t monitor_on_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_kobject *damon_kobj = container_of(kobj,
			struct damon_kobject, kobj);
	bool on, err;

	err = kstrtobool(buf, &on);
	if (err)
		return err;

	damon_kobj->monitor_on = on;
	return count;
}

static struct kobj_attribute monitor_on_attr = __ATTR(monitor_on, 0600,
		monitor_on_show, monitor_on_store);

static void damon_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_kobject, kobj));
}

static struct attribute *damon_attrs[] = {
	&monitor_on_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(damon);

static struct kobj_type damon_ktype = {
	.release = damon_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_groups,
};

static int __init damon_sysfs_init(void)
{
	struct damon_kobject *damon_kobj;
	int err;

	damon_kobj = kzalloc(sizeof(*damon_kobj), GFP_KERNEL);
	if (!damon_kobj)
		return -ENOMEM;
	err = kobject_init_and_add(&damon_kobj->kobj, &damon_ktype, mm_kobj,
			"damon");
	if (err) {
		kobject_put(&damon_kobj->kobj);
		return err;
	}

	return 0;
}
subsys_initcall(damon_sysfs_init);
