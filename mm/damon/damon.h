/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: SeongJae Park <sjpark@amazon.de>
 */

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32() % (r - l))

/*
 * 't->id' should be the pointer to the relevant 'struct pid' having reference
 * count.  Caller must put the returned task, unless it is NULL.
 */
#define damon_get_task_struct(t) \
	(get_pid_task((struct pid *)t->id, PIDTYPE_PID))

/*
 * Get the mm_struct of the given target
 *
 * Caller _must_ put the mm_struct after use, unless it is NULL.
 *
 * Returns the mm_struct of the target on success, NULL on failure
 */
static inline struct mm_struct *damon_get_mm(struct damon_target *t)
{
	struct task_struct *task;
	struct mm_struct *mm;

	task = damon_get_task_struct(t);
	if (!task)
		return NULL;

	mm = get_task_mm(task);
	put_task_struct(task);
	return mm;
}
