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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/slab.h>

#define MIN_RECORD_BUFFER_LEN	1024
#define MAX_RECORD_BUFFER_LEN	(4 * 1024 * 1024)
#define MAX_RFILE_PATH_LEN	256

struct dbgfs_recorder {
	unsigned char *rbuf;
	unsigned int rbuf_len;
	unsigned int rbuf_offset;
	char *rfile_path;
};

static struct damon_ctx **dbgfs_ctxs;
static int dbgfs_nr_ctxs;
static struct dentry **dbgfs_dirs;

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

static ssize_t dbgfs_attrs_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char kbuf[128];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = scnprintf(kbuf, ARRAY_SIZE(kbuf), "%lu %lu %lu %lu %lu\n",
			ctx->sample_interval, ctx->aggr_interval,
			ctx->primitive_update_interval, ctx->min_nr_regions,
			ctx->max_nr_regions);
	mutex_unlock(&ctx->kdamond_lock);

	return simple_read_from_buffer(buf, count, ppos, kbuf, ret);
}

static ssize_t dbgfs_attrs_write(struct file *file,
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

static ssize_t dbgfs_record_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	struct dbgfs_recorder *rec = ctx->callback.private;
	char record_buf[20 + MAX_RFILE_PATH_LEN];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = scnprintf(record_buf, ARRAY_SIZE(record_buf), "%u %s\n",
			rec->rbuf_len, rec->rfile_path);
	mutex_unlock(&ctx->kdamond_lock);
	return simple_read_from_buffer(buf, count, ppos, record_buf, ret);
}

/*
 * dbgfs_set_recording() - Set attributes for the recording.
 * @ctx:	target kdamond context
 * @rbuf_len:	length of the result buffer
 * @rfile_path:	path to the monitor result files
 *
 * Setting 'rbuf_len' 0 disables recording.
 *
 * This function should not be called while the kdamond is running.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int dbgfs_set_recording(struct damon_ctx *ctx,
			unsigned int rbuf_len, char *rfile_path)
{
	struct dbgfs_recorder *recorder;
	size_t rfile_path_len;

	if (rbuf_len && (rbuf_len > MAX_RECORD_BUFFER_LEN ||
			rbuf_len < MIN_RECORD_BUFFER_LEN)) {
		pr_err("result buffer size (%u) is out of [%d,%d]\n",
				rbuf_len, MIN_RECORD_BUFFER_LEN,
				MAX_RECORD_BUFFER_LEN);
		return -EINVAL;
	}
	rfile_path_len = strnlen(rfile_path, MAX_RFILE_PATH_LEN);
	if (rfile_path_len >= MAX_RFILE_PATH_LEN) {
		pr_err("too long (>%d) result file path %s\n",
				MAX_RFILE_PATH_LEN, rfile_path);
		return -EINVAL;
	}

	recorder = ctx->callback.private;
	if (!recorder) {
		recorder = kzalloc(sizeof(*recorder), GFP_KERNEL);
		if (!recorder)
			return -ENOMEM;
		ctx->callback.private = recorder;
	}

	recorder->rbuf_len = rbuf_len;
	kfree(recorder->rbuf);
	recorder->rbuf = NULL;
	kfree(recorder->rfile_path);
	recorder->rfile_path = NULL;

	if (rbuf_len) {
		recorder->rbuf = kvmalloc(rbuf_len, GFP_KERNEL);
		if (!recorder->rbuf)
			return -ENOMEM;
	}
	recorder->rfile_path = kmalloc(rfile_path_len + 1, GFP_KERNEL);
	if (!recorder->rfile_path)
		return -ENOMEM;
	strncpy(recorder->rfile_path, rfile_path, rfile_path_len + 1);

	return 0;
}

static ssize_t dbgfs_record_write(struct file *file,
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

	err = dbgfs_set_recording(ctx, rbuf_len, rfile_path);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	kfree(kbuf);
	return ret;
}

#define targetid_is_pid(ctx)	\
	(ctx->primitive.target_valid == damon_va_target_valid)

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

static ssize_t dbgfs_target_ids_read(struct file *file,
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

static void dbgfs_put_pids(unsigned long *ids, int nr_ids)
{
	int i;

	for (i = 0; i < nr_ids; i++)
		put_pid((struct pid *)ids[i]);
}

static ssize_t dbgfs_target_ids_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf, *nrs;
	unsigned long *targets;
	ssize_t nr_targets;
	ssize_t ret = count;
	int i;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	nrs = kbuf;

	targets = str_to_target_ids(nrs, ret, &nr_targets);
	if (!targets) {
		ret = -ENOMEM;
		goto out;
	}

	if (targetid_is_pid(ctx)) {
		for (i = 0; i < nr_targets; i++) {
			targets[i] = (unsigned long)find_get_pid(
					(int)targets[i]);
			if (!targets[i]) {
				dbgfs_put_pids(targets, i);
				ret = -EINVAL;
				goto free_targets_out;
			}
		}
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		if (targetid_is_pid(ctx))
			dbgfs_put_pids(targets, nr_targets);
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_targets(ctx, targets, nr_targets);
	if (err) {
		if (targetid_is_pid(ctx))
			dbgfs_put_pids(targets, nr_targets);
		ret = err;
	}

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
free_targets_out:
	kfree(targets);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t dbgfs_kdamond_pid_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond)
		len = scnprintf(kbuf, count, "%d\n", ctx->kdamond->pid);
	else
		len = scnprintf(kbuf, count, "none\n");
	mutex_unlock(&ctx->kdamond_lock);
	if (!len)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static int damon_dbgfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return nonseekable_open(inode, file);
}

static const struct file_operations attrs_fops = {
	.owner = THIS_MODULE,
	.open = damon_dbgfs_open,
	.read = dbgfs_attrs_read,
	.write = dbgfs_attrs_write,
};

static const struct file_operations record_fops = {
	.owner = THIS_MODULE,
	.open = damon_dbgfs_open,
	.read = dbgfs_record_read,
	.write = dbgfs_record_write,
};

static const struct file_operations target_ids_fops = {
	.owner = THIS_MODULE,
	.open = damon_dbgfs_open,
	.read = dbgfs_target_ids_read,
	.write = dbgfs_target_ids_write,
};

static const struct file_operations kdamond_pid_fops = {
	.owner = THIS_MODULE,
	.open = damon_dbgfs_open,
	.read = dbgfs_kdamond_pid_read,
};

static int dbgfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx)
{
	const char * const file_names[] = {"attrs", "record", "target_ids",
		"kdamond_pid"};
	const struct file_operations *fops[] = {&attrs_fops, &record_fops,
		&target_ids_fops, &kdamond_pid_fops};
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

/*
 * Flush the content in the result buffer to the result file
 */
