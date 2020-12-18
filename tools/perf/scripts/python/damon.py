# SPDX-License-Identifier: GPL-2.0
#
# Author: SeongJae Park <sjpark@amazon.de>

from __future__ import print_function

import os
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *


def trace_begin():
	pass

def trace_end():
	pass

start_time = None
nr_printed = 0
def damon__damon_aggregated(event_name, context, common_cpu,
		common_secs, common_nsecs, common_pid, common_comm,
		common_callchain, target_id, nr_regions, start, end,
		nr_accesses, perf_sample_dict):
	global start_time
	global nr_printed
	time = common_secs * 1000000000 + common_nsecs
	if not start_time:
		start_time = time
		print('start_time: %d' % start_time)
	if nr_printed == 0:
		print('rel time: %d' % (time - start_time))
		print('target_id: %d' % target_id)
		print('nr_regions: %d' % nr_regions)
	print('%x-%x (%d): %u' % (start, end, end - start, nr_accesses))

	nr_printed += 1
	if nr_printed == nr_regions:
		nr_printed = 0
		print()
