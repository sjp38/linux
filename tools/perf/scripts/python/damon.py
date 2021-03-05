# SPDX-License-Identifier: GPL-2.0
#
# Author: SeongJae Park <sjpark@amazon.de>

from __future__ import print_function

import argparse
import os
import sys
import time

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *

# For intensive print() calls, 'IOError: [Errno 11] Resource temporarily
# unavailable' triggered.  This function handles the error.
def pr_safe(*args):
	while True:
		try:
			print(*args)
			return
		except IOError:
			time.sleep(0.1)

class Region:
	start = None
	end = None
	nr_accesses = None

	def __init__(self, start, end, nr_accesses):
		self.start = start
		self.end = end
		self.nr_accesses = nr_accesses

class Snapshot:
	monitored_time = None
	target_id = None
	regions = None

	def __init__(self, monitored_time, target_id):
		self.monitored_time = monitored_time
		self.target_id = target_id
		self.regions = []

class Record:
	start_time = None
	snapshots = None

	def __init__(self, start_time):
		self.start_time = start_time
		self.snapshots = []

def trace_begin():
	pass

def trace_end():
	print_record_raw(record, args.sz_bytes)

args = None
record = None
nr_read_regions = 0
def damon__damon_aggregated(event_name, context, common_cpu,
		common_secs, common_nsecs, common_pid, common_comm,
		common_callchain, target_id, nr_regions, start, end,
		nr_accesses, perf_sample_dict):
	global record
	global nr_read_regions

	time = common_secs * 1000000000 + common_nsecs
	if not record:
		record = Record(time)

	if nr_read_regions == 0:
		snapshot = Snapshot(time, target_id)
		record.snapshots.append(snapshot)

	snapshot = record.snapshots[-1]
	snapshot.regions.append(Region(start, end, nr_accesses))

	nr_read_regions += 1
	if nr_read_regions == nr_regions:
		nr_read_regions = 0

def format_sz(number, sz_bytes):
	if sz_bytes:
		return '%d' % number

	if number > 1<<40:
		return '%.3f TiB' % (number / (1<<40))
	if number > 1<<30:
		return '%.3f GiB' % (number / (1<<30))
	if number > 1<<20:
		return '%.3f MiB' % (number / (1<<20))
	if number > 1<<10:
		return '%.3f KiB' % (number / (1<<10))
	return '%d B' % number

def print_record_raw(record, sz_bytes):
	pr_safe('start_time:', record.start_time)
	for snapshot in record.snapshots:
		pr_safe('relative_time:',
				snapshot.monitored_time - record.start_time)
		pr_safe('target_id:', snapshot.target_id)
		pr_safe('nr_regions:', len(snapshot.regions))
		for region in snapshot.regions:
			pr_safe('%x-%x (%s): %d' % (region.start, region.end,
				format_sz(region.end - region.start, sz_bytes),
				region.nr_accesses))
		pr_safe()

def main():
	global args

	parser = argparse.ArgumentParser()
	parser.add_argument('--sz-bytes', action='store_true',
			help='report size in bytes')
	args = parser.parse_args()

if __name__ == '__main__':
	main()