static void dbgfs_flush_rbuffer(struct dbgfs_recorder *rec)
{
	ssize_t sz;
	loff_t pos = 0;
	struct file *rfile;

	if (!rec->rbuf_offset)
		return;

	rfile = filp_open(rec->rfile_path,
			O_CREAT | O_RDWR | O_APPEND | O_LARGEFILE, 0644);
	if (IS_ERR(rfile)) {
		pr_err("Cannot open the result file %s\n",
				rec->rfile_path);
		return;
	}

	while (rec->rbuf_offset) {
		sz = kernel_write(rfile, rec->rbuf, rec->rbuf_offset, &pos);
		if (sz < 0)
			break;
		rec->rbuf_offset -= sz;
	}
	filp_close(rfile, NULL);
}

/*
 * Write a data into the result buffer
 */
static void dbgfs_write_rbuf(struct damon_ctx *ctx, void *data, ssize_t size)
{
	struct dbgfs_recorder *rec = ctx->callback.private;

	if (!rec->rbuf_len || !rec->rbuf || !rec->rfile_path)
		return;
	if (rec->rbuf_offset + size > rec->rbuf_len)
		dbgfs_flush_rbuffer(ctx->callback.private);
	if (rec->rbuf_offset + size > rec->rbuf_len) {
		pr_warn("%s: flush failed, or wrong size given(%u, %zu)\n",
				__func__, rec->rbuf_offset, size);
		return;
	}

	memcpy(&rec->rbuf[rec->rbuf_offset], data, size);
	rec->rbuf_offset += size;
}

static void dbgfs_write_record_header(struct damon_ctx *ctx)
{
	int recfmt_ver = 2;

	dbgfs_write_rbuf(ctx, "damon_recfmt_ver", 16);
	dbgfs_write_rbuf(ctx, &recfmt_ver, sizeof(recfmt_ver));
}

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

static int dbgfs_before_start(struct damon_ctx *ctx)
{
	dbgfs_write_record_header(ctx);
	return 0;
}

/*
 * Store the aggregated monitoring results to the result buffer
 *
 * The format for the result buffer is as below:
 *
 *   <time> <number of targets> <array of target infos>
 *
 *   target info: <id> <number of regions> <array of region infos>
 *   region info: <start address> <end address> <nr_accesses>
 */
