// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/kobject.h>
#include <linux/pid.h>
#include <linux/slab.h>

static struct damon_ctx **damon_sysfs_ctxs;
static int damon_sysfs_nr_ctxs;
static DEFINE_MUTEX(damon_sysfs_lock);

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
 * schemes/stats directory
 */

struct damon_sysfs_stats {
	struct kobject kobj;
	unsigned long nr_tried;
	unsigned long sz_tried;
	unsigned long nr_applied;
	unsigned long sz_applied;
	unsigned long qt_exceeds;
};

static struct damon_sysfs_stats *damon_sysfs_stats_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_stats), GFP_KERNEL);
}

static ssize_t damon_sysfs_stats_nr_tried_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->nr_tried);
}

static ssize_t damon_sysfs_stats_nr_tried_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);
	int err = kstrtoul(buf, 10, &stats->nr_tried);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_stats_sz_tried_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->sz_tried);
}

static ssize_t damon_sysfs_stats_sz_tried_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);
	int err = kstrtoul(buf, 10, &stats->sz_tried);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_stats_nr_applied_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->nr_applied);
}

static ssize_t damon_sysfs_stats_nr_applied_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);
	int err = kstrtoul(buf, 10, &stats->nr_applied);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_stats_sz_applied_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->sz_applied);
}

static ssize_t damon_sysfs_stats_sz_applied_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);
	int err = kstrtoul(buf, 10, &stats->sz_applied);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_stats_qt_exceeds_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->qt_exceeds);
}

static ssize_t damon_sysfs_stats_qt_exceeds_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);
	int err = kstrtoul(buf, 10, &stats->qt_exceeds);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_stats_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_stats, kobj));
}

static struct kobj_attribute damon_sysfs_stats_nr_tried_attr =
		__ATTR(nr_tried, 0400, damon_sysfs_stats_nr_tried_show,
				damon_sysfs_stats_nr_tried_store);

static struct kobj_attribute damon_sysfs_stats_sz_tried_attr =
		__ATTR(sz_tried, 0400, damon_sysfs_stats_sz_tried_show,
				damon_sysfs_stats_sz_tried_store);

static struct kobj_attribute damon_sysfs_stats_nr_applied_attr =
		__ATTR(nr_applied, 0400, damon_sysfs_stats_nr_applied_show,
				damon_sysfs_stats_nr_applied_store);

static struct kobj_attribute damon_sysfs_stats_sz_applied_attr =
		__ATTR(sz_applied, 0400, damon_sysfs_stats_sz_applied_show,
				damon_sysfs_stats_sz_applied_store);

static struct kobj_attribute damon_sysfs_stats_qt_exceeds_attr =
		__ATTR(qt_exceeds, 0400, damon_sysfs_stats_qt_exceeds_show,
				damon_sysfs_stats_qt_exceeds_store);

static struct attribute *damon_sysfs_stats_attrs[] = {
	&damon_sysfs_stats_nr_tried_attr.attr,
	&damon_sysfs_stats_sz_tried_attr.attr,
	&damon_sysfs_stats_nr_applied_attr.attr,
	&damon_sysfs_stats_sz_applied_attr.attr,
	&damon_sysfs_stats_qt_exceeds_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_stats);

static struct kobj_type damon_sysfs_stats_ktype = {
	.release = damon_sysfs_stats_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_stats_groups,
};

/*
 * watermarks directory
 */

struct damon_sysfs_watermarks {
	struct kobject kobj;
	enum damos_wmark_metric metric;
	unsigned long interval_us;
	unsigned long high;
	unsigned long mid;
	unsigned long low;
};

static struct damon_sysfs_watermarks *damon_sysfs_watermarks_alloc(
		enum damos_wmark_metric metric, unsigned long interval_us,
		unsigned long high, unsigned long mid, unsigned long low)
{
	struct damon_sysfs_watermarks *watermarks = kmalloc(
			sizeof(*watermarks), GFP_KERNEL);

	if (!watermarks)
		return NULL;
	watermarks->kobj = (struct kobject){};
	watermarks->metric = metric;
	watermarks->interval_us = interval_us;
	watermarks->high = high;
	watermarks->mid = mid;
	watermarks->low = low;
	return watermarks;
}

static const char * const damon_sysfs_wmark_metric_strs[] = {
	"none",
	"free_mem_rate",
};

static ssize_t damon_sysfs_watermarks_metric_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_sysfs_wmark_metric_strs[watermarks->metric]);
}

static ssize_t damon_sysfs_watermarks_metric_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	if (!strncmp(buf, "none\n", count))
		watermarks->metric = DAMOS_WMARK_NONE;
	else if (!strncmp(buf, "free_mem_rate\n", count))
		watermarks->metric = DAMOS_WMARK_FREE_MEM_RATE;
	else
		return -EINVAL;

	return count;
}

static ssize_t damon_sysfs_watermarks_interval_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->interval_us);
}

static ssize_t damon_sysfs_watermarks_interval_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 10, &watermarks->interval_us);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_watermarks_high_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->high);
}

