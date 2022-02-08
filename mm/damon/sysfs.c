// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/kobject.h>
#include <linux/slab.h>

/*
 * unsigned long range directory
 */

struct damon_sysfs_ul_range {
	struct kobject kobj;
	unsigned long min;
	unsigned long max;
};

static struct damon_sysfs_ul_range *damon_sysfs_ul_range_alloc(
		unsigned long min,
		unsigned long max)
{
	struct damon_sysfs_ul_range *range = kmalloc(sizeof(*range),
			GFP_KERNEL);

	if (!range)
		return NULL;
	range->kobj = (struct kobject){};
	range->min = min;
	range->max = max;

	return range;
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

static void damon_sysfs_ul_range_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_ul_range, kobj));
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
 * intervals directory
 */

struct damon_sysfs_intervals {
	struct kobject kobj;
	unsigned long sample_us;
	unsigned long aggr_us;
	unsigned long update_us;
};

static struct damon_sysfs_intervals *damon_sysfs_intervals_alloc(
		unsigned long sample_us, unsigned long aggr_us,
		unsigned long update_us)
{
	struct damon_sysfs_intervals *intervals = kmalloc(sizeof(*intervals),
			GFP_KERNEL);

	if (!intervals)
		return NULL;

	intervals->kobj = (struct kobject){};
	intervals->sample_us = sample_us;
	intervals->aggr_us = aggr_us;
	intervals->update_us = update_us;
	return intervals;
}

static ssize_t damon_sysfs_intervals_sample_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->sample_us);
}

static ssize_t damon_sysfs_intervals_sample_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 10, &us);

	if (err)
		return -EINVAL;

	intervals->sample_us = us;
	return count;
}

static ssize_t damon_sysfs_intervals_aggr_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->aggr_us);
}

static ssize_t damon_sysfs_intervals_aggr_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 10, &us);

	if (err)
		return -EINVAL;

	intervals->aggr_us = us;
	return count;
}

static ssize_t damon_sysfs_intervals_update_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->update_us);
}

static ssize_t damon_sysfs_intervals_update_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 10, &us);

	if (err)
		return -EINVAL;

	intervals->update_us = us;
	return count;
}

static void damon_sysfs_intervals_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_intervals, kobj));
}

static struct kobj_attribute damon_sysfs_intervals_sample_us_attr =
		__ATTR(sample_us, 0600,
				damon_sysfs_intervals_sample_us_show,
				damon_sysfs_intervals_sample_us_store);

static struct kobj_attribute damon_sysfs_intervals_aggr_us_attr =
		__ATTR(aggr_us, 0600,
				damon_sysfs_intervals_aggr_us_show,
				damon_sysfs_intervals_aggr_us_store);

static struct kobj_attribute damon_sysfs_intervals_update_us_attr =
		__ATTR(update_us, 0600,
				damon_sysfs_intervals_update_us_show,
				damon_sysfs_intervals_update_us_store);

static struct attribute *damon_sysfs_intervals_attrs[] = {
	&damon_sysfs_intervals_sample_us_attr.attr,
	&damon_sysfs_intervals_aggr_us_attr.attr,
	&damon_sysfs_intervals_update_us_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_intervals);

static struct kobj_type damon_sysfs_intervals_ktype = {
	.release = damon_sysfs_intervals_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_intervals_groups,
};

/*
 * monitoring_attrs directory
 */

struct damon_sysfs_attrs {
	struct kobject kobj;
	struct damon_sysfs_intervals *intervals;
	struct damon_sysfs_ul_range *nr_regions;
};

static struct damon_sysfs_attrs *damon_sysfs_attrs_alloc(void)
{
	struct damon_sysfs_attrs *attrs = kmalloc(sizeof(*attrs), GFP_KERNEL);

	if (!attrs)
		return NULL;
	attrs->kobj = (struct kobject){};
	return attrs;
}

