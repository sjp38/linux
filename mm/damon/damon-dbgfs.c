// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON Debugfs Interface
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon-dbgfs: " fmt

#include <linux/damon.h>
#include <linux/debugfs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/slab.h>

/* Monitoring contexts for debugfs interface users. */
static struct damon_ctx **debugfs_ctxs;
static int debugfs_nr_ctxs = 1;

static DEFINE_MUTEX(damon_dbgfs_lock);

static ssize_t debugfs_monitor_on_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char monitor_on_buf[5];
	bool monitor_on = damon_nr_running_ctxs() != 0;
	int len;

	len = scnprintf(monitor_on_buf, 5, monitor_on ? "on\n" : "off\n");

	return simple_read_from_buffer(buf, count, ppos, monitor_on_buf, len);
}

/*
 * Returns non-empty string on success, negarive error code otherwise.
 */
static char *user_input_str(const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret;

	/* We do not accept continuous write */
	if (*ppos)
		return ERR_PTR(-EINVAL);

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return ERR_PTR(-ENOMEM);

	ret = simple_write_to_buffer(kbuf, count + 1, ppos, buf, count);
	if (ret != count) {
		kfree(kbuf);
		return ERR_PTR(-EIO);
	}
	kbuf[ret] = '\0';

	return kbuf;
}

static ssize_t debugfs_monitor_on_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret = count;
	char *kbuf;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* Remove white space */
	if (sscanf(kbuf, "%s", kbuf) != 1)
		return -EINVAL;
	if (!strncmp(kbuf, "on", count))
		err = damon_start_ctx_ptrs(debugfs_ctxs, debugfs_nr_ctxs);
	else if (!strncmp(kbuf, "off", count))
		err = damon_stop_ctx_ptrs(debugfs_ctxs, debugfs_nr_ctxs);
	else
		return -EINVAL;

	if (err)
		ret = err;
	return ret;
}

static ssize_t sprint_schemes(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damos *s;
	int written = 0;
	int rc;

	damon_for_each_scheme(s, c) {
		rc = scnprintf(&buf[written], len - written,
				"%lu %lu %u %u %u %u %d %lu %lu\n",
				s->min_sz_region, s->max_sz_region,
				s->min_nr_accesses, s->max_nr_accesses,
				s->min_age_region, s->max_age_region,
				s->action, s->stat_count, s->stat_sz);
		if (!rc)
			return -ENOMEM;

		written += rc;
	}
	return written;
}

static ssize_t debugfs_schemes_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	len = sprint_schemes(ctx, kbuf, count);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static void free_schemes_arr(struct damos **schemes, ssize_t nr_schemes)
{
	ssize_t i;

	for (i = 0; i < nr_schemes; i++)
		kfree(schemes[i]);
	kfree(schemes);
}

static bool damos_action_valid(int action)
{
	switch (action) {
	case DAMOS_WILLNEED:
	case DAMOS_COLD:
	case DAMOS_PAGEOUT:
	case DAMOS_HUGEPAGE:
	case DAMOS_NOHUGEPAGE:
	case DAMOS_STAT:
		return true;
	default:
		return false;
	}
}

/*
 * Converts a string into an array of struct damos pointers
 *
 * Returns an array of struct damos pointers that converted if the conversion
 * success, or NULL otherwise.
 */
static struct damos **str_to_schemes(const char *str, ssize_t len,
				ssize_t *nr_schemes)
{
	struct damos *scheme, **schemes;
	const int max_nr_schemes = 256;
	int pos = 0, parsed, ret;
	unsigned long min_sz, max_sz;
	unsigned int min_nr_a, max_nr_a, min_age, max_age;
	unsigned int action;

	schemes = kmalloc_array(max_nr_schemes, sizeof(scheme),
			GFP_KERNEL);
	if (!schemes)
		return NULL;

	*nr_schemes = 0;
	while (pos < len && *nr_schemes < max_nr_schemes) {
		ret = sscanf(&str[pos], "%lu %lu %u %u %u %u %u%n",
				&min_sz, &max_sz, &min_nr_a, &max_nr_a,
				&min_age, &max_age, &action, &parsed);
		if (ret != 7)
			break;
		if (!damos_action_valid(action)) {
			pr_err("wrong action %d\n", action);
			goto fail;
		}

		pos += parsed;
		scheme = damon_new_scheme(min_sz, max_sz, min_nr_a, max_nr_a,
				min_age, max_age, action);
		if (!scheme)
			goto fail;

		schemes[*nr_schemes] = scheme;
		*nr_schemes += 1;
	}
	return schemes;
fail:
	free_schemes_arr(schemes, *nr_schemes);
	return NULL;
}

