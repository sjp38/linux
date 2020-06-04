/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM damon

#if !defined(_TRACE_DAMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DAMON_H

#include <linux/damon.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(damon_aggregated,

	TP_PROTO(struct damon_task *t, struct damon_region *r,
		unsigned int nr_regions),

	TP_ARGS(t, r, nr_regions),

	TP_STRUCT__entry(
		__field(int, pid)
		__field(unsigned int, nr_regions)
		__field(unsigned long, vm_start)
		__field(unsigned long, vm_end)
		__field(unsigned int, nr_accesses)
	),

	TP_fast_assign(
		__entry->pid = t->pid;
		__entry->nr_regions = nr_regions;
		__entry->vm_start = r->vm_start;
		__entry->vm_end = r->vm_end;
		__entry->nr_accesses = r->nr_accesses;
	),

	TP_printk("pid=%d nr_regions=%u %lu-%lu: %u", __entry->pid,
			__entry->nr_regions, __entry->vm_start,
			__entry->vm_end, __entry->nr_accesses)
);

#endif /* _TRACE_DAMON_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