static int damon_sysfs_attrs_add_dirs(struct damon_sysfs_attrs *attrs)
{
	struct damon_sysfs_intervals *intervals;
	struct damon_sysfs_ul_range *nr_regions_range;
	int err;

	intervals = damon_sysfs_intervals_alloc(5000, 100000, 60000000);
	if (!intervals)
		return -ENOMEM;

	err = kobject_init_and_add(&intervals->kobj,
			&damon_sysfs_intervals_ktype, &attrs->kobj,
			"intervals");
	if (err) {
		kfree(intervals);
		return err;
	}
	attrs->intervals = intervals;

	nr_regions_range = damon_sysfs_ul_range_alloc(10, 1000);
	if (!nr_regions_range)
		return -ENOMEM;

	err = kobject_init_and_add(&nr_regions_range->kobj,
			&damon_sysfs_ul_range_ktype, &attrs->kobj,
			"nr_regions");
	if (err) {
		kfree(nr_regions_range);
		kobject_put(&intervals->kobj);
		return err;
	}
	attrs->nr_regions = nr_regions_range;

	return 0;
}

static void damon_sysfs_attrs_rm_dirs(struct damon_sysfs_attrs *attrs)
{
	kobject_put(&attrs->nr_regions->kobj);
	kobject_put(&attrs->intervals->kobj);
}

static void damon_sysfs_attrs_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_attrs, kobj));
}

static struct attribute *damon_sysfs_attrs_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_attrs);

static struct kobj_type damon_sysfs_attrs_ktype = {
	.release = damon_sysfs_attrs_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_attrs_groups,
};

/*
 * context directory
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

static struct damon_sysfs_context *damon_sysfs_context_alloc(
		enum damon_sysfs_damon_type damon_type)
{
	struct damon_sysfs_context *context = kmalloc(sizeof(*context),
				GFP_KERNEL);

	if (!context)
		return NULL;
	context->kobj = (struct kobject){};
	context->damon_type = damon_type;
	return context;
}

static int damon_sysfs_context_add_dirs(struct damon_sysfs_context *context)
{
	struct damon_sysfs_attrs *attrs;
	int err;

	attrs = damon_sysfs_attrs_alloc();
	if (!attrs)
		return -ENOMEM;

	err = kobject_init_and_add(&attrs->kobj, &damon_sysfs_attrs_ktype,
			&context->kobj, "monitoring_attrs");
	if (err) {
		kfree(attrs);
		return err;
	}

	err = damon_sysfs_attrs_add_dirs(attrs);
	if (err) {
		kobject_put(&attrs->kobj);
		return err;
	}
	context->attrs = attrs;

	return err;
}

static void damon_sysfs_context_rm_dirs(struct damon_sysfs_context *context)
{
	damon_sysfs_attrs_rm_dirs(context->attrs);
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


static struct attribute *damon_sysfs_context_attrs[] = {
	&damon_sysfs_context_type_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_context);

static struct kobj_type context_ktype = {
	.release = damon_sysfs_context_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_context_groups,
};

/*
 * contexts directory
 */

struct damon_sysfs_contexts {
	struct kobject kobj;
	struct damon_sysfs_context **contexts_arr;
	int nr;
};

static struct damon_sysfs_contexts *damon_sysfs_contexts_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_contexts), GFP_KERNEL);
}

static void damon_sysfs_contexts_rm_dirs(struct damon_sysfs_contexts *contexts)
{
	struct damon_sysfs_context **contexts_arr = contexts->contexts_arr;
	int i;

	for (i = 0; i < contexts->nr; i++) {
		damon_sysfs_context_rm_dirs(contexts_arr[i]);
		kobject_put(&contexts_arr[i]->kobj);
	}
	kfree(contexts_arr);
	contexts->contexts_arr = NULL;
	contexts->nr = 0;
}

static int damon_sysfs_contexts_add_dirs(struct damon_sysfs_contexts *contexts,
		int nr_contexts)
{
	struct damon_sysfs_context **contexts_arr, *context;
	int err, i;

	damon_sysfs_contexts_rm_dirs(contexts);
	if (!nr_contexts)
		return 0;

	contexts_arr = kmalloc(sizeof(*contexts_arr) * nr_contexts,
			GFP_KERNEL);
	if (!contexts_arr)
		return -ENOMEM;
	contexts->contexts_arr = contexts_arr;

	for (i = 0; i < nr_contexts; i++) {
		context = damon_sysfs_context_alloc(DAMON_SYSFS_TYPE_VADDR);
		if (!context) {
			damon_sysfs_contexts_rm_dirs(contexts);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&context->kobj, &context_ktype,
				&contexts->kobj, "%d", i);
		if (err) {
			kfree(context);
			damon_sysfs_contexts_rm_dirs(contexts);
			return err;
		}

		err = damon_sysfs_context_add_dirs(context);
		if (err) {
			kobject_put(&context->kobj);
			damon_sysfs_contexts_rm_dirs(contexts);
			return err;
		}

		contexts_arr[i] = context;
		contexts->nr++;
	}
	return 0;
}

