// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/kobject.h>
#include <linux/slab.h>

/*
 * unsigned long range
 * -------------------
 */

struct damon_sysfs_ul_range {
	struct kobject kobj;
	unsigned long min;
	unsigned long max;
};

static void damon_sysfs_ul_range_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_ul_range, kobj));
}

static ssize_t damon_sysfs_ul_range_min_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);

	return sysfs_emit(buf, "%lu\n", range->min);
}

static ssize_t damon_sysfs_ul_range_min_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);
	unsigned long min;
	int err;

	err = kstrtoul(buf, 10, &min);
	if (err)
		return -EINVAL;

	range->min = min;
	return count;
}

static ssize_t damon_sysfs_ul_range_max_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);

	return sysfs_emit(buf, "%lu\n", range->max);
}

static ssize_t damon_sysfs_ul_range_max_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);
	unsigned long max;
	int err;

	err = kstrtoul(buf, 10, &max);
	if (err)
		return -EINVAL;

	range->max = max;
	return count;
}

static struct kobj_attribute damon_sysfs_ul_range_min_attr =
		__ATTR(min, 0600, damon_sysfs_ul_range_min_show,
				damon_sysfs_ul_range_min_store);

static struct kobj_attribute damon_sysfs_ul_range_max_attr =
		__ATTR(max, 0600, damon_sysfs_ul_range_max_show,
				damon_sysfs_ul_range_max_store);

static struct attribute *damon_sysfs_ul_range_attrs[] = {
	&damon_sysfs_ul_range_min_attr.attr,
	&damon_sysfs_ul_range_max_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_ul_range);

static struct kobj_type damon_sysfs_ul_range_ktype = {
	.release = damon_sysfs_ul_range_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_ul_range_groups,
};

/*
 * monitoring_attrs directory
 * --------------------------
 */

struct damon_sysfs_attrs {
	struct kobject kobj;
	unsigned long sample_interval_us;
	unsigned long aggr_interval_us;
	unsigned long primitive_update_interval_us;
	struct damon_sysfs_ul_range *nr_regions;
};

static int damon_sysfs_attrs_add_files(struct damon_sysfs_attrs *attrs)
{
	struct damon_sysfs_ul_range *nr_regions_range;
	int err;

	nr_regions_range = kzalloc(sizeof(*nr_regions_range), GFP_KERNEL);
	if (!nr_regions_range)
		return -ENOMEM;

	err = kobject_init_and_add(&nr_regions_range->kobj,
			&damon_sysfs_ul_range_ktype, &attrs->kobj,
			"nr_regions");
	if (err) {
		kfree(nr_regions_range);
		return err;
	}
	attrs->nr_regions = nr_regions_range;

	return err;
}

static void damon_sysfs_attrs_rm_files(struct damon_sysfs_attrs *attrs)
{
	kobject_put(&attrs->nr_regions->kobj);
}

static ssize_t damon_sysfs_attrs_sampling_interval_us_show(
		struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_attrs *attrs = container_of(kobj,
			struct damon_sysfs_attrs, kobj);

	return sysfs_emit(buf, "%lu\n", attrs->sample_interval_us);
}

static ssize_t damon_sysfs_attrs_sampling_interval_us_store(
		struct kobject *kobj, struct kobj_attribute *attr, const char
		*buf, size_t count)
{
	struct damon_sysfs_attrs *attrs = container_of(kobj,
			struct damon_sysfs_attrs, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	attrs->sample_interval_us = us;
	return count;
}

static ssize_t damon_sysfs_attrs_aggregation_interval_us_show(
		struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_attrs *attrs = container_of(kobj,
			struct damon_sysfs_attrs, kobj);

	return sysfs_emit(buf, "%lu\n", attrs->aggr_interval_us);
}

static ssize_t damon_sysfs_attrs_aggregation_interval_us_store(
		struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_attrs *attrs = container_of(kobj,
			struct damon_sysfs_attrs, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	attrs->aggr_interval_us = us;
	return count;
}

static ssize_t damon_sysfs_attrs_prmtv_update_interval_us_show(
		struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_attrs *attrs = container_of(kobj,
			struct damon_sysfs_attrs, kobj);

	return sysfs_emit(buf, "%lu\n", attrs->primitive_update_interval_us);
}

static ssize_t damon_sysfs_attrs_prmtv_update_interval_us_store(
		struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_attrs *attrs = container_of(kobj,
			struct damon_sysfs_attrs, kobj);
	unsigned long us;
	int err;

	err = kstrtoul(buf, 10, &us);
	if (err)
		return -EINVAL;

	attrs->primitive_update_interval_us = us;
	return count;
}

static void damon_sysfs_attrs_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_attrs, kobj));
}