static ssize_t damon_sysfs_watermarks_high_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 10, &watermarks->high);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_watermarks_mid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->mid);
}

static ssize_t damon_sysfs_watermarks_mid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 10, &watermarks->mid);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_watermarks_low_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->low);
}

static ssize_t damon_sysfs_watermarks_low_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 10, &watermarks->low);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_watermarks_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_watermarks, kobj));
}

static struct kobj_attribute damon_sysfs_watermarks_metric_attr =
		__ATTR(metric, 0600, damon_sysfs_watermarks_metric_show,
				damon_sysfs_watermarks_metric_store);

static struct kobj_attribute damon_sysfs_watermarks_interval_us_attr =
		__ATTR(interval_us, 0600,
				damon_sysfs_watermarks_interval_us_show,
				damon_sysfs_watermarks_interval_us_store);

static struct kobj_attribute damon_sysfs_watermarks_high_attr =
		__ATTR(high, 0600, damon_sysfs_watermarks_high_show,
				damon_sysfs_watermarks_high_store);

static struct kobj_attribute damon_sysfs_watermarks_mid_attr =
		__ATTR(mid, 0600, damon_sysfs_watermarks_mid_show,
				damon_sysfs_watermarks_mid_store);

static struct kobj_attribute damon_sysfs_watermarks_low_attr =
		__ATTR(low, 0600, damon_sysfs_watermarks_low_show,
				damon_sysfs_watermarks_low_store);

static struct attribute *damon_sysfs_watermarks_attrs[] = {
	&damon_sysfs_watermarks_metric_attr.attr,
	&damon_sysfs_watermarks_interval_us_attr.attr,
	&damon_sysfs_watermarks_high_attr.attr,
	&damon_sysfs_watermarks_mid_attr.attr,
	&damon_sysfs_watermarks_low_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_watermarks);

static struct kobj_type damon_sysfs_watermarks_ktype = {
	.release = damon_sysfs_watermarks_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_watermarks_groups,
};

/*
 * scheme/weights directory
 */

struct damon_sysfs_weights {
	struct kobject kobj;
	unsigned int sz;
	unsigned int nr_accesses;
	unsigned int age;
};

static struct damon_sysfs_weights *damon_sysfs_weights_alloc(unsigned int sz,
		unsigned int nr_accesses, unsigned int age)
{
	struct damon_sysfs_weights *weights = kmalloc(sizeof(*weights),
			GFP_KERNEL);

	if (!weights)
		return NULL;
	weights->kobj = (struct kobject){};
	weights->sz = sz;
	weights->nr_accesses = nr_accesses;
	weights->age = age;
	return weights;
}

static ssize_t damon_sysfs_weights_sz_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);

	return sysfs_emit(buf, "%u\n", weights->sz);
}

static ssize_t damon_sysfs_weights_sz_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);
	int err = kstrtouint(buf, 10, &weights->sz);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_weights_nr_accesses_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);

	return sysfs_emit(buf, "%u\n", weights->nr_accesses);
}

static ssize_t damon_sysfs_weights_nr_accesses_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);
	int err = kstrtouint(buf, 10, &weights->nr_accesses);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_weights_age_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);

	return sysfs_emit(buf, "%u\n", weights->age);
}

static ssize_t damon_sysfs_weights_age_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);
	int err = kstrtouint(buf, 10, &weights->age);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_weights_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_weights, kobj));
}

static struct kobj_attribute damon_sysfs_weights_sz_attr =
		__ATTR(sz, 0600, damon_sysfs_weights_sz_show,
				damon_sysfs_weights_sz_store);

static struct kobj_attribute damon_sysfs_weights_nr_accesses_attr =
		__ATTR(nr_accesses, 0600, damon_sysfs_weights_nr_accesses_show,
				damon_sysfs_weights_nr_accesses_store);

static struct kobj_attribute damon_sysfs_weights_age_attr =
		__ATTR(age, 0600, damon_sysfs_weights_age_show,
				damon_sysfs_weights_age_store);

static struct attribute *damon_sysfs_weights_attrs[] = {
	&damon_sysfs_weights_sz_attr.attr,
	&damon_sysfs_weights_nr_accesses_attr.attr,
	&damon_sysfs_weights_age_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_weights);

static struct kobj_type damon_sysfs_weights_ktype = {
	.release = damon_sysfs_weights_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_weights_groups,
};

/*
 * quotas directory
 */

struct damon_sysfs_quotas {
	struct kobject kobj;
	struct damon_sysfs_weights *weights;
	unsigned long ms;
	unsigned long sz;
	unsigned long reset_interval_ms;
};

static struct damon_sysfs_quotas *damon_sysfs_quotas_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_quotas), GFP_KERNEL);
}

