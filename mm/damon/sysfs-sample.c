// SPDX-License-Identifier: FPL-2.0
/*
 * DAMON sample sysfs directory implementation
 */

#include <linux/slab.h>

#include "sysfs-common.h"

/*
 * access check report filter directory
 */

struct damon_sysfs_sample_filter {
	struct kobject kobj;
	enum damon_sample_filter_type type;
	bool matching;
	bool allow;
	cpumask_t cpumask;
	int *tid_arr;	/* first entry is the length of the array */
};

static struct damon_sysfs_sample_filter *damon_sysfs_sample_filter_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_sample_filter), GFP_KERNEL);
}

struct damon_sysfs_sample_filter_type_name {
	enum damon_sample_filter_type type;
	char *name;
};

static const struct damon_sysfs_sample_filter_type_name
damon_sysfs_sample_filter_type_names[] = {
	{
		.type = DAMON_FILTER_TYPE_CPUMASK,
		.name = "cpumask",
	},
	{
		.type = DAMON_FILTER_TYPE_THREADS,
		.name = "threads",
	},
	{
		.type = DAMON_FILTER_TYPE_WRITE,
		.name = "write",
	},
};

static ssize_t type_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	int i = 0;

	for (; i < ARRAY_SIZE(damon_sysfs_sample_filter_type_names); i++) {
		const struct damon_sysfs_sample_filter_type_name *type_name;

		type_name = &damon_sysfs_sample_filter_type_names[i];
		if (type_name->type == filter->type)
			return sysfs_emit(buf, "%s\n", type_name->name);
	}
	return -EINVAL;
}

static ssize_t type_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	ssize_t ret = -EINVAL;
	int i = 0;

	for (; i < ARRAY_SIZE(damon_sysfs_sample_filter_type_names); i++) {
		const struct damon_sysfs_sample_filter_type_name *type_name;

		type_name = &damon_sysfs_sample_filter_type_names[i];
		if (sysfs_streq(buf, type_name->name)) {
			filter->type = type_name->type;
			ret = count;
			break;
		}
	}
	return ret;
}

static ssize_t matching_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);

	return sysfs_emit(buf, "%c\n", filter->matching ? 'Y' : 'N');
}

static ssize_t matching_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	bool matching;
	int err = kstrtobool(buf, &matching);

	if (err)
		return err;

	filter->matching = matching;
	return count;
}

static ssize_t allow_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);

	return sysfs_emit(buf, "%c\n", filter->allow ? 'Y' : 'N');
}

static ssize_t allow_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	bool allow;
	int err = kstrtobool(buf, &allow);

	if (err)
		return err;

	filter->allow = allow;
	return count;
}

static ssize_t cpumask_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);

	return sysfs_emit(buf, "%*pbl\n", cpumask_pr_args(&filter->cpumask));
}

static ssize_t cpumask_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	cpumask_t cpumask;
	int err = cpulist_parse(buf, &cpumask);

	if (err)
		return err;
	filter->cpumask = cpumask;
	return count;
}

static ssize_t tid_arr_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_sample_filter *sample_filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	char *str;
	int nr_tids, *tid_arr;
	int i, ret;

	if (!sample_filter->tid_arr)
		return sysfs_emit(buf, "\n");

	str = kcalloc(2048, sizeof(*str), GFP_KERNEL);
	if (!str)
		return -ENOMEM;
	nr_tids = sample_filter->tid_arr[0];
	tid_arr = &sample_filter->tid_arr[1];
	for (i = 0; i < nr_tids; i++) {
		snprintf(&str[strlen(str)], 2048 - strlen(str), "%d",
				tid_arr[i]);
		if (i < nr_tids - 1)
			snprintf(&str[strlen(str)], 2048 - strlen(str), ",");
	}
	ret = sysfs_emit(buf, "%s\n", str);
	kfree(str);
	return ret;
}

static ssize_t tid_arr_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_sample_filter *sample_filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);
	int err;

	err = parse_int_array(buf, count, &sample_filter->tid_arr);
	if (err)
		return err;
	return count;
}

static void damon_sysfs_sample_filter_release(struct kobject *kobj)
{
	struct damon_sysfs_sample_filter *filter = container_of(kobj,
			struct damon_sysfs_sample_filter, kobj);

	kfree(filter);
}

static struct kobj_attribute damon_sysfs_sample_filter_type_attr =
		__ATTR_RW_MODE(type, 0600);

static struct kobj_attribute damon_sysfs_sample_filter_matching_attr =
		__ATTR_RW_MODE(matching, 0600);

static struct kobj_attribute damon_sysfs_sample_filter_allow_attr =
		__ATTR_RW_MODE(allow, 0600);

static struct kobj_attribute damon_sysfs_sample_filter_cpumask_attr =
		__ATTR_RW_MODE(cpumask, 0600);

static struct kobj_attribute damon_sysfs_sample_filter_tid_arr_attr =
		__ATTR_RW_MODE(tid_arr, 0600);