static ssize_t damon_sysfs_contexts_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);

	return sysfs_emit(buf, "%d\n", contexts->nr);
}

static ssize_t damon_sysfs_contexts_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);
	int nr, err;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	err = damon_sysfs_contexts_add_dirs(contexts, nr);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_contexts_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_contexts, kobj));
}

static struct kobj_attribute damon_sysfs_contexts_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_contexts_nr_show, damon_sysfs_contexts_nr_store);

static struct attribute *damon_sysfs_contexts_attrs[] = {
	&damon_sysfs_contexts_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_contexts);

static struct kobj_type damon_sysfs_contexts_ktype = {
	.release = damon_sysfs_contexts_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_contexts_groups,
};

/*
 * kdamond directory
 */

struct damon_sysfs_kdamond {
	struct kobject kobj;
	struct damon_sysfs_contexts *contexts;
	int pid;
};

static struct damon_sysfs_kdamond *damon_sysfs_kdamond_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_kdamond), GFP_KERNEL);
}

static int damon_sysfs_kdamond_add_dirs(struct damon_sysfs_kdamond *kdamond)
{
	struct damon_sysfs_contexts *contexts;
	int err;

	contexts = damon_sysfs_contexts_alloc();
	if (!contexts)
		return -ENOMEM;

	err = kobject_init_and_add(&contexts->kobj,
			&damon_sysfs_contexts_ktype, &kdamond->kobj,
			"contexts");
	if (err) {
		kfree(contexts);
		return err;
	}
	kdamond->contexts = contexts;

	return err;
}

static void damon_sysfs_kdamond_rm_dirs(struct damon_sysfs_kdamond *kdamond)
{
	damon_sysfs_contexts_rm_dirs(kdamond->contexts);
	kobject_put(&kdamond->contexts->kobj);
}

static ssize_t damon_sysfs_kdamond_pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	/* TODO: get pid of the kdamond and show */
	return sysfs_emit(buf, "%d\n", 42);
}

static void damon_sysfs_kdamond_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_kdamond, kobj));
}

static struct kobj_attribute damon_sysfs_kdamond_pid_attr = __ATTR(pid, 0400,
		damon_sysfs_kdamond_pid_show, NULL);

static struct attribute *damon_sysfs_kdamond_attrs[] = {
	&damon_sysfs_kdamond_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_kdamond);

static struct kobj_type damon_sysfs_kdamond_ktype = {
	.release = damon_sysfs_kdamond_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_kdamond_groups,
};

/*
 * kdamonds directory
 */

struct damon_sysfs_kdamonds {
	struct kobject kobj;
	struct damon_sysfs_kdamond **kdamonds_arr;
	int nr;
};

static struct damon_sysfs_kdamonds *damon_sysfs_kdamonds_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_kdamonds), GFP_KERNEL);
}

static void damon_sysfs_kdamonds_rm_dirs(struct damon_sysfs_kdamonds *kdamonds)
{
	struct damon_sysfs_kdamond **kdamonds_arr = kdamonds->kdamonds_arr;
	int i;

	for (i = 0; i < kdamonds->nr; i++) {
		damon_sysfs_kdamond_rm_dirs(kdamonds_arr[i]);
		kobject_put(&kdamonds_arr[i]->kobj);
	}
	kfree(kdamonds_arr);
	kdamonds->kdamonds_arr = NULL;
	kdamonds->nr = 0;
}