static int damon_sysfs_quotas_add_dirs(struct damon_sysfs_quotas *quotas)
{
	struct damon_sysfs_weights *weights;
	int err;

	weights = damon_sysfs_weights_alloc(0, 0, 0);
	if (!weights)
		return -ENOMEM;

	err = kobject_init_and_add(&weights->kobj, &damon_sysfs_weights_ktype,
			&quotas->kobj, "weights");
	if (err) {
		kfree(weights);
		return err;
	}
	quotas->weights = weights;

	return err;
}

static void damon_sysfs_quotas_rm_dirs(struct damon_sysfs_quotas *quotas)
{
	kobject_put(&quotas->weights->kobj);
}

static ssize_t damon_sysfs_quotas_ms_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);

	return sysfs_emit(buf, "%lu\n", quotas->ms);
}

static ssize_t damon_sysfs_quotas_ms_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);
	int err = kstrtoul(buf, 10, &quotas->ms);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_quotas_sz_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);

	return sysfs_emit(buf, "%lu\n", quotas->sz);
}

static ssize_t damon_sysfs_quotas_sz_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);
	int err = kstrtoul(buf, 10, &quotas->sz);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_quotas_reset_interval_ms_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);

	return sysfs_emit(buf, "%lu\n", quotas->reset_interval_ms);
}

static ssize_t damon_sysfs_quotas_reset_interval_ms_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);
	int err = kstrtoul(buf, 10, &quotas->reset_interval_ms);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_quotas_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_quotas, kobj));
}

static struct kobj_attribute damon_sysfs_quotas_ms_attr = __ATTR(ms, 0600,
		damon_sysfs_quotas_ms_show, damon_sysfs_quotas_ms_store);

static struct kobj_attribute damon_sysfs_quotas_sz_attr = __ATTR(sz, 0600,
		damon_sysfs_quotas_sz_show, damon_sysfs_quotas_sz_store);

static struct kobj_attribute damon_sysfs_quotas_reset_interval_ms_attr =
		__ATTR(reset_interval_ms, 0600,
			damon_sysfs_quotas_reset_interval_ms_show,
			damon_sysfs_quotas_reset_interval_ms_store);

static struct attribute *damon_sysfs_quotas_attrs[] = {
	&damon_sysfs_quotas_ms_attr.attr,
	&damon_sysfs_quotas_sz_attr.attr,
	&damon_sysfs_quotas_reset_interval_ms_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_quotas);

static struct kobj_type damon_sysfs_quotas_ktype = {
	.release = damon_sysfs_quotas_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_quotas_groups,
};

/*
 * access_pattern directory
 */

struct damon_sysfs_access_pattern {
	struct kobject kobj;
	struct damon_sysfs_ul_range *sz;
	struct damon_sysfs_ul_range *nr_accesses;
	struct damon_sysfs_ul_range *age;
};

static
struct damon_sysfs_access_pattern *damon_sysfs_access_pattern_alloc(void)
{
	struct damon_sysfs_access_pattern *access_pattern =
		kmalloc(sizeof(*access_pattern), GFP_KERNEL);

	if (!access_pattern)
		return NULL;
	access_pattern->kobj = (struct kobject){};
	return access_pattern;
}

static int damon_sysfs_access_pattern_add_dirs(
		struct damon_sysfs_access_pattern *access_pattern)
{
	struct damon_sysfs_ul_range *sz, *nr_accesses, *age;
	int err;

	sz = damon_sysfs_ul_range_alloc(0, 0);
	if (!sz)
		return -ENOMEM;
	err = kobject_init_and_add(&sz->kobj, &damon_sysfs_ul_range_ktype,
			&access_pattern->kobj, "sz");
	if (err) {
		kfree(sz);
		return err;
	}
	access_pattern->sz = sz;

	nr_accesses = damon_sysfs_ul_range_alloc(0, 0);
	if (!nr_accesses)
		return -ENOMEM;
	err = kobject_init_and_add(&nr_accesses->kobj, &damon_sysfs_ul_range_ktype,
			&access_pattern->kobj, "nr_accesses");
	if (err) {
		kfree(nr_accesses);
		kobject_put(&sz->kobj);
		return err;
	}
	access_pattern->nr_accesses = nr_accesses;

	age = damon_sysfs_ul_range_alloc(0, 0);
	if (!age)
		return -ENOMEM;
	err = kobject_init_and_add(&age->kobj, &damon_sysfs_ul_range_ktype,
			&access_pattern->kobj, "age");
	if (err) {
		kfree(age);
		kobject_put(&nr_accesses->kobj);
		kobject_put(&sz->kobj);
		return err;
	}
	access_pattern->age = age;

	return 0;
}

static void damon_sysfs_access_pattern_rm_dirs(
		struct damon_sysfs_access_pattern *access_pattern)
{
	kobject_put(&access_pattern->sz->kobj);
	kobject_put(&access_pattern->nr_accesses->kobj);
	kobject_put(&access_pattern->age->kobj);
}

static void damon_sysfs_access_pattern_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_access_pattern, kobj));
}

static struct attribute *damon_sysfs_access_pattern_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_access_pattern);

static struct kobj_type damon_sysfs_access_pattern_ktype = {
	.release = damon_sysfs_access_pattern_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_access_pattern_groups,
};

