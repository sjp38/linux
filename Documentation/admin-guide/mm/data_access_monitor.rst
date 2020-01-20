.. SPDX-License-Identifier: GPL-2.0

==========================
DAMON: Data Access MONitor
==========================


Too Long; Don't Read
====================

DAMON is a kernel module that allows users to monitor the actual memory access
pattern of specific user-space processes.  It aims to be 1) accurate enough to
be useful for performance-centric domains, and 2) sufficiently light-weight so
that it can be applied online.

For the goals, DAMON utilizes its two core mechanisms, called region-based
sampling and adaptive regions adjustment.  The region-based sampling allows
users to make their own trade-off between the quality and the overhead of the
monitoring and set the upperbound of the monitoring overhead.  Further, the
adaptive regions adjustment mechanism makes DAMON to maximize the quality and
minimize the overhead with its best efforts while preserving the users
configured trade-off.

Please note that the term 'memory' in this document means 'main memory'.  It
also assumes that it would usually utilizes the middle level speed memory
devices such as DRAMs or NVRAMs.  CPU caches or storage devices are not our
concern, as those are too fast or too slow to be in DAMON's scope.


Background
==========

For performance-centric analysis and optimizations of memory management schemes
(either that of kernel space or user space), the actual data access pattern of
the workloads is highly useful.  The information need to be only reasonable
rather than strictly correct, because some level of incorrectness can be
handled in many performance-centric domains.  It also need to be taken within
reasonably short time with only light-weight overhead.

Manually extracting such data is not easy and time consuming if the target
workload is huge and complex, even for the developers of the programs.  There
are a range of tools and techniques developed for general memory access
investigations, and some of those could be partially used for this purpose.
However, most of those are not practical or unscalable, mainly because those
are designed with no consideration about the trade-off between the accuracy of
the output and the overhead.

The memory access instrumentation techniques which is applied to many tools
such as Intel PIN is essential for correctness required cases such as invalid
memory access bug detections.  However, those usually incur high overhead which
is unacceptable for many of the performance-centric domains.  Periodic access
checks based on H/W or S/W access counting features (e.g., the Accessed bits of
PTEs or the PG_Idle flags of pages) can dramatically decrease the overhead by
forgiving some of the quality, compared to the instrumentation based
techniques.  The reduced quality is still reasonable for many of the domains,
but the overhead can arbitrarily increase as the size of the target workload
grows.  Miniature-like static region based sampling can set the upperbound of
the overhead, but it will now decrease the quality of the output as the size of
the workload grows.


Expected Use-cases
==================

A straightforward usecase of DAMON would be the program behavior analysis.
With the DAMON output, users can confirm whether the program is running as
intended or not.  This will be useful for debuggings and tests of design
points.

The monitored results can also be useful for counting the dynamic working set
size of workloads.  For the administration of memory overcommitted systems or
selection of the environments (e.g., containers providing different amount of
memory) for your workloads, this will be useful.

If you are a programmer, you can optimize your program by managing the memory
based on the actual data access pattern.  For example, you can identify the
dynamic hotness of your data using DAMON and call ``mlock()`` to keep your hot
data in DRAM, or call ``madvise()`` with ``MADV_PAGEOUT`` to proactively
reclaim cold data.  Even though your program is guaranteed to not encounter
memory pressure, you can still improve the performance by applying the DAMON
outputs for call of ``MADV_HUGEPAGE`` and ``MADV_NOHUGEPAGE``.  More creative
optimizations would be possible.  Our evaluations of DAMON includes a
straightforward optimization using the ``mlock()``.  Please refer to the below
Evaluation section for more detail.

As DAMON incurs very low overhead, such optimizations can be applied not only
offline, but also online.  Also, there is no reason to limit such optimizations
to the user space.  Several parts of the kernel's memory management mechanisms
could be also optimized using DAMON. The reclamation, the THP (de)promotion
decisions, and the compaction would be such a candidates.


Mechanisms of DAMON
===================


Basic Access Check
------------------

DAMON basically reports what pages are how frequently accessed.  The report is
passed to users in binary format via a ``result file`` which users can set it's
path.  Note that the frequency is not an absolute number of accesses, but a
relative frequency among the pages of the target workloads.

Users can also control the resolution of the reports by setting two time
intervals, ``sampling interval`` and ``aggregation interval``.  In detail,
DAMON checks access to each page per ``sampling interval``, aggregates the
results (counts the number of the accesses to each page), and reports the
aggregated results per ``aggregation interval``.  For the access check of each
page, DAMON uses the Accessed bits of PTEs.

