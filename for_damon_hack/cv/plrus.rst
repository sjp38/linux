Subject: [PATCH] Extend DAMOS for Proactive LRU-lists Sorting

Changes from RFC
================

Compared to the RFC
(https://lore.kernel.org/damon/20220513150000.25797-1-sj@kernel.org/), this
version of the patchset contains below changes.

- Use more self-explaining DAMOS action names
- Put more evaluation results
- Introduce a static kernel module for easy use of conservatively-tuned
  DAMON-based proactive LRU-lists sorting

Introduction
============

In short, this patchset 1) extends DAMON-based Operation Schemes for proactive
data access-aware LRU-lists sorting, and 2) implements a static kernel module
for easy use of conservatively-tuned version of that.

Background
----------

As page-granularity access checking overhead could be too significant on huge
systems, LRU lists are normally sorted partially and reactively for special
events including specific user requests, system calls and memory pressure.  As
a result, LRU lists are sometimes not so perfectly sorted to be used as a
trustworthy access pattern source for selecting best reclamation target pages
under memory pressure.

Proactive reclamation is well known to be helpful for reducing such non-optimal
reclamation target selection caused performance drops.  However, proactive
reclamation is not a best option for some cases, because it could incur
additional I/O.  For an example, it could be prohitive for systems using
storage devices that total number of writes is limited, or cloud block storages
that charges every I/O.

DAMON-based Proactive LRU-lists Sorting
---------------------------------------

Using DAMON for Proactive LRU-lists Sorting (PLRUS) could be helpful for this
situation, as DAMON can identify access patterns while inducing only
upper-bounded overhead.  The idea is quite simple.  Find hot pages and cold
pages using DAMON, and prioritize hot pages while deprioritizing cold pages on
their LRU-lists.

This patchset extends DAMON to support such scheme by introducing new DAMOS
actions for prioritizing and deprioritizing memory regions of specific access
patterns on their LRU-lists.  In detail, this patchset simply uses
'mark_page_accessed()' and 'deactivate_page()' functions for prioritization and
deprioritization of pages on their LRU lists, respectively.

To make the scheme easy to use without minimum tuning for common situations,
this patchset further implements a static kernel moduel called 'damon_lru_sort'
using the extended functionality.  It does the proactive LRU-lists sorting with
conservatively chosen default hotness/coldness thresholds under small CPU usage
quota limit.  That is, the module will make no harm for common situation but
provide some level of benefit for systems having clearly hot and cold pages
under memory pressure.

Evaluation
==========

In short, PLRUS achieves 10% memory PSI (some) reduction, 14% major page faults
reduction, and 3.74% speedup under memory pressure.

Setup
-----

To show the effect of PLRUS, I run PARSEC3 and SPLASH-2X benchmarks under below
variant systems and measure a few metrics including the runtime of each
workload, number of system-wide major page faults, and system-wide memory PSI
(some).

- orig: Latest mm-unstable kernel + this patchset, but no DAMON scheme applied.
- mprs: Same to orig but have artificial memory pressure.
- plrus: Same to mprs but a radically tuned PLRUS scheme is applied to the
         entire physical address space of the system.

For the artificial memory pressure, I set memory.limit_in_bytes to 75% of the
running workload's peak RSS, wait 1 second, remove the pressure by setting it
to 200% of the peak RSS, wait 10 seconds, and repeat the procedure until the
workload finishes[1].  I use zram based swap device.  The tests are
automated[2].

[1] https://github.com/awslabs/damon-tests/blob/next/perf/runners/back/0009_memcg_pressure.sh
[2] https://github.com/awslabs/damon-tests/tree/next/perf

Radically Tuned PLRUS
---------------------

To show effect of PLRUS on the PARSEC3/SPLASH-2X workloads which runs for no
log time, we use radically tuned version of PLRUS.  The version asks DAMON to

1. find any memory regions shown some accesses (approximately >=20 accesses per
   100 sampling) and prioritize pages of the regions on their LRU lists using
   up to 2% CPU time.  Under the CPU time limit, prioritize regions having
   higher access frequency and kept the access frequency longer first.

2. find any memory regions shown no access for at least >=5 seconds and
   deprioritize pages of the rgions on their LRU lists using up to 2% CPU time.
   Under the CPU time limit, deprioritize regions that not accessed for longer
   time first.

Results
-------

I repeat the tests 25 times and calculate average of the measured numbers.  The
results are as below:

    metric               orig        mprs         plrus        plrus/mprs
    runtime_seconds      190.06      292.83       281.87       0.96
    pgmajfaults          852.55      8769420.00   7525040.00   0.86
    memory_psi_some_us   106911.00   6943420.00   6220920.00   0.90

The first cell shows the metric that the following cells of the row shows.
Second, third, and fourth cells show the metrics under the configs, and the
fifth cell shows the metric under 'plrus' divided by the metric under 'mprs'.
The first row is for legend.  Second row shows the averaged runtime of the
workloads in seconds.  Third row shows the number of system-wide major page
faults while the test was ongoing.  Fourth row shows the system-wide memory
pressure stall information for some processes in microseconds.

In short, PLRUS achieves 10% memory PSI (some) reduction, 14% major page faults
reduction, and 3.74% speedup under memory pressure.  We also confirmed the CPU
usage of kdamond was 2.61% of single CPU, which is below 4% as expected.

Sequence of Patches
===================

The first patch cleans up DAMOS_PAGEOUT handling code of physical address space
monitoring operations implementation for easier extension of the code.  The
second patch implements a new DAMOS action called 'hot', which applies
'mark_page_accessed()' to the pages under the memory regions having the target
access pattern.  Finally, the third patch makes the physical address space
monitoring operations implementation supports the 'cold' action, which applies
'deactivate_page()' to the pages under the memory regions having the target
access pattern.