/*
 * scheme directory
 */

struct damon_sysfs_scheme {
	struct kobject kobj;
	enum damos_action action;
	struct damon_sysfs_access_pattern *access_pattern;
	struct damon_sysfs_quotas *quotas;
	struct damon_sysfs_watermarks *watermarks;
	struct damon_sysfs_stats *stats;
};

static const char * const damon_sysfs_damos_action_strs[] = {
	"willneed",
	"cold",
	"pageout",
	"hugepage",
	"nohugepage",
	"stat",
};

static struct damon_sysfs_scheme *damon_sysfs_scheme_alloc(
		enum damos_action action)
{
	struct damon_sysfs_scheme *scheme = kmalloc(sizeof(*scheme),
				GFP_KERNEL);

	if (!scheme)
		return NULL;
	scheme->kobj = (struct kobject){};
	scheme->action = action;
	return scheme;
}

static int damon_sysfs_scheme_add_dirs(struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_access_pattern *access_pattern;
	struct damon_sysfs_quotas *quotas;
	struct damon_sysfs_watermarks *watermarks;
	struct damon_sysfs_stats *stats;
	int err;

	/* add access_pattern */
	access_pattern = damon_sysfs_access_pattern_alloc();
	if (!access_pattern)
		return -ENOMEM;
	err = kobject_init_and_add(&access_pattern->kobj,
			&damon_sysfs_access_pattern_ktype, &scheme->kobj,
			"access_pattern");
	if (err) {
		kfree(access_pattern);
		return err;
	}
	err = damon_sysfs_access_pattern_add_dirs(access_pattern);
	if (err) {
		kobject_put(&access_pattern->kobj);
		return err;
	}

	/* add quotas */
	quotas = damon_sysfs_quotas_alloc();
	if (!quotas) {
		kobject_put(&access_pattern->kobj);
		return -ENOMEM;
	}
	err = kobject_init_and_add(&quotas->kobj, &damon_sysfs_quotas_ktype,
			&scheme->kobj, "quotas");
	if (err) {
		kfree(quotas);
		goto put_access_pattern_out;
	}
	err = damon_sysfs_quotas_add_dirs(quotas);
	if (err)
		goto put_quotas_out;

	/* add watermarks */
	watermarks = damon_sysfs_watermarks_alloc(DAMOS_WMARK_NONE, 0, 0, 0,
			0);
	if (!watermarks) {
		err = -ENOMEM;
		goto put_quotas_out;
	}
	err = kobject_init_and_add(&watermarks->kobj,
			&damon_sysfs_watermarks_ktype, &scheme->kobj,
			"watermarks");
	if (err) {
		kfree(watermarks);
		goto put_quotas_out;
	}

	/* add stats */
	stats = damon_sysfs_stats_alloc();
	if (!stats) {
		err = -ENOMEM;
		goto out;
	}
	err = kobject_init_and_add(&stats->kobj, &damon_sysfs_stats_ktype,
			&scheme->kobj, "stats");
	if (err) {
		kfree(stats);
		goto out;
	}

	scheme->access_pattern = access_pattern;
	scheme->quotas = quotas;
	scheme->watermarks = watermarks;
	scheme->stats = stats;

	return err;

out:
	kobject_put(&watermarks->kobj);
put_quotas_out:
	kobject_put(&quotas->kobj);
put_access_pattern_out:
	kobject_put(&access_pattern->kobj);
	return err;
}

static void damon_sysfs_scheme_rm_dirs(struct damon_sysfs_scheme *scheme)
{
	damon_sysfs_access_pattern_rm_dirs(scheme->access_pattern);
	kobject_put(&scheme->access_pattern->kobj);
	damon_sysfs_quotas_rm_dirs(scheme->quotas);
	kobject_put(&scheme->quotas->kobj);
	kobject_put(&scheme->watermarks->kobj);
	kobject_put(&scheme->stats->kobj);
	return;
}

static ssize_t damon_sysfs_scheme_action_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme *scheme = container_of(kobj,
			struct damon_sysfs_scheme, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_sysfs_damos_action_strs[scheme->action]);
}

static ssize_t damon_sysfs_scheme_action_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme *scheme = container_of(kobj,
			struct damon_sysfs_scheme, kobj);

	if (!strncmp(buf, "willneed\n", count))
		scheme->action = DAMOS_WILLNEED;
	else if (!strncmp(buf, "cold\n", count))
		scheme->action = DAMOS_COLD;
	else if (!strncmp(buf, "pageout\n", count))
		scheme->action = DAMOS_PAGEOUT;
	else if (!strncmp(buf, "hugepage\n", count))
		scheme->action = DAMOS_HUGEPAGE;
	else if (!strncmp(buf, "nohugepage\n", count))
		scheme->action = DAMOS_NOHUGEPAGE;
	else if (!strncmp(buf, "stat\n", count))
		scheme->action = DAMOS_STAT;
	else
		return -EINVAL;

	return count;
}