This is thus similar to the previously mentioned periodic access checks based
mechanisms, which overhead is increasing as the size of the target process
grows.


Region Based Sampling
---------------------

To avoid the unbounded increase of the overhead, DAMON groups a number of
adjacent pages that assumed to have same access frequencies into a region.  As
long as the assumption (pages in a region have same access frequencies) is
kept, only one page in the region is required to be checked.  Thus, for each
``sampling interval``, DAMON randomly picks one page in each region and clears
its Accessed bit.  After one more ``sampling interval``, DAMON reads the
Accessed bit of the page and increases the access frequency of the region if
the bit has set meanwhile.  Therefore, the monitoring overhead is controllable
by setting the number of regions.  DAMON allows users to set the minimal and
maximum number of regions for the trade-off.

Except the assumption, this is almost same with the above-mentioned
miniature-like static region based sampling.  In other words, this scheme
cannot preserve the quality of the output if the assumption is not guaranteed.


Adaptive Regions Adjustment
---------------------------

At the beginning of the monitoring, DAMON constructs the initial regions by
evenly splitting the memory mapped address space of the process into the
user-specified minimal number of regions.  In this initial state, the
assumption is normally not kept and thus the quality could be low.  To keep the
assumption as much as possible, DAMON adaptively merges and splits each region.
For each ``aggregation interval``, it compares the access frequencies of
adjacent regions and merges those if the frequency difference is small.  Then,
after it reports and clears the aggregated access frequency of each region, it
splits each region into two regions if the total number of regions is smaller
than the half of the user-specified maximum number of regions.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the bounds users set for their trade-off.


Applying Dynamic Memory Mappings
--------------------------------

Only a number of small parts in the super-huge virtual address space of the
processes is mapped to physical memory and accessed.  Thus, tracking the
unmapped address regions is just wasteful.  However, tracking every memory
mapping change might incur an overhead.  For the reason, DAMON applies the
dynamic memory mapping changes to the tracking regions only for each of an
user-specified time interval (``regions update interval``).


User Interface
==============

DAMON exports three files, ``attrs``, ``pids``, and ``monitor_on`` under its
debugfs directory, ``<debugfs>/damon/``.


Attributes
----------

Users can read and write the ``sampling interval``, ``aggregation interval``,
``regions update interval``, min/max number of regions, and the path to
``result file`` by reading from and writing to the ``attrs`` file.  For
example, below commands set those values to 5 ms, 100 ms, 1,000 ms, 10, 1000,
and ``/damon.data`` and check it again::

    # cd <debugfs>/damon
    # echo 5000 100000 1000000 10 1000 /damon.data > attrs
    # cat attrs
    5000 100000 1000000 10 1000 /damon.data


Target PIDs
-----------

Users can read and write the pids of current monitoring target processes by
reading from and writing to the `pids` file.  For example, below commands set
processes having pids 42 and 4242 as the processes to be monitored and check
it again::

    # cd <debugfs>/damon
    # echo 42 4242 > pids
    # cat pids
    42 4242

Note that setting the pids doesn't starts the monitoring.


Turning On/Off
--------------

You can check current status, start and stop the monitoring by reading from and
writing to the ``monitor_on`` file.  Writing ``on`` to the file starts DAMON to
monitor the target processes with the attributes.  Writing ``off`` to the file
stops DAMON.  DAMON also stops if every target processes is be terminated.
Below example commands turn on, off, and check status of DAMON::

    # cd <debugfs>/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

Please note that you cannot write to the ``attrs`` and ``pids`` files while the
monitoring is turned on.  If you write to the files while DAMON is running,
``-EINVAL`` will be returned.


User Space Wrapper
------------------

DAMON has a shallow wrapper python script, ``/tools/damon/damo`` that provides
more convenient interface.  Note that it is only aimed to be used for minimal
reference of the DAMON's raw interfaces and for debugging of the DAMON itself.
Based on the debugfs interface, you can create another cool and more convenient
user space tools.


Quick Tutorial
--------------

To test DAMON on your system,

1. Ensure your kernel is built with CONFIG_DAMON turned on, and debugfs is
   mounted at ``/sys/kernel/debug/``.
2. ``<your kernel source tree>/tools/damon/damo -h``
