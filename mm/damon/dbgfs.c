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

struct debugfs_recorder {
	unsigned char *rbuf;
	unsigned int rbuf_len;
	unsigned int rbuf_offset;
	char *rfile_path;
};

/* Monitoring contexts for debugfs interface users. */
static struct damon_ctx **debugfs_ctxs;
static int debugfs_nr_ctxs = 1;
static int debugfs_nr_terminated_ctxs;

static DEFINE_MUTEX(damon_dbgfs_lock);

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

/*
 * Flush the content in the result buffer to the result file
 */
static void debugfs_flush_rbuffer(struct debugfs_recorder *rec)
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
static void debugfs_write_rbuf(struct damon_ctx *ctx, void *data, ssize_t size)
{
	struct debugfs_recorder *rec = (struct debugfs_recorder *)ctx->private;

	if (!rec->rbuf_len || !rec->rbuf || !rec->rfile_path)
		return;
	if (rec->rbuf_offset + size > rec->rbuf_len)
		debugfs_flush_rbuffer(ctx->private);
	if (rec->rbuf_offset + size > rec->rbuf_len) {
		pr_warn("%s: flush failed, or wrong size given(%u, %zu)\n",
				__func__, rec->rbuf_offset, size);
		return;
	}

	memcpy(&rec->rbuf[rec->rbuf_offset], data, size);
	rec->rbuf_offset += size;
}

static void debugfs_write_record_header(struct damon_ctx *ctx)
{
	int recfmt_ver = 2;

	debugfs_write_rbuf(ctx, "damon_recfmt_ver", 16);
	debugfs_write_rbuf(ctx, &recfmt_ver, sizeof(recfmt_ver));
}

static void debugfs_init_vm_regions(struct damon_ctx *ctx)
{
	debugfs_write_record_header(ctx);
	damon_va_init_regions(ctx);
}

static void debugfs_unlock_page_idle_lock(void)
{
	mutex_lock(&damon_dbgfs_lock);
	if (++debugfs_nr_terminated_ctxs == debugfs_nr_ctxs) {
		debugfs_nr_terminated_ctxs = 0;
		mutex_unlock(&page_idle_lock);
	}
	mutex_unlock(&damon_dbgfs_lock);
}

static void debugfs_vm_cleanup(struct damon_ctx *ctx)
{
	debugfs_flush_rbuffer(ctx->private);
	debugfs_unlock_page_idle_lock();
	damon_va_cleanup(ctx);
}

static void debugfs_init_phys_regions(struct damon_ctx *ctx)
{
	debugfs_write_record_header(ctx);
}