static ssize_t debugfs_schemes_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	struct damos **schemes;
	ssize_t nr_schemes = 0, ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	schemes = str_to_schemes(kbuf, ret, &nr_schemes);
	if (!schemes) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_schemes(ctx, schemes, nr_schemes);
	if (err)
		ret = err;
	else
		nr_schemes = 0;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	free_schemes_arr(schemes, nr_schemes);
out:
	kfree(kbuf);
	return ret;
}

#define targetid_is_pid(ctx)	\
	(ctx->target_valid == kdamond_vm_target_valid)

static ssize_t sprint_target_ids(struct damon_ctx *ctx, char *buf, ssize_t len)
{
	struct damon_target *t;
	unsigned long id;
	int written = 0;
	int rc;

	damon_for_each_target(t, ctx) {
		id = t->id;
		if (targetid_is_pid(ctx))
			/* Show pid numbers to debugfs users */
			id = (unsigned long)pid_vnr((struct pid *)id);

		rc = scnprintf(&buf[written], len - written, "%lu ", id);
		if (!rc)
			return -ENOMEM;
		written += rc;
	}
	if (written)
		written -= 1;
	written += scnprintf(&buf[written], len - written, "\n");
	return written;
}

static ssize_t debugfs_target_ids_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	ssize_t len;
	char ids_buf[320];

	mutex_lock(&ctx->kdamond_lock);
	len = sprint_target_ids(ctx, ids_buf, 320);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		return len;

	return simple_read_from_buffer(buf, count, ppos, ids_buf, len);
}

/*
 * Converts a string into an array of unsigned long integers
 *
 * Returns an array of unsigned long integers if the conversion success, or
 * NULL otherwise.
 */
static unsigned long *str_to_target_ids(const char *str, ssize_t len,
					ssize_t *nr_ids)
{
	unsigned long *ids;
	const int max_nr_ids = 32;
	unsigned long id;
	int pos = 0, parsed, ret;

	*nr_ids = 0;
	ids = kmalloc_array(max_nr_ids, sizeof(id), GFP_KERNEL);
	if (!ids)
		return NULL;
	while (*nr_ids < max_nr_ids && pos < len) {
		ret = sscanf(&str[pos], "%lu%n", &id, &parsed);
		pos += parsed;
		if (ret != 1)
			break;
		ids[*nr_ids] = id;
		*nr_ids += 1;
	}

	return ids;
}

/* Returns pid for the given pidfd if it's valid, or NULL otherwise. */
static struct pid *damon_get_pidfd_pid(unsigned int pidfd)
{
	struct fd f;
	struct pid *pid;

	f = fdget(pidfd);
	if (!f.file)
		return NULL;

	pid = pidfd_pid(f.file);
	if (!IS_ERR(pid))
		get_pid(pid);
	else
		pid = NULL;

	fdput(f);
	return pid;
}

static ssize_t debugfs_target_ids_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf, *nrs;
	bool received_pidfds = false;
	unsigned long *targets;
	ssize_t nr_targets;
	ssize_t ret = count;
	int i;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	nrs = kbuf;
	if (!strncmp(kbuf, "paddr\n", count)) {
		/* Configure the context for physical memory monitoring */
		damon_set_paddr_primitives(ctx);
		/* target id is meaningless here, but we set it just for fun */
		scnprintf(kbuf, count, "42    ");
	} else {
		/* Configure the context for virtual memory monitoring */
		damon_set_vaddr_primitives(ctx);
		if (!strncmp(kbuf, "pidfd ", 6)) {
			received_pidfds = true;
			nrs = &kbuf[6];
		}
	}

	targets = str_to_target_ids(nrs, ret, &nr_targets);
	if (!targets) {
		ret = -ENOMEM;
		goto out;
	}

	if (received_pidfds) {
		for (i = 0; i < nr_targets; i++)
			targets[i] = (unsigned long)damon_get_pidfd_pid(
					(unsigned int)targets[i]);
	} else if (targetid_is_pid(ctx)) {
		for (i = 0; i < nr_targets; i++)
			targets[i] = (unsigned long)find_get_pid(
					(int)targets[i]);
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EINVAL;
		goto unlock_out;
	}

	err = damon_set_targets(ctx, targets, nr_targets);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	kfree(targets);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t debugfs_record_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char record_buf[20 + MAX_RFILE_PATH_LEN];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = scnprintf(record_buf, ARRAY_SIZE(record_buf), "%u %s\n",
			ctx->rbuf_len, ctx->rfile_path);
	mutex_unlock(&ctx->kdamond_lock);
	return simple_read_from_buffer(buf, count, ppos, record_buf, ret);
}

