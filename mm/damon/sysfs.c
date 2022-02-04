// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/kobject.h>
#include <linux/slab.h>

/*
 * monitoring_attrs directory
 */

struct damon_attrs_kobj {
	struct kobject kobj;
	unsigned long sample_interval_us;
	unsigned long aggr_interval_us;
	unsigned long primitive_update_interval_us;
	unsigned long min_nr_regions;
	unsigned long max_nr_regions;
};

static ssize_t damon_attrs_sampling_interval_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);

	return sysfs_emit(buf, "%lu\n", damon_attrs_kobj->sample_interval_us);
}

static ssize_t damon_attrs_sampling_interval_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	damon_attrs_kobj->sample_interval_us = us;
	return count;
}

static ssize_t damon_attrs_aggregation_interval_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);

	return sysfs_emit(buf, "%lu\n", damon_attrs_kobj->aggr_interval_us);
}

static ssize_t damon_attrs_aggregation_interval_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	damon_attrs_kobj->aggr_interval_us = us;
	return count;
}

static ssize_t damon_attrs_prmtv_update_interval_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);

	return sysfs_emit(buf, "%lu\n",
			damon_attrs_kobj->primitive_update_interval_us);
}

static ssize_t damon_attrs_prmtv_update_interval_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	damon_attrs_kobj->primitive_update_interval_us = us;
	return count;
}

static ssize_t damon_attrs_min_nr_regions_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);

	return sysfs_emit(buf, "%lu\n",
			damon_attrs_kobj->min_nr_regions);
}

static ssize_t damon_attrs_min_nr_regions_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	damon_attrs_kobj->min_nr_regions = us;
	return count;
}

static ssize_t damon_attrs_max_nr_regions_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);

	return sysfs_emit(buf, "%lu\n",
			damon_attrs_kobj->max_nr_regions);
}

static ssize_t damon_attrs_max_nr_regions_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_attrs_kobj *damon_attrs_kobj = container_of(kobj,
			struct damon_attrs_kobj, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	damon_attrs_kobj->max_nr_regions = us;
	return count;
}

static void damon_attrs_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_attrs_kobj, kobj));
}

static void damon_attrs_kobj_remove_childs(
		struct damon_attrs_kobj *damon_attrs_kobj)
{
	return;
}

static struct kobj_attribute damon_attrs_sample_interval_us_attr =
		__ATTR(sample_interval_us, 0600,
				damon_attrs_sampling_interval_us_show,
				damon_attrs_sampling_interval_us_store);

static struct kobj_attribute damon_attrs_aggr_interval_us_attr =
		__ATTR(aggr_interval_us, 0600,
				damon_attrs_aggregation_interval_us_show,
				damon_attrs_aggregation_interval_us_store);

static struct kobj_attribute damon_attrs_update_interval_us_attr =
		__ATTR(update_interval_us, 0600,
				damon_attrs_prmtv_update_interval_us_show,
				damon_attrs_prmtv_update_interval_us_store);

static struct kobj_attribute damon_attrs_min_nr_regions_attr =
		__ATTR(min_nr_regions, 0600,
				damon_attrs_min_nr_regions_show,
				damon_attrs_min_nr_regions_store);

static struct kobj_attribute damon_attrs_max_nr_regions_attr =
		__ATTR(max_nr_regions, 0600,
				damon_attrs_max_nr_regions_show,
				damon_attrs_max_nr_regions_store);

static struct attribute *damon_attrs_attrs[] = {
	&damon_attrs_sample_interval_us_attr.attr,
	&damon_attrs_aggr_interval_us_attr.attr,
	&damon_attrs_update_interval_us_attr.attr,
	&damon_attrs_min_nr_regions_attr.attr,
	&damon_attrs_max_nr_regions_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_attrs);

static struct kobj_type damon_attrs_ktype = {
	.release = damon_attrs_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_attrs_groups,
};

/*
 * context directory
 * -----------------
 */

enum damon_prmtv_set {
	DAMON_PRMTV_VADDR,
	DAMON_PRMTV_PADDR,
};

static const char * const damon_prmtv_set_strs[] = {
	"vaddr",
	"paddr",
};

struct context_kobj {
	struct kobject kobj;
	enum damon_prmtv_set prmtv_set;
	struct damon_attrs_kobj *damon_attrs_kobj;
};

static ssize_t context_prmtv_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct context_kobj *context_kobj = container_of(kobj,
			struct context_kobj, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_prmtv_set_strs[context_kobj->prmtv_set]);
}

static ssize_t context_prmtv_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct context_kobj *context_kobj = container_of(kobj,
			struct context_kobj, kobj);

	if (!strncmp(buf, "vaddr\n", count))
		context_kobj->prmtv_set = DAMON_PRMTV_VADDR;
	else if (!strncmp(buf, "paddr\n", count))
		context_kobj->prmtv_set = DAMON_PRMTV_PADDR;
	else
		return -EINVAL;

	return count;
}

static void context_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct context_kobj, kobj));
}

static void context_kobj_remove_childs(struct context_kobj *context_kobj)
{
	damon_attrs_kobj_remove_childs(context_kobj->damon_attrs_kobj);
	kobject_put(&context_kobj->damon_attrs_kobj->kobj);
	return;
}

