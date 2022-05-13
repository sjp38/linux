Subject: [RFC PATCH] Extend DAMOS for Proactive LRU-lists Sorting

Introduction
============

As page-granularity access checking overhead could be too significant on huge
systems, LRU lists are normally sorted reactively and partially for special
events including explicit system calls and memory pressure.  As a result, LRU
lists could be not well sorted to be used for finding good reclamation target
pages, especially when memory pressure is first happened after a while.

Proactive reclamation is well known to be helpful for minimizing the memory
pressure performance drops.  However, proactive reclamation could incur
additional I/O, so not a best option for some cases.  For an example, cloud
block storages would charge each I/O.

Using DAMON for Proactive LRU-lists Sorting (PLRUS) could be helpful for this
situation, as DAMON can identify access patterns while inducing only controlled
overhead.  The idea is simple.  Find hot pages and cold pages using DAMON, and
do 'mark_page_accessed()' for the hot pages while doing 'deactivate_page()' for
the cold pages.  This patchset extends DAMON to support PLRUS by introducing a
new DAMOS action for doing the 'mark_page_accessed()' to memory regions of a
specific access pattern, and supporting 'cold' DAMOS action from the physical
address space monitoring operations set.

In terms of making reclamation less harmful, PLRUS will work similar to the
proactive reclamation, but avoids the additional I/Os.  Of course, PLRUS will
not reduce memory utilization on its own, unlike proactive reclamation.  If
that's a problem, doing DAMON-based proactive reclamation (DAMON_RECLAIM)
simultaneously for only super cold pages, or for severe memory pressure could
work.  One additional advantage of PLRUS is that it makes LRU lists a more
trustworthy source of access patterns.

Example DAMON-based Operation Schemes for PLRUS
===============================================

So, users will be able to do PLRUS via DAMON-based Operation Schemes (DAMOS)
after applying this patchset.  An example of such DAMOS config for PLRUS would
be something like below.  Sorry for the crippy format.  Please refer to the
parser script[1] for detail of the format.  In short, this config asks DAMON to

1. find any memory regions of >=4K size having shown at least some access
   (approximately 20 accesses per 100 sampling) and apply 'mark_accessed()' to
   those using up to 2% CPU time.  Under the CPU time limit, apply the function
   to regions having higher access frequency and kept the access frequency
   longer first.

2. find any memory regions of >=4K size having shown no access for 200ms or
   more and 'deactivate()' those using up to 2% CPU time.  Under the CPU time
   limit, apply the function to regions kept the no access longer first.

    # format is:
    # <min/max size> <min/max frequency (0-100)> <min/max age> <action> \
    # 		<quota> <weights> <watermarks>
 
    # LRU-activate hot pages (more hot ones first) under 2% CPU usage limit
    4K  max         20 max           min max         hot \
    		20ms 0B 1s      0 7 3   free_mem_rate 5s 1000 999 0
 
    # LRU-deactivate cold pages (colder ones first) under 2% CPU usage limit
    4K  max         min min         20ms max         cold \
    		20ms 0B 1s      0 3 7   free_mem_rate 5s 1000 999 0

[1] https://github.com/awslabs/damo/blob/next/_convert_damos.py

Evaluation
==========

To show the effect of PLRUS, I ran PARSEC3 and SPLASH-2X benchmarks under below
variant kernels and measured the runtime of each workload.

- orig: Latest mm-unstable kernel + this patchset, but no DAMON scheme applied.
- mprs: Same to orig but have artificial memory pressure.
- plrus: Same to mprs but above example PLRUS scheme is applied to the physical
         address space of the system.

For the artificial memory pressure, I set memory.limit_in_bytes to 75% of the
running workload's peak RSS, wait 3 seconds, remove the pressure by setting it
to 200% of the running workload's peak RSS, wait 30 seconds, and repeat the
procedure until the workload finishes[1].  The tests are automated[2].

I repeat the tests five times and calculate average runtime of the five
measurements.  The results are as below:

    runtime_secs            orig    mprs    mprs_overhead_percent plrus   plrus_overhead_percent
    parsec3/blackscholes    139.35  139.68  0.24                  140.37  0.74
    parsec3/bodytrack       124.67  127.26  2.08                  128.31  2.92
    parsec3/canneal         207.61  400.95  93.12                 355.23  71.10
    parsec3/dedup           18.30   18.84   2.94                  19.30   5.47
    parsec3/facesim         350.42  353.69  0.93                  349.14  -0.36
    parsec3/fluidanimate    338.57  337.16  -0.42                 342.18  1.07
    parsec3/freqmine        434.39  435.67  0.30                  436.49  0.48
    parsec3/raytrace        182.24  186.18  2.16                  189.08  3.75
    parsec3/streamcluster   634.49  2993.27 371.76                2576.04 306.00
    parsec3/swaptions       221.68  221.84  0.07                  221.97  0.13
    parsec3/vips            87.82   103.01  17.30                 103.18  17.48
    parsec3/x264            108.92  132.82  21.93                 128.22  17.71
    splash2x/barnes         130.30  135.87  4.27                  138.52  6.31
    splash2x/fft            62.09   98.33   58.37                 99.85   60.82
    splash2x/lu_cb          132.15  135.49  2.53                  135.22  2.32
    splash2x/lu_ncb         149.89  154.92  3.36                  155.26  3.59
    splash2x/ocean_cp       80.04   108.20  35.18                 113.85  42.24
    splash2x/ocean_ncp      163.70  217.40  32.81                 231.09  41.17
    splash2x/radiosity      142.32  143.13  0.57                  144.50  1.53
    splash2x/radix          50.28   78.21   55.55                 85.96   70.98
    splash2x/raytrace       133.75  134.21  0.34                  136.21  1.84
    splash2x/volrend        120.39  121.72  1.10                  120.87  0.40
    splash2x/water_nsquared 373.37  388.31  4.00                  398.72  6.79
    splash2x/water_spatial  133.81  143.73  7.42                  144.00  7.61
    average                 188.36  304.58  61.70                 287.23  52.49

On average, 'plrus' achieves 5% speedup under memory pressure.  For the two
best cases (parsec3/canneal and parsec3/streamcluster), 'plrus' achieves 11%
and 13.93% speedup under memory pressure.  Given that the scheme is not highly
tuned, applied to entire system memory, and uses only up to 4% single CPU time,
the improvement could be helpful for some cases.

[1] https://github.com/awslabs/damon-tests/blob/next/perf/runners/back/0009_memcg_pressure.sh
[2] https://github.com/awslabs/damon-tests/tree/next/perf

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