static int dbgfs_after_aggregation(struct damon_ctx *c)
{
	struct damon_target *t;
	struct timespec64 now;
	unsigned int nr;

	ktime_get_coarse_ts64(&now);

	dbgfs_write_rbuf(c, &now, sizeof(now));
	nr = nr_damon_targets(c);
	dbgfs_write_rbuf(c, &nr, sizeof(nr));

	damon_for_each_target(t, c) {
		struct damon_region *r;

		dbgfs_write_rbuf(c, &t->id, sizeof(t->id));
		nr = damon_nr_regions(t);
		dbgfs_write_rbuf(c, &nr, sizeof(nr));
		damon_for_each_region(r, t) {
			dbgfs_write_rbuf(c, &r->ar.start, sizeof(r->ar.start));
			dbgfs_write_rbuf(c, &r->ar.end, sizeof(r->ar.end));
			dbgfs_write_rbuf(c, &r->nr_accesses,
					sizeof(r->nr_accesses));
		}
	}

	return 0;
}

static int dbgfs_before_terminate(struct damon_ctx *ctx)
{
	dbgfs_flush_rbuffer(ctx->callback.private);
	return 0;
}

static struct damon_ctx *dbgfs_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = damon_new_ctx(DAMON_ADAPTIVE_TARGET);
	if (!ctx)
		return NULL;

	if (dbgfs_set_recording(ctx, 0, "none")) {
		damon_destroy_ctx(ctx);
		return NULL;
	}

	damon_va_set_primitives(ctx);
	ctx->callback.before_start = dbgfs_before_start;
	ctx->callback.after_aggregation = dbgfs_after_aggregation;
	ctx->callback.before_terminate = dbgfs_before_terminate;
	return ctx;
}

static ssize_t dbgfs_monitor_on_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char monitor_on_buf[5];
	bool monitor_on = damon_nr_running_ctxs() != 0;
	int len;

	len = scnprintf(monitor_on_buf, 5, monitor_on ? "on\n" : "off\n");

	return simple_read_from_buffer(buf, count, ppos, monitor_on_buf, len);
}

static ssize_t dbgfs_monitor_on_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret = count;
	char *kbuf;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* Remove white space */
	if (sscanf(kbuf, "%s", kbuf) != 1) {
		kfree(kbuf);
		return -EINVAL;
	}

	if (!strncmp(kbuf, "on", count))
		err = damon_start(dbgfs_ctxs, dbgfs_nr_ctxs);
	else if (!strncmp(kbuf, "off", count))
		err = damon_stop(dbgfs_ctxs, dbgfs_nr_ctxs);
	else
		err = -EINVAL;

	if (err)
		ret = err;
	kfree(kbuf);
	return ret;
}

static const struct file_operations monitor_on_fops = {
	.owner = THIS_MODULE,
	.read = dbgfs_monitor_on_read,
	.write = dbgfs_monitor_on_write,
};

static int __init __damon_dbgfs_init(void)
{
	struct dentry *dbgfs_root;
	const char * const file_names[] = {"monitor_on"};
	const struct file_operations *fops[] = {&monitor_on_fops};
	int i;

	dbgfs_root = debugfs_create_dir("damon", NULL);
	if (IS_ERR(dbgfs_root)) {
		pr_err("failed to create the dbgfs dir\n");
		return PTR_ERR(dbgfs_root);
	}

	for (i = 0; i < ARRAY_SIZE(file_names); i++) {
		if (!debugfs_create_file(file_names[i], 0600, dbgfs_root,
					NULL, fops[i])) {
			pr_err("failed to create %s file\n", file_names[i]);
			return -ENOMEM;
		}
	}
	dbgfs_fill_ctx_dir(dbgfs_root, dbgfs_ctxs[0]);

	dbgfs_dirs = kmalloc_array(1, sizeof(dbgfs_root), GFP_KERNEL);
	dbgfs_dirs[0] = dbgfs_root;

	return 0;
}

/*
 * Functions for the initialization
 */

static int __init damon_dbgfs_init(void)
{
	int rc;

	dbgfs_ctxs = kmalloc(sizeof(*dbgfs_ctxs), GFP_KERNEL);
	dbgfs_ctxs[0] = dbgfs_new_ctx();
	if (!dbgfs_ctxs[0])
		return -ENOMEM;
	dbgfs_nr_ctxs = 1;

	rc = __damon_dbgfs_init();
	if (rc)
		pr_err("%s: dbgfs init failed\n", __func__);

	return rc;
}

module_init(damon_dbgfs_init);