static void damon_sysfs_scheme_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_scheme, kobj));
}

static struct kobj_attribute damon_sysfs_scheme_action_attr = __ATTR(
		action, 0600, damon_sysfs_scheme_action_show,
		damon_sysfs_scheme_action_store);


static struct attribute *damon_sysfs_scheme_attrs[] = {
	&damon_sysfs_scheme_action_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme);

static struct kobj_type damon_sysfs_scheme_ktype = {
	.release = damon_sysfs_scheme_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_groups,
};

/*
 * schemes directory
 */

struct damon_sysfs_schemes {
	struct kobject kobj;
	struct damon_sysfs_scheme **schemes_arr;
	int nr_schemes;
};

static struct damon_sysfs_schemes *damon_sysfs_schemes_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_schemes), GFP_KERNEL);
}

static void damon_sysfs_schemes_rm_dirs(struct damon_sysfs_schemes *schemes)
{
	struct damon_sysfs_scheme **schemes_arr = schemes->schemes_arr;
	int i;

	for (i = 0; i < schemes->nr_schemes; i++) {
		damon_sysfs_scheme_rm_dirs(schemes_arr[i]);
		kobject_put(&schemes_arr[i]->kobj);
	}
	kfree(schemes_arr);
	schemes->schemes_arr = NULL;
	schemes->nr_schemes = 0;
}

static int damon_sysfs_schemes_add_dirs(struct damon_sysfs_schemes *schemes,
		int nr_schemes)
{
	struct damon_sysfs_scheme **schemes_arr, *scheme;
	int err, i;

	damon_sysfs_schemes_rm_dirs(schemes);
	if (!nr_schemes)
		return 0;

	schemes_arr = kmalloc(sizeof(*schemes_arr) * nr_schemes,
			GFP_KERNEL);
	if (!schemes_arr)
		return -ENOMEM;
	schemes->schemes_arr = schemes_arr;

	for (i = 0; i < nr_schemes; i++) {
		scheme = damon_sysfs_scheme_alloc(DAMOS_STAT);
		if (!scheme) {
			damon_sysfs_schemes_rm_dirs(schemes);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&scheme->kobj,
				&damon_sysfs_scheme_ktype, &schemes->kobj,
				"%d", i);
		if (err) {
			damon_sysfs_schemes_rm_dirs(schemes);
			kfree(scheme);
			return err;
		}

		err = damon_sysfs_scheme_add_dirs(scheme);
		if (err) {
			kobject_put(&scheme->kobj);
			damon_sysfs_schemes_rm_dirs(schemes);
			return err;
		}

		schemes_arr[i] = scheme;
		schemes->nr_schemes++;
	}
	return 0;
}

static ssize_t damon_sysfs_schemes_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_schemes *schemes = container_of(kobj,
			struct damon_sysfs_schemes, kobj);

	return sysfs_emit(buf, "%d\n", schemes->nr_schemes);
}

static ssize_t damon_sysfs_schemes_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_schemes *schemes = container_of(kobj,
			struct damon_sysfs_schemes, kobj);
	int nr, err;

	err = kstrtoint(buf, 10, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	mutex_lock(&damon_sysfs_lock);
	err = damon_sysfs_schemes_add_dirs(schemes, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_schemes_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_schemes, kobj));
}

static struct kobj_attribute damon_sysfs_schemes_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_schemes_nr_show, damon_sysfs_schemes_nr_store);

static struct attribute *damon_sysfs_schemes_attrs[] = {
	&damon_sysfs_schemes_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_schemes);

static struct kobj_type damon_sysfs_schemes_ktype = {
	.release = damon_sysfs_schemes_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_schemes_groups,
};

/*
 * init region directory
 */

struct damon_sysfs_region {
	struct kobject kobj;
	unsigned long start;
	unsigned long end;
};

static struct damon_sysfs_region *damon_sysfs_region_alloc(
		unsigned long start,
		unsigned long end)
{
	struct damon_sysfs_region *region = kmalloc(sizeof(*region),
			GFP_KERNEL);

	if (!region)
		return NULL;
	region->kobj = (struct kobject){};
	region->start = start;
	region->end = end;
	return region;
}

static ssize_t damon_sysfs_region_start_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->start);
}

static ssize_t damon_sysfs_region_start_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);
	int err = kstrtoul(buf, 10, &region->start);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t damon_sysfs_region_end_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->end);
}

static ssize_t damon_sysfs_region_end_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);
	int err = kstrtoul(buf, 10, &region->end);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_region_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_region, kobj));
}

static struct kobj_attribute damon_sysfs_region_start_attr =
		__ATTR(start, 0600, damon_sysfs_region_start_show,
				damon_sysfs_region_start_store);

static struct kobj_attribute damon_sysfs_region_end_attr =
		__ATTR(end, 0600, damon_sysfs_region_end_show,
				damon_sysfs_region_end_store);

static struct attribute *damon_sysfs_region_attrs[] = {
	&damon_sysfs_region_start_attr.attr,
	&damon_sysfs_region_end_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_region);