static struct attribute *damon_sysfs_sample_filter_attrs[] = {
	&damon_sysfs_sample_filter_type_attr.attr,
	&damon_sysfs_sample_filter_matching_attr.attr,
	&damon_sysfs_sample_filter_allow_attr.attr,
	&damon_sysfs_sample_filter_cpumask_attr.attr,
	&damon_sysfs_sample_filter_tid_arr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_sample_filter);

static const struct kobj_type damon_sysfs_sample_filter_ktype = {
	.release = damon_sysfs_sample_filter_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_sample_filter_groups,
};

/*
 * access check report filters directory
 */

struct damon_sysfs_sample_filters {
	struct kobject kobj;
	struct damon_sysfs_sample_filter **filters_arr;
	int nr;
};

static struct damon_sysfs_sample_filters *
damon_sysfs_sample_filters_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_sample_filters), GFP_KERNEL);
}

static void damon_sysfs_sample_filters_rm_dirs(
		struct damon_sysfs_sample_filters *filters)
{
	struct damon_sysfs_sample_filter **filters_arr = filters->filters_arr;
	int i;

	for (i = 0; i < filters->nr; i++)
		kobject_put(&filters_arr[i]->kobj);
	filters->nr = 0;
	kfree(filters_arr);
	filters->filters_arr = NULL;
}

static int damon_sysfs_sample_filters_add_dirs(
		struct damon_sysfs_sample_filters *filters, int nr_filters)
{
	struct damon_sysfs_sample_filter **filters_arr, *filter;
	int err, i;

	damon_sysfs_sample_filters_rm_dirs(filters);
	if (!nr_filters)
		return 0;

	filters_arr = kmalloc_array(nr_filters, sizeof(*filters_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!filters_arr)
		return -ENOMEM;
	filters->filters_arr = filters_arr;

	for (i = 0; i < nr_filters; i++) {
		filter = damon_sysfs_sample_filter_alloc();
		if (!filter) {
			damon_sysfs_sample_filters_rm_dirs(filters);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&filter->kobj,
				&damon_sysfs_sample_filter_ktype,
				&filters->kobj, "%d", i);
		if (err) {
			kobject_put(&filter->kobj);
			damon_sysfs_sample_filters_rm_dirs(filters);
			return err;
		}

		filters_arr[i] = filter;
		filters->nr++;
	}
	return 0;
}

static ssize_t nr_filters_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_sample_filters *filters = container_of(kobj,
			struct damon_sysfs_sample_filters, kobj);

	return sysfs_emit(buf, "%d\n", filters->nr);
}

static ssize_t nr_filters_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_sample_filters *filters;
	int nr, err = kstrtoint(buf, 0, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	filters = container_of(kobj, struct damon_sysfs_sample_filters, kobj);

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_sample_filters_add_dirs(filters, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_sample_filters_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_sample_filters, kobj));
}

static struct kobj_attribute damon_sysfs_sample_filters_nr_attr =
		__ATTR_RW_MODE(nr_filters, 0600);

static struct attribute *damon_sysfs_sample_filters_attrs[] = {
	&damon_sysfs_sample_filters_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_sample_filters);

static const struct kobj_type damon_sysfs_sample_filters_ktype = {
	.release = damon_sysfs_sample_filters_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_sample_filters_groups,
};

/*
 * access check primitives directory
 */

struct damon_sysfs_primitives {
	struct kobject kobj;
	bool page_table;
	bool page_fault;
};

static struct damon_sysfs_primitives *damon_sysfs_primitives_alloc(
		bool page_table, bool page_fault)
{
	struct damon_sysfs_primitives *primitives = kmalloc(
			sizeof(*primitives), GFP_KERNEL);

	if (!primitives)
		return NULL;

	primitives->kobj = (struct kobject){};
	primitives->page_table = page_table;
	primitives->page_fault = page_fault;
	return primitives;
}

static ssize_t page_table_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_primitives *primitives = container_of(kobj,
			struct damon_sysfs_primitives, kobj);

	return sysfs_emit(buf, "%c\n", primitives->page_table ? 'Y' : 'N');
}

static ssize_t page_table_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_primitives *primitives = container_of(kobj,
			struct damon_sysfs_primitives, kobj);
	bool enable;
	int err = kstrtobool(buf, &enable);

	if (err)
		return err;
	primitives->page_table = enable;
	return count;
}

static ssize_t page_fault_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_primitives *primitives = container_of(kobj,
			struct damon_sysfs_primitives, kobj);

	return sysfs_emit(buf, "%c\n", primitives->page_fault ? 'Y' : 'N');
}

static ssize_t page_fault_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_primitives *primitives = container_of(kobj,
			struct damon_sysfs_primitives, kobj);
	bool enable;
	int err = kstrtobool(buf, &enable);

	if (err)
		return err;
	primitives->page_fault = enable;
	return count;
}