static struct kobj_attribute damon_sysfs_attrs_sample_interval_us_attr =
		__ATTR(sample_interval_us, 0600,
				damon_sysfs_attrs_sampling_interval_us_show,
				damon_sysfs_attrs_sampling_interval_us_store);

static struct kobj_attribute damon_sysfs_attrs_aggr_interval_us_attr =
		__ATTR(aggr_interval_us, 0600,
				damon_sysfs_attrs_aggregation_interval_us_show,
				damon_sysfs_attrs_aggregation_interval_us_store);

static struct kobj_attribute damon_sysfs_attrs_update_interval_us_attr =
		__ATTR(update_interval_us, 0600,
				damon_sysfs_attrs_prmtv_update_interval_us_show,
				damon_sysfs_attrs_prmtv_update_interval_us_store);

static struct attribute *damon_sysfs_attrs_attrs[] = {
	&damon_sysfs_attrs_sample_interval_us_attr.attr,
	&damon_sysfs_attrs_aggr_interval_us_attr.attr,
	&damon_sysfs_attrs_update_interval_us_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_attrs);

static struct kobj_type damon_sysfs_attrs_ktype = {
	.release = damon_sysfs_attrs_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_attrs_groups,
};

/*
 * context directory
 * -----------------
 */

enum damon_sysfs_damon_type {
	DAMON_SYSFS_TYPE_VADDR,
	DAMON_SYSFS_TYPE_PADDR,
};

static const char * const damon_prmtv_set_strs[] = {
	"vaddr",
	"paddr",
};

struct damon_sysfs_context {
	struct kobject kobj;
	enum damon_sysfs_damon_type damon_type;
	struct damon_sysfs_attrs *attrs;
};

static int damon_sysfs_context_add_files(struct damon_sysfs_context *context)
{
	struct damon_sysfs_attrs *attrs;
	int err;

	attrs = kzalloc(sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	err = kobject_init_and_add(&attrs->kobj, &damon_sysfs_attrs_ktype,
			&context->kobj, "monitoring_attrs");
	if (err) {
		kfree(attrs);
		return err;
	}
	context->attrs = attrs;

	err = damon_sysfs_attrs_add_files(attrs);
	if (err) {
		kobject_put(&attrs->kobj);
		kfree(attrs);
		return err;
	}

	return err;
}

static void damon_sysfs_context_rm_files(struct damon_sysfs_context *context)
{
	damon_sysfs_attrs_rm_files(context->attrs);
	kobject_put(&context->attrs->kobj);
	return;
}

static ssize_t damon_sysfs_context_type_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_context *context = container_of(kobj,
			struct damon_sysfs_context, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_prmtv_set_strs[context->damon_type]);
}

static ssize_t damon_sysfs_context_type_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_context *context = container_of(kobj,
			struct damon_sysfs_context, kobj);

	if (!strncmp(buf, "vaddr\n", count))
		context->damon_type = DAMON_SYSFS_TYPE_VADDR;
	else if (!strncmp(buf, "paddr\n", count))
		context->damon_type = DAMON_SYSFS_TYPE_PADDR;
	else
		return -EINVAL;

	return count;
}

static void damon_sysfs_context_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_context, kobj));
}

static struct kobj_attribute damon_sysfs_context_type_attr = __ATTR(damon_type,
		0600, damon_sysfs_context_type_show,
		damon_sysfs_context_type_store);


