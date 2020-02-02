/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM damon

#if !defined(_TRACE_DAMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DAMON_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(damon_write_rbuf,

	TP_PROTO(void *buf, const ssize_t sz),

	TP_ARGS(buf, sz),

	TP_STRUCT__entry(
		__dynamic_array(char, buf, sz)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(buf), buf, sz);
	),

	TP_printk("dat=%s", __print_hex(__get_dynamic_array(buf),
			__get_dynamic_array_len(buf)))
);

#endif /* _TRACE_DAMON_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