static ssize_t debugfs_record_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	unsigned int rbuf_len;
	char rfile_path[MAX_RFILE_PATH_LEN];
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (sscanf(kbuf, "%u %s",
				&rbuf_len, rfile_path) != 2) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_recording(ctx, rbuf_len, rfile_path);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t sprint_init_regions(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r;
	int written = 0;
	int rc;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t) {
			rc = scnprintf(&buf[written], len - written,
					"%lu %lu %lu\n",
					t->id, r->ar.start, r->ar.end);
			if (!rc)
				return -ENOMEM;
			written += rc;
		}
	}
	return written;
}

static ssize_t debugfs_init_regions_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		mutex_unlock(&ctx->kdamond_lock);
		return -EBUSY;
	}

	len = sprint_init_regions(ctx, kbuf, count);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static int add_init_region(struct damon_ctx *c,
			 unsigned long target_id, struct damon_addr_range *ar)
{
	struct damon_target *t;
	struct damon_region *r, *prev;
	int rc = -EINVAL;

	if (ar->start >= ar->end)
		return -EINVAL;

	damon_for_each_target(t, c) {
		if (t->id == target_id) {
			r = damon_new_region(ar->start, ar->end);
			if (!r)
				return -ENOMEM;
			damon_add_region(r, t);
			if (nr_damon_regions(t) > 1) {
				prev = damon_prev_region(r);
				if (prev->ar.end > r->ar.start) {
					damon_destroy_region(r);
					return -EINVAL;
				}
			}
			rc = 0;
		}
	}
	return rc;
}

static int set_init_regions(struct damon_ctx *c, const char *str, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r, *next;
	int pos = 0, parsed, ret;
	unsigned long target_id;
	struct damon_addr_range ar;
	int err;

	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}

	while (pos < len) {
		ret = sscanf(&str[pos], "%lu %lu %lu%n",
				&target_id, &ar.start, &ar.end, &parsed);
		if (ret != 3)
			break;
		err = add_init_region(c, target_id, &ar);
		if (err)
			goto fail;
		pos += parsed;
	}

	return 0;

fail:
	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r);
	}
	return err;
}

static ssize_t debugfs_init_regions_write(struct file *file,
					  const char __user *buf, size_t count,
					  loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = set_init_regions(ctx, kbuf, ret);
	if (err)
		ret = err;

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	kfree(kbuf);
	return ret;
}

static ssize_t debugfs_attrs_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char kbuf[128];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = scnprintf(kbuf, ARRAY_SIZE(kbuf), "%lu %lu %lu %lu %lu\n",
			ctx->sample_interval, ctx->aggr_interval,
			ctx->regions_update_interval, ctx->min_nr_regions,
			ctx->max_nr_regions);
	mutex_unlock(&ctx->kdamond_lock);

	return simple_read_from_buffer(buf, count, ppos, kbuf, ret);
}

static ssize_t debugfs_attrs_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	unsigned long s, a, r, minr, maxr;
	char *kbuf;
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (sscanf(kbuf, "%lu %lu %lu %lu %lu",
				&s, &a, &r, &minr, &maxr) != 5) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_attrs(ctx, s, a, r, minr, maxr);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t debugfs_nr_contexts_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char kbuf[32];
	int ret;

	mutex_lock(&damon_dbgfs_lock);
	ret = scnprintf(kbuf, ARRAY_SIZE(kbuf), "%d\n", debugfs_nr_ctxs);
	mutex_unlock(&damon_dbgfs_lock);

	return simple_read_from_buffer(buf, count, ppos, kbuf, ret);
}

static struct dentry **debugfs_dirs;

static int debugfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx);

static ssize_t debugfs_nr_contexts_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret = count;
	int nr_contexts, i;
	char dirname[32];
	struct dentry *root;
	struct dentry **new_dirs;
	struct damon_ctx **new_ctxs;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (sscanf(kbuf, "%d", &nr_contexts) != 1) {
		ret = -EINVAL;
		goto out;
	}
	if (nr_contexts < 1) {
		pr_err("nr_contexts should be >=1\n");
		ret = -EINVAL;
		goto out;
	}
	if (nr_contexts == debugfs_nr_ctxs)
		goto out;

	mutex_lock(&damon_dbgfs_lock);
	if (damon_nr_running_ctxs()) {
		ret = -EBUSY;
		goto unlock_out;
	}

	for (i = nr_contexts; i < debugfs_nr_ctxs; i++) {
		debugfs_remove(debugfs_dirs[i]);
		damon_destroy_ctx(debugfs_ctxs[i]);
	}

	new_dirs = kmalloc_array(nr_contexts, sizeof(*new_dirs), GFP_KERNEL);
	if (!new_dirs) {
		ret = -ENOMEM;
		goto unlock_out;
	}

	new_ctxs = kmalloc_array(nr_contexts, sizeof(*debugfs_ctxs),
			GFP_KERNEL);
	if (!new_ctxs) {
		ret = -ENOMEM;
		goto unlock_out;
	}

	for (i = 0; i < debugfs_nr_ctxs && i < nr_contexts; i++) {
		new_dirs[i] = debugfs_dirs[i];
		new_ctxs[i] = debugfs_ctxs[i];
	}
	kfree(debugfs_dirs);
	debugfs_dirs = new_dirs;
	kfree(debugfs_ctxs);
	debugfs_ctxs = new_ctxs;

	root = debugfs_dirs[0];
	if (!root) {
		ret = -ENOENT;
		goto unlock_out;
	}

	for (i = debugfs_nr_ctxs; i < nr_contexts; i++) {
		scnprintf(dirname, sizeof(dirname), "ctx%d", i);
		debugfs_dirs[i] = debugfs_create_dir(dirname, root);
		if (!debugfs_dirs[i]) {
			pr_err("dir %s creation failed\n", dirname);
			ret = -ENOMEM;
			break;
		}

		debugfs_ctxs[i] = damon_new_ctx();
		if (!debugfs_ctxs[i]) {
			pr_err("ctx for %s creation failed\n", dirname);
			ret = -ENOMEM;
			break;
		}

		if (debugfs_fill_ctx_dir(debugfs_dirs[i], debugfs_ctxs[i])) {
			ret = -ENOMEM;
			break;
		}
	}

	debugfs_nr_ctxs = i;