static struct kobj_attribute context_prmtv_attr = __ATTR(prmtv, 0600,
		context_prmtv_show, context_prmtv_store);

static struct attribute *context_attrs[] = {
	&context_prmtv_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(context);

static struct kobj_type context_ktype = {
	.release = context_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = context_groups,
};

/*
 * contexts directory
 * ------------------
 */

struct contexts_kobj {
	struct kobject kobj;
	struct context_kobj **context_kobjs;
	int nr;
};

static ssize_t contexts_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct contexts_kobj *contexts_kobj = container_of(kobj,
			struct contexts_kobj, kobj);

	return sysfs_emit(buf, "%d\n", contexts_kobj->nr);
}

static void contexts_kobj_remove_childs(struct contexts_kobj *contexts_kobj)
{
	struct context_kobj ** context_kobjs = contexts_kobj->context_kobjs;
	int i;

	for (i = 0; i < contexts_kobj->nr; i++) {
		context_kobj_remove_childs(context_kobjs[i]);
		kobject_put(&context_kobjs[i]->kobj);
	}
	kfree(context_kobjs);
	contexts_kobj->context_kobjs = NULL;
	contexts_kobj->nr = 0;
}

static ssize_t contexts_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct contexts_kobj *contexts_kobj = container_of(kobj,
			struct contexts_kobj, kobj);
	struct context_kobj **context_kobjs, *context_kobj;
	struct damon_attrs_kobj *damon_attrs_kobj;
	int nr, err;
	int i;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	contexts_kobj_remove_childs(contexts_kobj);

	context_kobjs = kmalloc(sizeof(*context_kobjs) * nr, GFP_KERNEL);
	contexts_kobj->context_kobjs = context_kobjs;
	for (i = 0; i < nr; i++) {
		context_kobj = kzalloc(sizeof(*context_kobj), GFP_KERNEL);
		if (!context_kobj) {
			contexts_kobj_remove_childs(contexts_kobj);
			return -ENOMEM;
		}
		err = kobject_init_and_add(&context_kobj->kobj, &context_ktype,
				&contexts_kobj->kobj, "%d", i);
		if (err) {
			contexts_kobj_remove_childs(contexts_kobj);
			kfree(context_kobj);
			return err;
		}

		damon_attrs_kobj = kzalloc(sizeof(*damon_attrs_kobj),
				GFP_KERNEL);
		if (!damon_attrs_kobj) {
			contexts_kobj_remove_childs(contexts_kobj);
			return -ENOMEM;
		}
		err = kobject_init_and_add(&damon_attrs_kobj->kobj,
				&damon_attrs_ktype, &context_kobj->kobj,
				"monitoring_attrs");
		if (err) {
			contexts_kobj_remove_childs(contexts_kobj);
			kfree(damon_attrs_kobj);
			return err;
		}
		context_kobj->damon_attrs_kobj = damon_attrs_kobj;

		context_kobjs[i] = context_kobj;
		contexts_kobj->nr++;
	}

	return count;
}

static void contexts_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct contexts_kobj, kobj));
}

static struct kobj_attribute contexts_nr_attr = __ATTR(nr, 0600,
		contexts_nr_show, contexts_nr_store);

static struct attribute *contexts_attrs[] = {
	&contexts_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(contexts);

static struct kobj_type contexts_ktype = {
	.release = contexts_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = contexts_groups,
};

/*
 * kdamond directory
 * -----------------
 */

struct kdamond_kobj {
	struct kobject kobj;
	struct contexts_kobj *contexts_kobj;
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

static void kdamond_kobj_remove_childs(struct kdamond_kobj *kdamond_kobj)
{
	contexts_kobj_remove_childs(kdamond_kobj->contexts_kobj);
	kobject_put(&kdamond_kobj->contexts_kobj->kobj);
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

	for (i = 0; i < kdamonds_kobj->nr; i++) {
		kdamond_kobj_remove_childs(kdamond_kobjs[i]);
		kobject_put(&kdamond_kobjs[i]->kobj);
	}
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
	struct contexts_kobj *contexts_kobj;
	int nr, err;
	int i;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	kdamonds_kobj_remove_childs(kdamonds_kobj);
	if (!nr)
		return count;

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

		contexts_kobj = kzalloc(sizeof(*contexts_kobj), GFP_KERNEL);
		if (!contexts_kobj) {
			kdamonds_kobj_remove_childs(kdamonds_kobj);
			return -ENOMEM;
		}
		err = kobject_init_and_add(&contexts_kobj->kobj,
				&contexts_ktype, &kdamond_kobj->kobj,
				"contexts");
		if (err) {
			kdamonds_kobj_remove_childs(kdamonds_kobj);
			kfree(contexts_kobj);
			return err;
		}
		kdamond_kobj->contexts_kobj = contexts_kobj;

		kdamond_kobjs[i] = kdamond_kobj;
		kdamonds_kobj->nr++;
	}

	return count;
}

static void kdamonds_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct kdamonds_kobj, kobj));
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