static void debugfs_phys_cleanup(struct damon_ctx *ctx)
{
	debugfs_flush_rbuffer(ctx->private);
	debugfs_unlock_page_idle_lock();

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
static void debugfs_aggregate_cb(struct damon_ctx *c)
{
	struct damon_target *t;
	struct timespec64 now;
	unsigned int nr;

	ktime_get_coarse_ts64(&now);

	debugfs_write_rbuf(c, &now, sizeof(now));
	nr = nr_damon_targets(c);
	debugfs_write_rbuf(c, &nr, sizeof(nr));

	damon_for_each_target(t, c) {
		struct damon_region *r;

		debugfs_write_rbuf(c, &t->id, sizeof(t->id));
		nr = damon_nr_regions(t);
		debugfs_write_rbuf(c, &nr, sizeof(nr));
		damon_for_each_region(r, t) {
			debugfs_write_rbuf(c, &r->ar.start, sizeof(r->ar.start));
			debugfs_write_rbuf(c, &r->ar.end, sizeof(r->ar.end));
			debugfs_write_rbuf(c, &r->nr_accesses,
					sizeof(r->nr_accesses));
		}
	}
}

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

static int debugfs_start_ctx_ptrs(struct damon_ctx **ctxs, int nr_ctxs)
{
	int rc;

	if (!mutex_trylock(&page_idle_lock))
		return -EBUSY;

	rc = damon_start_ctx_ptrs(ctxs, nr_ctxs);
	if (rc)
		mutex_unlock(&page_idle_lock);

	return rc;
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
		err = debugfs_start_ctx_ptrs(debugfs_ctxs, debugfs_nr_ctxs);
	else if (!strncmp(kbuf, "off", count))
		err = damon_stop_ctx_ptrs(debugfs_ctxs, debugfs_nr_ctxs);
	else
		return -EINVAL;

	if (err)
		ret = err;
	return ret;
}

static ssize_t debugfs_kdamond_pid_read(struct file *file,
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
	(ctx->target_valid == damon_va_target_valid)

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

static void debugfs_set_vaddr_primitives(struct damon_ctx *ctx)
{
	damon_set_vaddr_primitives(ctx);
	ctx->init_target_regions = debugfs_init_vm_regions;
	ctx->cleanup = debugfs_vm_cleanup;
}

static void debugfs_set_paddr_primitives(struct damon_ctx *ctx)
{
	damon_set_paddr_primitives(ctx);
	ctx->init_target_regions = debugfs_init_phys_regions;
	ctx->cleanup = debugfs_phys_cleanup;
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
		debugfs_set_paddr_primitives(ctx);
		/* target id is meaningless here, but we set it just for fun */
		scnprintf(kbuf, count, "42    ");
	} else {
		/* Configure the context for virtual memory monitoring */
		debugfs_set_vaddr_primitives(ctx);

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
	struct debugfs_recorder *rec = ctx->private;
	char record_buf[20 + MAX_RFILE_PATH_LEN];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = scnprintf(record_buf, ARRAY_SIZE(record_buf), "%u %s\n",
			rec->rbuf_len, rec->rfile_path);
	mutex_unlock(&ctx->kdamond_lock);
	return simple_read_from_buffer(buf, count, ppos, record_buf, ret);
}

/*
 * debugfs_set_recording() - Set attributes for the recording.
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
static int debugfs_set_recording(struct damon_ctx *ctx,
			unsigned int rbuf_len, char *rfile_path)
{
	struct debugfs_recorder *recorder;
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

	recorder = ctx->private;
	if (!recorder) {
		recorder = kzalloc(sizeof(*recorder), GFP_KERNEL);
		if (!recorder)
			return -ENOMEM;
		ctx->private = recorder;
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

	err = debugfs_set_recording(ctx, rbuf_len, rfile_path);
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
			if (damon_nr_regions(t) > 1) {
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

static struct dentry **debugfs_dirs;

static int debugfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx);

static void debugfs_free_recorder(struct debugfs_recorder *recorder)
{
	kfree(recorder->rbuf);
	kfree(recorder->rfile_path);
	kfree(recorder);
}

static struct damon_ctx *debugfs_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = damon_new_ctx();
	if (!ctx)
		return NULL;

	if (debugfs_set_recording(ctx, 0, "none")) {
		damon_destroy_ctx(ctx);
		return NULL;
	}

	debugfs_set_vaddr_primitives(ctx);
	ctx->aggregate_cb = debugfs_aggregate_cb;
	return ctx;
}

static void debugfs_destroy_ctx(struct damon_ctx *ctx)
{
	debugfs_free_recorder(ctx->private);
	damon_destroy_ctx(ctx);
}

static ssize_t debugfs_mk_context_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret = count;
	char *ctx_name;
	struct dentry *root, **new_dirs;
	struct damon_ctx **new_ctxs;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);
	ctx_name = kmalloc(count + 1, GFP_KERNEL);
	if (!ctx_name) {
		kfree(kbuf);
		return -ENOMEM;
	}

	/* Trim white space */
	if (sscanf(kbuf, "%s", ctx_name) != 1) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&damon_dbgfs_lock);
	if (damon_nr_running_ctxs()) {
		ret = -EBUSY;
		goto unlock_out;
	}

	new_ctxs = krealloc(debugfs_ctxs, sizeof(*debugfs_ctxs) *
			(debugfs_nr_ctxs + 1), GFP_KERNEL);
	if (!new_ctxs) {
		ret = -ENOMEM;
		goto unlock_out;
	}
	new_dirs = krealloc(debugfs_dirs, sizeof(*debugfs_dirs) *
			(debugfs_nr_ctxs + 1), GFP_KERNEL);
	if (!new_dirs) {
		kfree(new_ctxs);
		ret = -ENOMEM;
		goto unlock_out;
	}

	debugfs_ctxs = new_ctxs;
	debugfs_dirs = new_dirs;

	root = debugfs_dirs[0];
	if (!root) {
		ret = -ENOENT;
		goto unlock_out;
	}

	debugfs_dirs[debugfs_nr_ctxs] = debugfs_create_dir(ctx_name, root);
	if (!debugfs_dirs[debugfs_nr_ctxs]) {
		pr_err("dir %s creation failed\n", ctx_name);
		ret = -ENOMEM;
		goto unlock_out;
	}

	debugfs_ctxs[debugfs_nr_ctxs] = debugfs_new_ctx();
	if (!debugfs_ctxs[debugfs_nr_ctxs]) {
		pr_err("ctx %s creation failed\n", ctx_name);
		debugfs_remove(debugfs_dirs[debugfs_nr_ctxs]);
		ret = -ENOMEM;
		goto unlock_out;
	}

	if (debugfs_fill_ctx_dir(debugfs_dirs[debugfs_nr_ctxs],
				debugfs_ctxs[debugfs_nr_ctxs])) {
		ret = -ENOMEM;
		goto unlock_out;
	}

	debugfs_nr_ctxs++;

unlock_out:
	mutex_unlock(&damon_dbgfs_lock);

out:
	kfree(kbuf);
	kfree(ctx_name);
	return ret;
}