unlock_out:
	mutex_unlock(&damon_dbgfs_lock);

out:
	kfree(kbuf);
	return ret;
}

static int damon_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return nonseekable_open(inode, file);
}

static const struct file_operations monitor_on_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_monitor_on_read,
	.write = debugfs_monitor_on_write,
};

static const struct file_operations target_ids_fops = {
	.owner = THIS_MODULE,
	.open = damon_debugfs_open,
	.read = debugfs_target_ids_read,
	.write = debugfs_target_ids_write,
};

static const struct file_operations schemes_fops = {
	.owner = THIS_MODULE,
	.open = damon_debugfs_open,
	.read = debugfs_schemes_read,
	.write = debugfs_schemes_write,
};

static const struct file_operations record_fops = {
	.owner = THIS_MODULE,
	.open = damon_debugfs_open,
	.read = debugfs_record_read,
	.write = debugfs_record_write,
};

static const struct file_operations init_regions_fops = {
	.owner = THIS_MODULE,
	.open = damon_debugfs_open,
	.read = debugfs_init_regions_read,
	.write = debugfs_init_regions_write,
};

static const struct file_operations attrs_fops = {
	.owner = THIS_MODULE,
	.open = damon_debugfs_open,
	.read = debugfs_attrs_read,
	.write = debugfs_attrs_write,
};

static const struct file_operations nr_contexts_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_nr_contexts_read,
	.write = debugfs_nr_contexts_write,
};

static int debugfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx)
{
	const char * const file_names[] = {"attrs", "init_regions", "record",
		"schemes", "target_ids"};
	const struct file_operations *fops[] = {&attrs_fops,
		&init_regions_fops, &record_fops, &schemes_fops,
		&target_ids_fops};
	int i;

	for (i = 0; i < ARRAY_SIZE(file_names); i++) {
		if (!debugfs_create_file(file_names[i], 0600, dir,
					ctx, fops[i])) {
			pr_err("failed to create %s file\n", file_names[i]);
			return -ENOMEM;
		}
	}

	return 0;
}

static int __init damon_debugfs_init(void)
{
	struct dentry *debugfs_root;
	const char * const file_names[] = {"nr_contexts", "monitor_on"};
	const struct file_operations *fops[] = {&nr_contexts_fops,
		&monitor_on_fops};
	int i;

	debugfs_root = debugfs_create_dir("damon", NULL);
	if (!debugfs_root) {
		pr_err("failed to create the debugfs dir\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(file_names); i++) {
		if (!debugfs_create_file(file_names[i], 0600, debugfs_root,
					NULL, fops[i])) {
			pr_err("failed to create %s file\n", file_names[i]);
			return -ENOMEM;
		}
	}
	debugfs_fill_ctx_dir(debugfs_root, debugfs_ctxs[0]);

	debugfs_dirs = kmalloc_array(1, sizeof(debugfs_root), GFP_KERNEL);
	debugfs_dirs[0] = debugfs_root;

	return 0;
}

/*
 * Functions for the initialization
 */

static int __init damon_dbgfs_init(void)
{
	int rc;

	debugfs_ctxs = kmalloc(sizeof(*debugfs_ctxs), GFP_KERNEL);
	debugfs_ctxs[0] = damon_new_ctx();
	if (!debugfs_ctxs[0])
		return -ENOMEM;

	rc = damon_debugfs_init();
	if (rc)
		pr_err("%s: debugfs init failed\n", __func__);

	return rc;
}

module_init(damon_dbgfs_init);

#include "damon-dbgfs-test.h"
