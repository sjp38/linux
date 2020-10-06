Subject: [RFC PATCH] Introduce DAMON-based Proactive LRU-lists Sorting

Introduction
============

As page-granularity access checking overhead could be unnegligible on huge
systems and arbitrarily increase as the size of system's memory grows, LRU
lists are normally sorted reactively and partially for some events including
explicit system calls and memory pressure.  This could result in not so ideally
sorted LRU lists, which could make a bad reclamation target pages selection,
and thus performance degradation under memory pressure.

Proactive reclamation could be helpful for minimizing the memory pressure
performance drops.  However, such reclamation could incur additional I/O, so
not a best option for some cases.  For an example, cloud block storages would
charge each I/O.  Also, as the artificially triggered reclaim will do page
granularity access checking, the overhead might not be negligible.

Using DAMON for Proactive LRU-lists Sorting (PLRUS) could be helpful for
improving this situation, as DAMON can identify access patterns while inducing
only controlled overhead.  The idea is simple.  Find hot pages and cold pages
using DAMON, and do 'mark_page_accessed()' for the hot pages while doing
'deactivate_page()' for the cold pages.  This patchset extends DAMON to be able
to be used for the purpose by introducing a new DAMOS action for doing the
'mark_page_accessed()' to memory regions of a specific access pattern, and
supporting 'cold' DAMOS action from the physical address space monitoring
operations set.

In terms of making reclamation less harmful, PLRUS will work similar to the
proactive reclamation, but reduce additional I/O when memory is enough.  Of
course, PLRUS will not reduce memory utilization on its own, unlike proactive
reclamation.  If that's a problem, doing DAMON-based proactive reclamation
(DAMON_RECLAIM) simultaneously for only super cold pages could work.  One
additional advantage of PLRUS is that it makes LRU lists a more trustworthy
source of access patterns.

Example DAMON-based Operation Schemes for PLRUS
===============================================

So, user will be able to do PLRUS via DAMON-based Operation Schemes (DAMOS)
after applying this patchset.  An example of such DAMOS config for PLRUS would
be something like below (Sorry for the crippy format).  In short, this config
asks DAMON to
1. find any memory regions of >=4K size having shown at least some access and
'mark_accessed()' regions in those having higher access frequency and kept the
access frequency longer first, using up to 5% CPU time, while
2. finding any memory regions of >=4K size having shown no access at all and
'deactivate()' regions in those have kept the no access frequency longer first,
using up to 5% CPU time.

	# format is:
	# <min/max size> <min/max frequency (0-100)> <min/max age> <action> \
	# 		<quota> <weights> <watermarks>

	# LRU-activate hot pages, more hot pages first, under 5% CPU usage limit
	4K  max         5 max           min max         hot \
			50ms 0B 1s      0 3 7   free_mem_rate 5s 1000 999 0

	# LRU-deactive cold pages, colder pages first under 5% CPU usage limit
	4K  max         min min         min max         cold \
			50ms 0B 1s      0 3 7   free_mem_rate 5s 1000 999 0

Evaluation
==========

To show the effect of PLRUS, I did below simple test.

1. Run a workload having 870MB peak RSS (parsec3.canneal).
2. Trigger artificial memory pressure by setting memory.limit_in_bytes to 80%
   of its peak RSS, wait 3 seconds, remove the pressure by setting it to 200%
   of its peak RSS, wait 30 seconds, and repeat until the workload finishes[1].
3. Measure the PSI full total time.

The PSI full total time increase during the test was 35 seconds when above
example PLRUS scheme is not applied, and it has reduced to 21 seconds after the
scheme is applied system-wide.  In short, above example PLRUS scheme achieved
about 40% memory pressure stall reduction.

[1] https://github.com/awslabs/damon-tests/blob/next/perf/runners/back/0009_memcg_pressure.sh

Complete Git Tree
=================

You can get the complete git tree from
https://git.kernel.org/sj/h/damon/plurs/rfc/v1.

Sequence of Patches
===================