static int damon_sysfs_kdamonds_add_dirs(struct damon_sysfs_kdamonds *kdamonds,
		int nr_kdamonds)
{
	struct damon_sysfs_kdamond **kdamonds_arr, *kdamond;
	int err, i;

	damon_sysfs_kdamonds_rm_dirs(kdamonds);
	if (!nr_kdamonds)
		return 0;

	kdamonds_arr = kmalloc(sizeof(*kdamonds_arr) * nr_kdamonds,
			GFP_KERNEL);
	if (!kdamonds_arr)
		return -ENOMEM;
	kdamonds->kdamonds_arr = kdamonds_arr;

	for (i = 0; i < nr_kdamonds; i++) {
		kdamond = damon_sysfs_kdamond_alloc();
		if (!kdamond) {
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&kdamond->kobj,
				&damon_sysfs_kdamond_ktype, &kdamonds->kobj,
				"%d", i);
		if (err) {
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			kfree(kdamond);
			return err;
		}

		err = damon_sysfs_kdamond_add_dirs(kdamond);
		if (err) {
			kobject_put(&kdamond->kobj);
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			return err;
		}

		kdamonds_arr[i] = kdamond;
		kdamonds->nr++;
	}
	return 0;
}

static ssize_t damon_sysfs_kdamonds_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_kdamonds *kdamonds = container_of(kobj,
			struct damon_sysfs_kdamonds, kobj);

	return sysfs_emit(buf, "%d\n", kdamonds->nr);
}

static ssize_t damon_sysfs_kdamonds_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_kdamonds *kdamonds = container_of(kobj,
			struct damon_sysfs_kdamonds, kobj);
	int nr, err;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	err = damon_sysfs_kdamonds_add_dirs(kdamonds, nr);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_kdamonds_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_kdamonds, kobj));
}

static struct kobj_attribute damon_sysfs_kdamonds_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_kdamonds_nr_show, damon_sysfs_kdamonds_nr_store);

static struct attribute *damon_sysfs_kdamonds_attrs[] = {
	&damon_sysfs_kdamonds_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_kdamonds);

static struct kobj_type damon_sysfs_kdamonds_ktype = {
	.release = damon_sysfs_kdamonds_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_kdamonds_groups,
};

/*
 * damon directory (root)
 */

struct damon_sysfs_damon {
	struct kobject kobj;
	struct damon_sysfs_kdamonds *kdamonds;
	bool monitor_on;
};

static struct damon_sysfs_damon *damon_sysfs_damon_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_damon), GFP_KERNEL);
}

static int damon_sysfs_damon_add_dirs(struct damon_sysfs_damon *damon)
{
	struct damon_sysfs_kdamonds *kdamonds;
	int err;

	kdamonds = damon_sysfs_kdamonds_alloc();
	if (!kdamonds)
		return -ENOMEM;

	err = kobject_init_and_add(&kdamonds->kobj,
			&damon_sysfs_kdamonds_ktype, &damon->kobj,
			"kdamonds");
	if (err) {
		kfree(kdamonds);
		return err;
	}
	damon->kdamonds = kdamonds;
	return err;
}

static ssize_t damon_sysfs_damon_monitor_on_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_damon *damon = container_of(kobj,
			struct damon_sysfs_damon, kobj);

	return sysfs_emit(buf, "%s\n", damon->monitor_on ? "on" : "off");
}

static ssize_t damon_sysfs_damon_monitor_on_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_damon *damon = container_of(kobj,
			struct damon_sysfs_damon, kobj);
	bool on, err;

	err = kstrtobool(buf, &on);
	if (err)
		return err;

	damon->monitor_on = on;
	return count;
}

static void damon_sysfs_damon_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_damon, kobj));
}

static struct kobj_attribute damon_sysfs_damon_monitor_on_attr =
	__ATTR(monitor_on, 0600, damon_sysfs_damon_monitor_on_show,
		damon_sysfs_damon_monitor_on_store);

static struct attribute *damon_sysfs_damon_attrs[] = {
	&damon_sysfs_damon_monitor_on_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_damon);

static struct kobj_type damon_sysfs_damon_ktype = {
	.release = damon_sysfs_damon_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_damon_groups,
};

static int __init damon_sysfs_init(void)
{
	struct damon_sysfs_damon *damon;
	int err;

	damon = damon_sysfs_damon_alloc();
	if (!damon)
		return -ENOMEM;
	err = kobject_init_and_add(&damon->kobj, &damon_sysfs_damon_ktype,
			mm_kobj, "damon");
	if (err) {
		kfree(damon);
		return err;
	}

	err = damon_sysfs_damon_add_dirs(damon);
	if (err) {
		kobject_put(&damon->kobj);
		return err;
	}

	return 0;
}
subsys_initcall(damon_sysfs_init);