static struct kobj_type damon_sysfs_region_ktype = {
	.release = damon_sysfs_region_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_region_groups,
};

/*
 * init_regions directory
 */

struct damon_sysfs_regions {
	struct kobject kobj;
	struct damon_sysfs_region **regions_arr;
	int nr_regions;
};

static struct damon_sysfs_regions *damon_sysfs_regions_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_regions), GFP_KERNEL);
}

static void damon_sysfs_regions_rm_dirs(struct damon_sysfs_regions *regions)
{
	struct damon_sysfs_region **regions_arr = regions->regions_arr;
	int i;

	for (i = 0; i < regions->nr_regions; i++)
		kobject_put(&regions_arr[i]->kobj);
	kfree(regions_arr);
	regions->regions_arr = NULL;
	regions->nr_regions = 0;
}

static int damon_sysfs_regions_add_dirs(struct damon_sysfs_regions *regions,
		int nr_regions)
{
	struct damon_sysfs_region **regions_arr, *region;
	int err, i;

	damon_sysfs_regions_rm_dirs(regions);
	if (!nr_regions)
		return 0;

	regions_arr = kmalloc(sizeof(*regions_arr) * nr_regions, GFP_KERNEL);
	if (!regions_arr)
		return -ENOMEM;
	regions->regions_arr = regions_arr;

	for (i = 0; i < nr_regions; i++) {
		region = damon_sysfs_region_alloc(0, 0);
		if (!region) {
			damon_sysfs_regions_rm_dirs(regions);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&region->kobj,
				&damon_sysfs_region_ktype, &regions->kobj,
				"%d", i);
		if (err) {
			kfree(region);
			damon_sysfs_regions_rm_dirs(regions);
			return err;
		}

		regions_arr[i] = region;
		regions->nr_regions++;
	}
	return 0;
}

static ssize_t damon_sysfs_regions_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_regions *regions = container_of(kobj,
			struct damon_sysfs_regions, kobj);

	return sysfs_emit(buf, "%d\n", regions->nr_regions);
}

static ssize_t damon_sysfs_regions_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_regions *regions = container_of(kobj,
			struct damon_sysfs_regions, kobj);
	int nr;
	int err = kstrtoint(buf, 10, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	mutex_lock(&damon_sysfs_lock);
	err = damon_sysfs_regions_add_dirs(regions, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_regions_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_regions, kobj));
}

static struct kobj_attribute damon_sysfs_regions_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_regions_nr_show, damon_sysfs_regions_nr_store);

static struct attribute *damon_sysfs_regions_attrs[] = {
	&damon_sysfs_regions_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_regions);

static struct kobj_type damon_sysfs_regions_ktype = {
	.release = damon_sysfs_regions_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_regions_groups,
};

/*
 * target directory
 */

struct damon_sysfs_target {
	struct kobject kobj;
	struct damon_sysfs_regions *regions;
	int pid;
};

static struct damon_sysfs_target *damon_sysfs_target_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_target), GFP_KERNEL);
}

static int damon_sysfs_target_add_dirs(struct damon_sysfs_target *target)
{
	struct damon_sysfs_regions *regions;
	int err;

	regions = damon_sysfs_regions_alloc();
	if (!regions)
		return -ENOMEM;

	err = kobject_init_and_add(&regions->kobj, &damon_sysfs_regions_ktype,
			&target->kobj, "regions");
	if (err) {
		kfree(regions);
		return err;
	}
	target->regions = regions;

	return err;
}

static void damon_sysfs_target_rm_dirs(struct damon_sysfs_target *target)
{
	damon_sysfs_regions_rm_dirs(target->regions);
	kobject_put(&target->regions->kobj);
}

static ssize_t damon_sysfs_target_pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_target *target = container_of(kobj,
			struct damon_sysfs_target, kobj);

	return sysfs_emit(buf, "%d\n", target->pid);
}

static ssize_t damon_sysfs_target_pid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_target *target = container_of(kobj,
			struct damon_sysfs_target, kobj);
	int err = kstrtoint(buf, 10, &target->pid);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_target_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_target, kobj));
}

static struct kobj_attribute damon_sysfs_target_pid_attr = __ATTR(pid, 0600,
		damon_sysfs_target_pid_show, damon_sysfs_target_pid_store);

static struct attribute *damon_sysfs_target_attrs[] = {
	&damon_sysfs_target_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_target);

static struct kobj_type damon_sysfs_target_ktype = {
	.release = damon_sysfs_target_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_target_groups,
};

/*
 * targets directory
 */

struct damon_sysfs_targets {
	struct kobject kobj;
	struct damon_sysfs_target **targets_arr;
	int nr_targets;
};

static struct damon_sysfs_targets *damon_sysfs_targets_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_targets), GFP_KERNEL);
}