static struct attribute *context_attrs[] = {
	&damon_sysfs_context_type_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(context);

static struct kobj_type context_ktype = {
	.release = damon_sysfs_context_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = context_groups,
};

/*
 * contexts directory
 * ------------------
 */

struct damon_sysfs_contexts {
	struct kobject kobj;
	struct damon_sysfs_context **contexts_arr;
	int nr;
};

static ssize_t contexts_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);

	return sysfs_emit(buf, "%d\n", contexts->nr);
}

static void contexts_kobj_rm_files(struct damon_sysfs_contexts *contexts)
{
	struct damon_sysfs_context **contexts_arr = contexts->contexts_arr;
	int i;

	for (i = 0; i < contexts->nr; i++) {
		damon_sysfs_context_rm_files(contexts_arr[i]);
		kobject_put(&contexts_arr[i]->kobj);
	}
	kfree(contexts_arr);
	contexts->contexts_arr = NULL;
	contexts->nr = 0;
}

static ssize_t contexts_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);
	struct damon_sysfs_context **contexts_arr, *context;
	int nr, err;
	int i;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	contexts_kobj_rm_files(contexts);

	contexts_arr = kmalloc(sizeof(*contexts_arr) * nr, GFP_KERNEL);
	contexts->contexts_arr = contexts_arr;
	for (i = 0; i < nr; i++) {
		context = kzalloc(sizeof(*context), GFP_KERNEL);
		if (!context) {
			contexts_kobj_rm_files(contexts);
			return -ENOMEM;
		}
		context->damon_type = DAMON_SYSFS_TYPE_VADDR;

		err = kobject_init_and_add(&context->kobj, &context_ktype,
				&contexts->kobj, "%d", i);
		if (err) {
			contexts_kobj_rm_files(contexts);
			kfree(context);
			return err;
		}

		err = damon_sysfs_context_add_files(context);
		if (err) {
			kobject_put(&context->kobj);
			contexts_kobj_rm_files(contexts);
			kfree(context);
			return err;
		}

		contexts_arr[i] = context;
		contexts->nr++;
	}

	return count;
}

static void contexts_kobj_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_contexts, kobj));
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
	struct damon_sysfs_contexts *contexts;
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

static void kdamond_kobj_rm_files(struct kdamond_kobj *kdamond_kobj)
{
	contexts_kobj_rm_files(kdamond_kobj->contexts);
	kobject_put(&kdamond_kobj->contexts->kobj);
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

static void kdamonds_kobj_rm_files(struct kdamonds_kobj *kdamonds_kobj)
{
	struct kdamond_kobj ** kdamond_kobjs = kdamonds_kobj->kdamond_kobjs;
	int i;

	for (i = 0; i < kdamonds_kobj->nr; i++) {
		kdamond_kobj_rm_files(kdamond_kobjs[i]);
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
	struct damon_sysfs_contexts *contexts;
	int nr, err;
	int i;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	kdamonds_kobj_rm_files(kdamonds_kobj);
	if (!nr)
		return count;

	kdamond_kobjs = kmalloc(sizeof(*kdamond_kobjs) * nr, GFP_KERNEL);
	kdamonds_kobj->kdamond_kobjs = kdamond_kobjs;
	for (i = 0; i < nr; i++) {
		kdamond_kobj = kzalloc(sizeof(*kdamond_kobj), GFP_KERNEL);
		if (!kdamond_kobj) {
			kdamonds_kobj_rm_files(kdamonds_kobj);
			return -ENOMEM;
		}
		err = kobject_init_and_add(&kdamond_kobj->kobj, &kdamond_ktype,
				&kdamonds_kobj->kobj, "%d", i);
		if (err) {
			kdamonds_kobj_rm_files(kdamonds_kobj);
			kfree(kdamond_kobj);
			return err;
		}

		contexts = kzalloc(sizeof(*contexts), GFP_KERNEL);
		if (!contexts) {
			kdamonds_kobj_rm_files(kdamonds_kobj);
			return -ENOMEM;
		}
		err = kobject_init_and_add(&contexts->kobj,
				&contexts_ktype, &kdamond_kobj->kobj,
				"contexts");
		if (err) {
			kdamonds_kobj_rm_files(kdamonds_kobj);
			kfree(contexts);
			return err;
		}
		kdamond_kobj->contexts = contexts;

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