static ssize_t debugfs_rm_context_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret = count;
	char *ctx_name;
	struct dentry *root, *dir, **new_dirs;
	struct damon_ctx **new_ctxs;
	int i, j;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);
	ctx_name = kmalloc(count + 1, GFP_KERNEL);
	if (!ctx_name) {
		kfree(kbuf);
		return -ENOMEM;
	}

	/* Trim white space */
	if (sscanf(kbuf, "%s", ctx_name) != 1) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&damon_dbgfs_lock);
	if (damon_nr_running_ctxs()) {
		ret = -EBUSY;
		goto unlock_out;
	}

	root = debugfs_dirs[0];
	if (!root) {
		ret = -ENOENT;
		goto unlock_out;
	}

	dir = debugfs_lookup(ctx_name, root);
	if (!dir) {
		ret = -ENOENT;
		goto unlock_out;
	}

	new_dirs = kmalloc_array(debugfs_nr_ctxs - 1, sizeof(*debugfs_dirs),
			GFP_KERNEL);
	if (!new_dirs) {
		ret = -ENOMEM;
		goto unlock_out;
	}
	new_ctxs = kmalloc_array(debugfs_nr_ctxs - 1, sizeof(*debugfs_ctxs),
			GFP_KERNEL);
	if (!new_ctxs) {
		kfree(new_dirs);
		ret = -ENOMEM;
		goto unlock_out;
	}

	for (i = 0, j = 0; i < debugfs_nr_ctxs; i++) {
		if (debugfs_dirs[i] == dir) {
			debugfs_remove(debugfs_dirs[i]);
			debugfs_destroy_ctx(debugfs_ctxs[i]);
			continue;
		}
		new_dirs[j] = debugfs_dirs[i];
		new_ctxs[j++] = debugfs_ctxs[i];
	}

	debugfs_dirs = new_dirs;
	debugfs_ctxs = new_ctxs;
	debugfs_nr_ctxs--;

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

static const struct file_operations kdamond_pid = {
	.owner = THIS_MODULE,
	.open = damon_debugfs_open,
	.read = debugfs_kdamond_pid_read,
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

static const struct file_operations mk_contexts_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_mk_context_write,
};

static const struct file_operations rm_contexts_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_rm_context_write,
};

static int debugfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx)
{
	const char * const file_names[] = {"attrs", "init_regions", "record",
		"schemes", "target_ids", "kdamond_pid"};
	const struct file_operations *fops[] = {&attrs_fops,
		&init_regions_fops, &record_fops, &schemes_fops,
		&target_ids_fops, &kdamond_pid};
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
	const char * const file_names[] = { "mk_contexts", "rm_contexts",
		"monitor_on"};
	const struct file_operations *fops[] = { &mk_contexts_fops,
		&rm_contexts_fops, &monitor_on_fops};
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
	debugfs_ctxs[0] = debugfs_new_ctx();
	if (!debugfs_ctxs[0])
		return -ENOMEM;

	rc = damon_debugfs_init();
	if (rc)
		pr_err("%s: debugfs init failed\n", __func__);

	return rc;
}

module_init(damon_dbgfs_init);

#include "dbgfs-test.h"