static void damon_sysfs_primitives_release(struct kobject *kobj)
{
	struct damon_sysfs_primitives *primitives = container_of(kobj,
			struct damon_sysfs_primitives, kobj);

	kfree(primitives);
}

static struct kobj_attribute damon_sysfs_primitives_page_table_attr =
		__ATTR_RW_MODE(page_table, 0600);

static struct kobj_attribute damon_sysfs_primitives_page_fault_attr =
		__ATTR_RW_MODE(page_fault, 0600);

static struct attribute *damon_sysfs_primitives_attrs[] = {
	&damon_sysfs_primitives_page_table_attr.attr,
	&damon_sysfs_primitives_page_fault_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_primitives);

static const struct kobj_type damon_sysfs_primitives_ktype = {
	.release = damon_sysfs_primitives_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_primitives_groups,
};

/*
 * sample directory
 */

struct damon_sysfs_sample *damon_sysfs_sample_alloc(void)
{
	struct damon_sysfs_sample *sample = kmalloc(
			sizeof(*sample), GFP_KERNEL);

	if (!sample)
		return NULL;
	sample->kobj = (struct kobject){};
	return sample;
}

int damon_sysfs_sample_add_dirs(struct damon_sysfs_sample *sample)
{
	struct damon_sysfs_primitives *primitives;
	struct damon_sysfs_sample_filters *filters;
	int err;

	primitives = damon_sysfs_primitives_alloc(true, false);
	if (!primitives)
		return -ENOMEM;
	err = kobject_init_and_add(&primitives->kobj,
			&damon_sysfs_primitives_ktype, &sample->kobj,
			"primitives");
	if (err)
		goto put_primitives_out;
	sample->primitives = primitives;

	filters = damon_sysfs_sample_filters_alloc();
	if (!filters) {
		err = -ENOMEM;
		goto put_primitives_out;
	}
	err = kobject_init_and_add(&filters->kobj,
			&damon_sysfs_sample_filters_ktype, &sample->kobj,
			"filters");
	if (err)
		goto put_filters_out;
	sample->filters = filters;
	return 0;
put_filters_out:
	kobject_put(&filters->kobj);
	sample->filters = NULL;
put_primitives_out:
	kobject_put(&primitives->kobj);
	sample->primitives = NULL;
	return err;
}

void damon_sysfs_sample_rm_dirs(struct damon_sysfs_sample *sample)
{
	if (sample->primitives)
		kobject_put(&sample->primitives->kobj);
	if (sample->filters) {
		damon_sysfs_sample_filters_rm_dirs(sample->filters);
		kobject_put(&sample->filters->kobj);
	}
}

void damon_sysfs_sample_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_sample, kobj));
}

static struct attribute *damon_sysfs_sample_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_sample);

const struct kobj_type damon_sysfs_sample_ktype = {
	.release = damon_sysfs_sample_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_sample_groups,
};

static int damon_sysfs_set_threads_filter(struct damon_sample_filter *filter,
		int *sysfs_tid_arr)
{
	int nr_tids, i;
	pid_t *tid_arr;

	if (!sysfs_tid_arr)
		return -EINVAL;
	nr_tids = sysfs_tid_arr[0];
	tid_arr = kmalloc_array(nr_tids, sizeof(*tid_arr), GFP_KERNEL);
	if (!tid_arr)
		return -ENOMEM;
	for (i = 0; i < nr_tids; i++)
		tid_arr[i] = sysfs_tid_arr[i + 1];
	filter->tid_arr = tid_arr;
	filter->nr_tids = nr_tids;
	return 0;
}

static int damon_sysfs_set_sample_filters(
		struct damon_sample_control *control,
		struct damon_sysfs_sample_filters *sysfs_filters)
{
	int i, err;

	for (i = 0; i < sysfs_filters->nr; i++) {
		struct damon_sysfs_sample_filter *sysfs_filter =
			sysfs_filters->filters_arr[i];
		struct damon_sample_filter *filter;

		filter = damon_new_sample_filter(
				sysfs_filter->type, sysfs_filter->matching,
				sysfs_filter->allow);
		if (!filter)
			return -ENOMEM;
		switch (filter->type) {
		case DAMON_FILTER_TYPE_CPUMASK:
			filter->cpumask = sysfs_filter->cpumask;
			break;
		case DAMON_FILTER_TYPE_THREADS:
			err = damon_sysfs_set_threads_filter(filter,
					sysfs_filter->tid_arr);
			if (err)
				damon_free_sample_filter(filter);
			break;
		default:
			break;
		}
		damon_add_sample_filter(control, filter);
	}
	return 0;
}


int damon_sysfs_set_sample_control(
		struct damon_sample_control *control,
		struct damon_sysfs_sample *sysfs_sample)
{
	control->primitives_enabled.page_table =
		sysfs_sample->primitives->page_table;
	control->primitives_enabled.page_fault =
		sysfs_sample->primitives->page_fault;

	return damon_sysfs_set_sample_filters(control,
			sysfs_sample->filters);
}
