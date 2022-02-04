// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/kobject.h>
#include <linux/slab.h>

/*
 * kdamond directory
 * -----------------
 */

struct kdamond_kobj {
	struct kobject kobj;
	int pid;
};

static ssize_t kdamond_pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	/* TODO: get pid of the kdamond and show */
	return sysfs_emit(buf, "%d\n", 42);
}

static void kdamond_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct kdamond_kobj, kobj));
}

static struct kobj_attribute kdamond_pid_attr = __ATTR(pid, 0400,
		kdamond_pid_show, NULL);

static struct attribute *kdamond_attrs[] = {
	&kdamond_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(kdamond);

static struct kobj_type kdamond_ktype = {
	.release = kdamond_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = kdamond_groups,
};

/*
 * kdamonds directory
 * ------------------
 */

struct kdamonds_kobj {
	struct kobject kobj;
	struct kdamond_kobj **kdamond_kobjs;
	int nr;
};

static ssize_t kdamonds_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct kdamonds_kobj *kdamonds_kobj = container_of(kobj,
			struct kdamonds_kobj, kobj);

	return sysfs_emit(buf, "%d\n", kdamonds_kobj->nr);
}

static void kdamonds_kobj_remove_childs(struct kdamonds_kobj *kdamonds_kobj)
{
	struct kdamond_kobj ** kdamond_kobjs = kdamonds_kobj->kdamond_kobjs;
	int i;

	for (i = 0; i < kdamonds_kobj->nr; i++)
		kobject_put(&kdamond_kobjs[i]->kobj);
	if (kdamond_kobjs)
		kfree(kdamond_kobjs);
	kdamonds_kobj->kdamond_kobjs = NULL;
	kdamonds_kobj->nr = 0;
}

static ssize_t kdamonds_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct kdamonds_kobj *kdamonds_kobj = container_of(kobj,
			struct kdamonds_kobj, kobj);
	struct kdamond_kobj **kdamond_kobjs, *kdamond_kobj;
	int nr, err;
	int i;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	kdamonds_kobj_remove_childs(kdamonds_kobj);

	kdamond_kobjs = kmalloc(sizeof(*kdamond_kobjs) * nr, GFP_KERNEL);
	kdamonds_kobj->kdamond_kobjs = kdamond_kobjs;
	for (i = 0; i < nr; i++) {
		kdamond_kobj = kzalloc(sizeof(*kdamond_kobj), GFP_KERNEL);
		if (!kdamond_kobj) {
			kdamonds_kobj_remove_childs(kdamonds_kobj);
			return -ENOMEM;
		}
		err = kobject_init_and_add(&kdamond_kobj->kobj, &kdamond_ktype,
				&kdamonds_kobj->kobj, "%d", i);
		if (err) {
			kdamonds_kobj_remove_childs(kdamonds_kobj);
			kfree(kdamond_kobj);
			return err;
		}

		kdamond_kobjs[i] = kdamond_kobj;
		kdamonds_kobj->nr++;
	}

	return count;
}

static void kdamonds_kobj_release(struct kobject *kobj)
{
	struct kdamonds_kobj *kdamonds_kobj = container_of(kobj,
			struct kdamonds_kobj, kobj);

	kdamonds_kobj_remove_childs(kdamonds_kobj);
	kfree(kdamonds_kobj);
}

static struct kobj_attribute kdamonds_nr_attr = __ATTR(nr, 0600,
		kdamonds_nr_show, kdamonds_nr_store);

static struct attribute *kdamonds_attrs[] = {
	&kdamonds_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(kdamonds);

static struct kobj_type kdamonds_ktype = {
	.release = kdamonds_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = kdamonds_groups,
};

/*
 * damon directory (root)
 * ----------------------
 */

struct damon_kobj {
	struct kobject kobj;
	bool monitor_on;
};

static ssize_t monitor_on_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_kobj *damon_kobj = container_of(kobj,
			struct damon_kobj, kobj);

	return sysfs_emit(buf, "%s\n", damon_kobj->monitor_on ? "on" : "off");
}

static ssize_t monitor_on_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_kobj *damon_kobj = container_of(kobj,
			struct damon_kobj, kobj);
	bool on, err;

	err = kstrtobool(buf, &on);
	if (err)
		return err;

	damon_kobj->monitor_on = on;
	return count;
}

static void damon_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_kobj, kobj));
}

static struct kobj_attribute monitor_on_attr = __ATTR(monitor_on, 0600,
		monitor_on_show, monitor_on_store);

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
	struct damon_kobj *damon_kobj;
	struct kdamonds_kobj *kdamonds_kobj;
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

	kdamonds_kobj = kzalloc(sizeof(*kdamonds_kobj), GFP_KERNEL);
	if (!kdamonds_kobj) {
		kobject_put(&damon_kobj->kobj);
		return -ENOMEM;
	}
	err = kobject_init_and_add(&kdamonds_kobj->kobj, &kdamonds_ktype,
			&damon_kobj->kobj, "kdamonds");
	if (err) {
		kobject_put(&kdamonds_kobj->kobj);
		kobject_put(&damon_kobj->kobj);
		return err;
	}

	return 0;
}
subsys_initcall(damon_sysfs_init);
