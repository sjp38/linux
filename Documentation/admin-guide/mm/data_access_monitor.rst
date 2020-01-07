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
access counting features (e.g., page table access bit) based access tracking
mechanisms can dramatically decrease the overhead by forgiving some of the
monitoring quality, compared to the instrumentation based techniques.  That
said, the overhead can arbitrarily increase as the size of the target workload
grows.  Miniature-like static region based sampling can set the upperbound of
the overhead, but it will now decrease the quality of the output as the size of
the workload grows.


Mechanisms of DAMON
===================


Basic Access Check
------------------

For each of user-specified time interval, DAMON reports what page is how
frequently accessed.  The report is passed to users by writing binary format
information to a ``result file`` that user previously specified it's path.
Note that the frequency is not an absolute number of accesses, but a relative
frequency.

Users can also control the resolution of the frequencies.  In detail, users can
set two time intervals, ``sampling interval`` and ``aggregation interval``.
DAMON checks access to each page per ``sampling interval``, aggregates the
results, and reports/initializes the aggregated results per ``aggregation
interval``.  For the access check of each page, DAMON uses the page table
access bit.

This is almost same with the previously mentioned H/W based access tracking
mechanisms, which overhead is increasing as the size of target process grows.


Region Based Sampling
---------------------

To avoid the unbounded increase of the overhead, DAMON groups the pages of the
target process into a number of regions that supposed to constructed with pages
that have similar access frequencies.  As long as the assumption is kept, only
one page in the region is required to be checked.  Thus, for each ``sampling
interval``, DAMON randomly pick one page in each region and clear its access
bit.  After one more ``sampling interval``, DAMON checks the access bits of the
pages chosen in the last interval and increase the access frequency of the
region if the bit is set meanwhile.  Thus, the monitoring overhead is
controlled by the number of regions.  DAMON allows users to set the minimal and
maximum number of regions for the trade-off.

Except the assumption, this is almost same with the miniature-like static
region based sampling.  Therefore, this scheme cannot preserve the quality of
the output if the assumption of the regions is not guaranteed.


Dynamic Regions Adjustment
--------------------------

At the beginning of the monitoring, DAMON creates initial regions by evenly
splitting the memory mapped address space of the process into the
user-specified minimal number of regions.  In this initial state, the
assumption is normally not kept and thus the quality could be low as the size
of the target increases.  To keep the assumption as much as be preserved, DAMON
dynamically merges and splits each region.  For each ``aggregation interval``,
it compares the access frequencies of adjacent regions and merges if the
difference is small.
Then, after it reports and clears the aggregated frequencies of each region, it
splits each region into two regions if total number of regions is smaller than
the user-specified maximum number of regions.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the users required trade-off bounds.


Applying Dynamic Memory Mapping
-------------------------------

Only a small portions of the super-huge virtual address space of processes is
mapped to physical memory and used.  Thus, tracking the unmapped address
regions is just wasteful.  However, tracking every memory mapping change can
incur high overhead only.  Thus, DAMON allow users to specify a time interval
(``regions update interval``) to check and apply the dynamic memory mapping
changes to the tracking regions.


User Interface
==============

DAMON exports three files, ``attrs``, ``pids``, and ``monitor_on`` under its
debugfs directory, ``/sys/kernel/debug/damon/``.


Attributes
----------

Users can read and write the ``sampling interval``, ``aggregation interval``,
``regions update interval``, min/max number of regions, and the ``result file``
path by reading from and writing to the file.  For example, below command sets
those values to 5 ms, 100 ms, 1,000 ms, 10, 1000, and ``/damon.data`` and
checks it again::

    # cd /sys/kernel/debug/damon
    # echo 5000 100000 1000000 10 1000 /damon.data > attrs
    # cat attrs
    5000 100000 1000000 10 1000 /damon.data


Target PIDs
-----------

Users can read and write the pids of current monitoring target processes by
reading from and writing to the `pids` file.  For example, below command sets
processes having pids 42 and 4242 as the processes to monitor and checks it
again::

    # cd /sys/kernel/debug/damon
    # echo 42 4242 > pids
    # cat pids
    42 4242

Note that setting the pids doesn't start the monitoring.


Turning On/Off
--------------

You can check current status, start and stop the DAMON by reading from and
writing to the ``monitor_on`` file.  Writing ``on`` to the file starts DAMON to
monitor the target processes with the attributes.  Writing ``off`` to the file
stops DAMON.  DAMON will also stop if every target processes is be terminated.
Below command turns on, off, and checks status of DAMON::

    # cd /sys/kernel/debug/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

Please note that 'attrs' and 'pids' file is only readable while DAMON is
running on.  If you try to write to those files while DAMON is running, it will
return ``-EINVAL``.


User Space Wrapper
------------------

DAMON has a shallow wrapper python script providing more convenient interface
as ``/tools/damon/damn``.  Note that it only aims to be used for minimal
reference of the debugfs interface and for debugging purposes.  Based on the
debugfs interface, you can of course create another cool user space tools.


Quick Tutorial
--------------

To test DAMON on your system,

1. Ensure your kernel is built with CONFIG_DAMON turned on.
2. ``<your kernel source tree>/tools/damon/damn -h``