static void damon_sysfs_targets_rm_dirs(struct damon_sysfs_targets *targets)
{
	struct damon_sysfs_target **targets_arr = targets->targets_arr;
	int i;

	for (i = 0; i < targets->nr_targets; i++) {
		damon_sysfs_target_rm_dirs(targets_arr[i]);
		kobject_put(&targets_arr[i]->kobj);
	}
	kfree(targets_arr);
	targets->targets_arr = NULL;
	targets->nr_targets = 0;
}

static int damon_sysfs_targets_add_dirs(struct damon_sysfs_targets *targets,
		int nr_targets)
{
	struct damon_sysfs_target **targets_arr, *target;
	int err, i;

	damon_sysfs_targets_rm_dirs(targets);
	if (!nr_targets)
		return 0;

	targets_arr = kmalloc(sizeof(*targets_arr) * nr_targets, GFP_KERNEL);
	if (!targets_arr)
		return -ENOMEM;
	targets->targets_arr = targets_arr;

	for (i = 0; i < nr_targets; i++) {
		target = damon_sysfs_target_alloc();
		if (!target) {
			damon_sysfs_targets_rm_dirs(targets);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&target->kobj,
				&damon_sysfs_target_ktype, &targets->kobj,
				"%d", i);
		if (err) {
			kfree(target);
			damon_sysfs_targets_rm_dirs(targets);
			return err;
		}

		err = damon_sysfs_target_add_dirs(target);
		if (err) {
			kobject_put(&target->kobj);
			damon_sysfs_targets_rm_dirs(targets);
			return err;
		}

		targets_arr[i] = target;
		targets->nr_targets++;
	}
	return 0;
}

static ssize_t damon_sysfs_targets_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_targets *targets = container_of(kobj,
			struct damon_sysfs_targets, kobj);

	return sysfs_emit(buf, "%d\n", targets->nr_targets);
}

static ssize_t damon_sysfs_targets_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_targets *targets = container_of(kobj,
			struct damon_sysfs_targets, kobj);
	int nr;
	int err = kstrtoint(buf, 10, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	mutex_lock(&damon_sysfs_lock);
	err = damon_sysfs_targets_add_dirs(targets, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_targets_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_targets, kobj));
}

static struct kobj_attribute damon_sysfs_targets_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_targets_nr_show, damon_sysfs_targets_nr_store);

static struct attribute *damon_sysfs_targets_attrs[] = {
	&damon_sysfs_targets_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_targets);

static struct kobj_type damon_sysfs_targets_ktype = {
	.release = damon_sysfs_targets_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_targets_groups,
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
	struct damon_sysfs_targets *targets;
	struct damon_sysfs_schemes *schemes;
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
	struct damon_sysfs_targets *targets;
	struct damon_sysfs_schemes *schemes;
	int err;

	/* add monitoring_attrs directory */
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
	if (err)
		goto put_attrs_out;

	/* add targets directory */
	targets = damon_sysfs_targets_alloc();
	if (!targets) {
		err = -ENOMEM;
		goto put_attrs_out;
	}
	err = kobject_init_and_add(&targets->kobj, &damon_sysfs_targets_ktype,
			&context->kobj, "targets");
	if (err) {
		kfree(targets);
		goto put_attrs_out;
	}

	/* add schemes directory */
	schemes = damon_sysfs_schemes_alloc();
	if (!schemes) {
		err = -ENOMEM;
		goto out;
	}
	err = kobject_init_and_add(&schemes->kobj, &damon_sysfs_schemes_ktype,
			&context->kobj, "schemes");
	if (err) {
		kfree(schemes);
		goto out;
	}

	context->attrs = attrs;
	context->targets = targets;
	context->schemes = schemes;
	return err;

out:
	kobject_put(&targets->kobj);
put_attrs_out:
	kobject_put(&attrs->kobj);
	return err;
}

static void damon_sysfs_context_rm_dirs(struct damon_sysfs_context *context)
{
	damon_sysfs_attrs_rm_dirs(context->attrs);
	kobject_put(&context->attrs->kobj);
	damon_sysfs_targets_rm_dirs(context->targets);
	kobject_put(&context->targets->kobj);
	damon_sysfs_schemes_rm_dirs(context->schemes);
	kobject_put(&context->schemes->kobj);
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

static struct kobj_type damon_sysfs_context_ktype = {
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

		err = kobject_init_and_add(&context->kobj,
				&damon_sysfs_context_ktype, &contexts->kobj,
				"%d", i);
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
	if (nr < 0 || 1 < nr)
		return -EINVAL;

	mutex_lock(&damon_sysfs_lock);
	err = damon_sysfs_contexts_add_dirs(contexts, nr);
	mutex_unlock(&damon_sysfs_lock);
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

	mutex_lock(&damon_sysfs_lock);
	err = damon_sysfs_kdamonds_add_dirs(kdamonds, nr);
	mutex_unlock(&damon_sysfs_lock);
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

static ssize_t damon_sysfs_damon_state_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d kdamonds running\n",
			damon_nr_running_ctxs());
}

static void damon_sysfs_destroy_targets(struct damon_ctx *ctx, bool use_pid)
{
	struct damon_target *t, *next;

	damon_for_each_target_safe(t, next, ctx) {
		if (use_pid)
			put_pid(t->pid);
		damon_destroy_target(t);
	}
}

