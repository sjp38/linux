.. _data_access_monitor:

==========================
DAMON: Data Access MONitor
==========================


Too Long; Don't Read
====================

The data access monitor (DAMON) is a kernel module that allows users to monitor
the actual memory access pattern of specific user-space processes.  It aims to
be 1) sufficiently accurate to be useful for performance centric optimizations,
and 2) light-weight enough so that it can be applied online.

For the goals, DAMON utilizes its two core mechanisms, called region-based
sampling and adaptive regions adjustment.  The region-based sampling allows
users to make their own trade-off between the quality and the overhead of the
monitoring.  Therefore, users can set the upperbound of the monitoring
overhead.  Further, the adaptive regions adjustment mechanism makes DAMON to
maximize the quality and minimize the overhead with its best efforts while
preserving the users configured trade-off.


Background
==========

For performance-centric analysis and optimization of memory management schemes
(either that of kernel space or user space), the actual data access pattern of
the workloads is highly useful.  The information should be reasonably accurate,
but also must guaranteed to be taken with only light-weight overhead.  Manually
extracting such data from huge and complex programs is not easy and time
consuming, even for the developers of the programs.

There are a range of tools and techniques developed for general memory access
investigations, and some of those could be partially used for this purpose.
However, most of those are not practical or unscalable, mainly because those
are designed with no consideration about the trade-off between the accuracy of
the output and the overhead.

The memory access instrumentation techniques which is applied to many tools
such as Intel PIN is essential for highly detailed analysis such as bug
detections, but incur unacceptably high overhead.  This is mainly due to the
fact that those techniques are designed for the highly detailed information,
which is not strictly required for performance-centric purpose.  H/W based
access counting features (e.g., the Accessed bits of PTEs) based access
tracking mechanisms can dramatically decrease the overhead by forgiving some of
the monitoring quality, compared to the instrumentation based techniques.  That
said, the overhead can arbitrarily increase as the size of the target workload
grows.  Miniature-like static region based sampling can set the upperbound of
the overhead, but it will now decrease the quality of the output as the size of
the workload grows.


Mechanisms of DAMON
===================


Basic Access Check
------------------

DAMON basically reports what pages are how frequently accessed.  The report is
passed to users in binary format via a ``result file`` which users can set it's
path.  Note that the frequency is not an absolute number of accesses, but a
relative frequency.

Users can also control the resolution of the reports by setting two time
intervals, ``sampling interval`` and ``aggregation interval``.  In detail,
DAMON checks access to each page per ``sampling interval``, aggregates the
results (counts the number of the accesses to each page), and
reports/initializes the aggregated results per ``aggregation interval``.  For
the access check of each page, DAMON uses the Accessed bits of PTEs.

This is thus similar to the previously mentioned H/W based access tracking
mechanisms, which overhead is increasing as the size of the target process
grows.


Region Based Sampling
---------------------

To avoid the unbounded increase of the overhead, DAMON groups a number of
adjacent pages that assumed to have same access frequencies into a region.  As
long as the assumption (pages in a region have same access frequencies) is
kept, only one page in the region is necessary to be checked.  Thus, for each
``sampling interval``, DAMON randomly picks one page in each region and clears
its Accessed bit.  After one more ``sampling interval``, DAMON reads the
Accessed bit of the page and increases the access frequency of the region if
the bit has set meanwhile.  Therefore, the monitoring overhead is controllable
by setting the number of regions.  DAMON allows users to set the minimal and
maximum number of regions for the trade-off.

Except the assumption, this is almost same with the miniature-like static
region based sampling.  Therefore, this scheme cannot preserve the quality of
the output if the assumption is not guaranteed.


Adaptive Regions Adjustment
---------------------------

At the beginning of the monitoring, DAMON creates initial regions by evenly
splitting the memory mapped address space of the process into the
user-specified minimal number of regions.  In this initial state, the
assumption is normally not kept and thus the quality could be low.  To keep the
assumption as much as possible, DAMON adaptively merges and splits each region.
For each ``aggregation interval``, it compares the access frequencies of
adjacent regions and merges if the difference is small.  Then, after it reports
and clears the aggregated access frequency of each region, it splits each
region into two regions if the total number of regions is smaller than the half
of the user-specified maximum number of regions.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the bounds users set for their trade-off.


Applying Dynamic Memory Mapping
-------------------------------

Only a small portions of the super-huge virtual address space of processes is
mapped to physical memory and used.  Thus, tracking the unmapped address
regions is just wasteful.  However, tracking every memory mapping change might
incur high overhead.  Thus, DAMON allow users to specify a time interval
(``regions update interval``) to check and apply the dynamic memory mapping
changes to the tracking regions.


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

DAMON has a shallow wrapper python script, ``/tools/damon/damn`` that provides
more convenient interface.  Note that it is only aimed to be used for minimal
reference of the DAMON's raw interfaces and for debugging of the DAMON itself.
Based on the debugfs interface, you can create another cool and more convenient
user space tools.


Quick Tutorial
--------------

To test DAMON on your system,

1. Ensure your kernel is built with CONFIG_DAMON turned on, and debugfs is
   mounted at ``/sys/kernel/debug/``.
2. ``<your kernel source tree>/tools/damon/damn -h``


Evaluation
==========

A prototype of DAMON has evaluated on an Intel Xeon E7-8837 machine using 20
benchmarks that picked from SPEC CPU 2006, NAS, Tensorflow Benchmark,
SPLASH-2X, and PARSEC 3 benchmark suite.  Nonethless, this section provides
only summary of the results.  For more detail, please refer to the slides used
for the introduction of DAMON at the Linux Plumbers Conference 2019
(https://linuxplumbersconf.org/event/4/contributions/548/attachments/311/590/damon_ksummit19.pdf)
or the MIDDLEWARE'19 industrial track paper
(https://dl.acm.org/doi/10.1145/3366626.3368125).

We first traced and visualized the data access pattern of each workload.  We
were able to confirm that the visualized results are reasonably accurate by
manually comparing those with the source code of the workloads.

To see the usefulness of the monitoring, we optimized 9 memory intensive
workloads among them for memory pressure situations using the DAMON outputs.
In detail, we identified frequently accessed memory regions in each workload
based on the DAMON results and protected them with ``mlock()`` system calls.
The optimized versions consistently show speedup (2.55x in best case, 1.65x in
average) under memory pressure situation.

We also measured the overhead of DAMON by counting the number of sampling
regions.  It was not only under the upperbound, but was much lower (0.6 percent
of the bound in best case, 13.288 percent of the bound in average) as we
intended with the adaptive regions adjustment.  We also compared the number
with that of the straightforward Accessed bit based monitoring, which checks
the access of every page frame.  The overhead was smaller than the normal
mechanism by 94.242.42x in best case, and 3,159.61x in average.
