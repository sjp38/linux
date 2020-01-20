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


User Space Tool for DAMON
=========================

There is a user space tool for DAMON, ``/tools/damon/damo``.  It provides
another user interface which more convenient than the debugfs interface.
Nevertheless, note that it is only aimed to be used for minimal reference of
the DAMON's debugfs interfaces and for tests of the DAMON itself.  Based on the
debugfs interface, you can create another cool and more convenient user space
tools.

The interface of the tool is basically subcommand based.  You can almost always
use ``-h`` option to get help of the use of each subcommand.  Currently, it
supports two subcommands, ``record`` and ``report``.


Recording Data Access Pattern
-----------------------------

The ``record`` subcommand records the data access pattern of target process in
a file (``./damon.data`` by default) using DAMON.  You can specifies the target
as either pid or a command for an execution of the process.  Below example
shows a command target usage::

    # cd <kernel>/tools/damon/
    # ./damo record "sleep 5"

The tool will execute ``sleep 5`` by itself and record the data access patterns
of the process.  Below example shows a pid target usage::

    # sleep 5 &
    # ./damo record `pidof sleep`

You can set more detailed attributes and path to the recorded data file using
optional arguments to the subcommand.  Use the ``-h`` option for more help.


Analyzing Data Access Pattern
-----------------------------

The ``report`` subcommand reads a data access pattern record file (if not
explicitly specified, reads ``./damon.data`` file if exists) and generates
reports of various types.  You can specify what type of report you want using
sub-subcommand to ``report`` subcommand.  For supported types, pass the ``-h``
option to ``report`` subcommand.


raw
~~~

``raw`` sub-subcommand simply transforms the record, which is storing the data
access patterns in binary format to human readable text.  For example::

    $ ./damo report raw
    start_time:  193485829398
    rel time:                0
    nr_tasks:  1
    pid:  1348
    nr_regions:  4
    560189609000-56018abce000(  22827008):  0
    7fbdff59a000-7fbdffaf1a00(   5601792):  0
    7fbdffaf1a00-7fbdffbb5000(    800256):  1
    7ffea0dc0000-7ffea0dfd000(    249856):  0

    rel time:        100000731
    nr_tasks:  1
    pid:  1348
    nr_regions:  6
    560189609000-56018abce000(  22827008):  0
    7fbdff59a000-7fbdff8ce933(   3361075):  0
    7fbdff8ce933-7fbdffaf1a00(   2240717):  1
    7fbdffaf1a00-7fbdffb66d99(    480153):  0
    7fbdffb66d99-7fbdffbb5000(    320103):  1
    7ffea0dc0000-7ffea0dfd000(    249856):  0

The first line shows recording started timestamp (nanosecond).  Records of data
access patterns are following this.  Each record is sperated by a blank line.
Each record first specifies the recorded time (``rel time``), number of
monitored tasks in this record (``nr_tasks``).  Multiple number of records of
data access pattern for each task continue.  Each data access pattern for each
task shows first it's pid (``pid``) and number of monitored virtual address
regions in this access pattern (``nr_regions``).  After that, each line shows
start/end address, size, and number of monitored accesses to the region for
each of the regions.


heats
~~~~~

The ``raw`` type shows detailed information but it is exhaustive to manually
read and analyzed.  For the reason, ``heats`` plots the data in heatmap form,
using time as x-axis, virtual address as y-axis, and access frequency as
z-axis.  Also, users set the resolution and start/end point of each axis via
optional arguments.  For example::

    $ ./damo report heats --tres 3 --ares 3
    0               0               0.0
    0               7609002         0.0
    0               15218004        0.0
    66112620851     0               0.0
    66112620851     7609002         0.0
    66112620851     15218004        0.0
    132225241702    0               0.0
    132225241702    7609002         0.0
    132225241702    15218004        0.0

This command shows the recorded access pattern of the ``sleep`` command using 3
data points for each of time axis and address axis.  Therefore, it shows 9 data
points in total.

Users can easily converts this text output into heatmap image or other 3D
representation using various tools such as 'gnuplot'.  ``raw`` sub-subcommand
also provides 'gnuplot' based heatmap image creation.  For this, you can use
``--heatmap`` option.  Also, note that because it uses 'gnuplot' internally, it
will fail if 'gnuplot' is not installed on your system.  For example::

    $ ./damo report heats --heatmap heatmap.png

Creates ``heatmap.png`` file containing the heatmap image.  It supports
``pdf``, ``png``, ``jpeg``, and ``svg``.

For proper zoom in / zoom out, you need to see the layout of the record.  For
that, use '--guide' option.  If the option is given, it will provide useful
information about the records in the record file.  For example::

    $ ./damo report heats --guide
    pid:1348
    time: 193485829398-198337863555 (4852034157)
    region   0: 00000094564599762944-00000094564622589952 (22827008)
    region   1: 00000140454009610240-00000140454016012288 (6402048)
    region   2: 00000140731597193216-00000140731597443072 (249856)

The output shows monitored regions (start and end addresses in byte) and
monitored time duration (start and end time in nanosecond) of each target task.
Therefore, it would be wise to plot only each region rather than plotting
entire address space in one heatmap because the gaps between the regions are so
huge in this case.


wss
~~~

The ``wss`` type shows the distribution or time-varying working set sizes of
the recorded workload using the records.  For example::

    $ ./damo report wss
    # <percentile> <wss>
    # pid   1348
    # avr:  66228
    0       0
    25      0
    50      0
    75      0
    100     1920615

Without any option, it shows the distribution of the working set sizes as
above.  Basically it shows 0th, 25th, 50th, 75th, and 100th percentile and
average of the measured working set sizes in the access pattern records.  In
this case, the working set size was zero for 75th percentile but 1,920,615
bytes in max and 66,228 in average.

By setting the sort key of the percentile using '--sortby', you can also see
how the working set size is chronologically changed.  For example::

    $ ./damo report wss --sortby time
    # <percentile> <wss>
    # pid   1348
    # avr:  66228
    0       0
    25      0
    50      0
    75      0
    100     0

The average is still 66,228.  And, because we sorted the working set using
recorded time and the access is very short, we cannot show when the access
made.

Users can specify the resolution of the distribution (``--range``).  It also
supports 'gnuplot' based simple visualization (``--plot``) of the distribution.