static int damon_sysfs_set_targets(struct damon_ctx *ctx,
		struct damon_sysfs_context *sysfs_ctx)
{
	struct damon_sysfs_targets *targets = sysfs_ctx->targets;
	bool use_pid = sysfs_ctx->damon_type == DAMON_SYSFS_TYPE_VADDR;
	int i;

	for (i = 0; i < targets->nr_targets; i++) {
		struct damon_target *t;

		t = damon_new_target();
		if (!t) {
			damon_sysfs_destroy_targets(ctx, use_pid);
			return -ENOMEM;
		}
		if (use_pid) {
			t->pid = find_get_pid(targets->targets_arr[i]->pid);
			if (!t->pid) {
				damon_sysfs_destroy_targets(ctx, use_pid);
				return -EINVAL;
			}
		}
		damon_add_target(ctx, t);
	}
	return 0;
}

static struct damon_ctx *damon_sysfs_build_ctx(
		struct damon_sysfs_context *sys_ctx)
{
	struct damon_ctx *ctx = damon_new_ctx();
	struct damon_sysfs_attrs *sys_attrs = sys_ctx->attrs;
	struct damon_sysfs_ul_range *sys_nr_regions = sys_attrs->nr_regions;
	struct damon_sysfs_intervals *sys_intervals = sys_attrs->intervals;
	bool use_pid = sys_ctx->damon_type == DAMON_SYSFS_TYPE_VADDR;
	int err;

	if (!ctx)
		return ERR_PTR(-ENOMEM);
	err = damon_set_attrs(ctx, sys_intervals->sample_us,
			sys_intervals->aggr_us, sys_intervals->update_us,
			sys_nr_regions->min, sys_nr_regions->max);
	if (err)
		return ERR_PTR(err);
	err = damon_sysfs_set_targets(ctx, sys_ctx);
	if (err)
		return ERR_PTR(err);
	if (use_pid)
		damon_va_set_primitives(ctx);
	return ctx;
}

static struct damon_ctx **damon_sysfs_build_ctxs(
		struct damon_sysfs_kdamonds *kdamonds, int *nr_ctxs)
{
	struct damon_ctx **ctxs = kmalloc_array(kdamonds->nr, sizeof(*ctxs),
			GFP_KERNEL);
	int i;

	*nr_ctxs = 0;

	if (!ctxs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < kdamonds->nr; i++) {
		struct damon_ctx *ctx;
		int j;

		if (kdamonds->kdamonds_arr[i]->contexts->nr == 0)
			continue;

		ctx = damon_sysfs_build_ctx(
			kdamonds->kdamonds_arr[i]->contexts->contexts_arr[0]);
		if (IS_ERR(ctx)) {
			for (j = 0; j < *nr_ctxs; j++)
				damon_destroy_ctx(ctxs[j]);
			kfree(ctxs);
			*nr_ctxs = 0;
			return ERR_PTR(PTR_ERR(ctx));
		}
		ctxs[(*nr_ctxs)++] = ctx;
	}
	return ctxs;
}

static ssize_t damon_sysfs_damon_state_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_damon *damon = container_of(kobj,
			struct damon_sysfs_damon, kobj);
	struct damon_ctx **ctxs;
	int nr_ctxs, i;
	ssize_t ret;

	mutex_lock(&damon_sysfs_lock);
	if (!strncmp(buf, "start\n", count)) {
		ctxs = damon_sysfs_build_ctxs(damon->kdamonds, &nr_ctxs);
		if (IS_ERR(ctxs)) {
			ret = PTR_ERR(ctxs);
			goto out;
		}
		ret = damon_start(ctxs, nr_ctxs);
		if (ret) {
			for (i = 0; i < nr_ctxs; i++)
				damon_destroy_ctx(ctxs[i]);
			goto out;
		}
		damon_sysfs_ctxs = ctxs;
		damon_sysfs_nr_ctxs = nr_ctxs;
	} else if (!strncmp(buf, "stop\n", count)) {
		ret = damon_stop(damon_sysfs_ctxs, damon_sysfs_nr_ctxs);
		if (ret) {
			goto out;
		}
		for (i = 0; i < damon_sysfs_nr_ctxs; i++)
			damon_destroy_ctx(damon_sysfs_ctxs[i]);
		damon_sysfs_nr_ctxs = 0;
		damon_sysfs_ctxs = NULL;
	} else {
		ret = -EINVAL;
	}
out:
	mutex_unlock(&damon_sysfs_lock);
	if (!ret)
		ret = count;
	return ret;
}

static void damon_sysfs_damon_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_damon, kobj));
}

static struct kobj_attribute damon_sysfs_damon_state_attr =
	__ATTR(state, 0600, damon_sysfs_damon_state_show,
		damon_sysfs_damon_state_store);

static struct attribute *damon_sysfs_damon_attrs[] = {
	&damon_sysfs_damon_state_attr.attr,
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
