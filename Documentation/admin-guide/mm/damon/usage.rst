.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below three interfaces for various use cases.

- *DAMON user space tool.*
  This is for privileged people such as system administrators who want a
  just-working human-friendly interface.  This interface is a reference
  implementation of the DAMON debugfs wrapper user space tool.  Using this
  tool, you can easily use the DAMON’s major features in a human-friendly way,
  though it may not be highly tuned for your specific use-cases.
- *debugfs interface.*
  This is for user space programmers who want optimized use of DAMON. Using
  this interface, you can use DAMON’s major features by reading from and
  writing to specific debugfs files.  Of course, you can write and use your
  personalized DAMON debugfs wrapper user space tool that reads/writes the
  debugfs files instead of you and provides a more human-friendly interface.
- *Kernel Space Programming Interface.*
  This is for kernel space programmers.  Using this, you can utilize every
  feature of DAMON most flexibly and efficiently by writing kernel space
  DAMON application programs for you.

We recommend you to start with the DAMON user space tool and move to debugfs
interface only if the real requirement is found, and prohibit the use of the
kernel space programming interface unless you need it, for the following
reasons.  First of all, there will be no big difference between the overheads
of these three interfaces, unless the use case is so special.  Also, all these
three interfaces support all major features of DAMON.

This document, therefore, does not describe the kernel space programming
interface in detail.  For the programming interface, please refer to :doc:`api`
or ``include/linux/damon.h`` file in the kernel source tree.



User Space Tool for DAMON
=========================

A reference implementation of the DAMON user space tool which provides a
convenient user interface is located at ``tools/damon/damo`` in the kernel
source tree.  Please note that this is initially aimed to be used for minimal
reference of the DAMON's debugfs interfaces and for tests of the DAMON itself.
Based on the debugfs interface, you can create another cool and more convenient
user space tools.

The interface of the tool is basically subcommand based.  You can almost always
use ``-h`` option to get the help of the use of each subcommand.  Currently, it
supports two subcommands, ``record`` and ``report``.

Below example commands assume you set ``$PATH`` to points ``tools/damon/`` for
brevity.  It is not mandatory for use of ``damo``, though.


Recording Data Access Pattern
-----------------------------

The ``record`` subcommand records the data access pattern of target processes
in a file (``./damon.data`` by default).  You can specify the target as either
pid of running target or a command for execution of the process.  Below example
shows a command target usage::

    # cd <kernel>/tools/damon/
    # damo record "sleep 5"

The tool will execute ``sleep 5`` by itself and record the data access patterns
of the process.  Below example shows a pid target usage::

    # sleep 5 &
    # damo record `pidof sleep`

You can tune this by setting the monitoring attributes and path to the record
file using optional arguments to the subcommand.  To know about the monitoring
attributes in detail, please refer to :doc:`mechanisms`.


Analyzing Data Access Pattern
-----------------------------

The ``report`` subcommand reads a data access pattern record file (if not
explicitly specified, reads ``./damon.data`` file by default) and generates
human-readable reports of various types.  You can specify what type of report
you want using a sub-subcommand to ``report`` subcommand.  ``raw``, ``heats``,
and ``wss`` report types are supported for now.


raw
~~~

``raw`` sub-subcommand simply transforms the binary record into human-readable
text.  For example::

    $ damo report raw
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

The first line shows the recording started timestamp (nanosecond).  Records of
data access patterns are following this.  Each record is separated by a blank
line.  Each record first specifies the recorded time (``rel time``), the number
of monitored tasks in this record (``nr_tasks``).  A numbers of records of data
access patterns for each task follow.  Each data access pattern for each task
shows it's pid (``pid``) and a number of monitored virtual address regions in
this access pattern (``nr_regions``) first.  After that, each line shows the
start/end address, size, and the number of monitored accesses to the region for
each of the regions.


heats
~~~~~

The ``raw`` output is very detailed but hard to manually read and analyze.
``heats`` sub-subcommand plots the data in 3-dimensional form, which represents
the time in x-axis, virtual address in y-axis, and the access frequency in
z-axis.  Users can set the resolution of the map (``--tres`` and ``--ares``)
and start/end point of each axis (``--tmin``, ``--tmax``, ``--amin``, and
``--amax``) via optional arguments.  For example::

    $ damo report heats --tres 3 --ares 3
    0               0               0.0
    0               7609002         0.0
    0               15218004        0.0
    66112620851     0               0.0
    66112620851     7609002         0.0
    66112620851     15218004        0.0
    132225241702    0               0.0
    132225241702    7609002         0.0
    132225241702    15218004        0.0

This command shows a recorded access pattern in heatmap of 3x3 resolution.
Therefore it shows 9 data points in total.  Each line shows each of the data
points.  The three numbers in each line represent time in nanosecond, virtual
address in bytes, and the observed access frequency.

Users can easily convert this text output into a heatmap image (represent z-axis
values with colors) or other 3D representations using various tools such as
'gnuplot'.  ``heats`` sub-subcommand also provides 'gnuplot' based heatmap
image creation.  For this, you can use ``--heatmap`` option.  Also, note that
because it uses 'gnuplot' internally, it will fail if 'gnuplot' is not
installed on your system.  For example::

    $ ./damo report heats --heatmap heatmap.png

Creates ``heatmap.png`` file containing the heatmap image.  It supports
``pdf``, ``png``, ``jpeg``, and ``svg``.

If the target address space is virtual memory address space and you plot the
entire address space, the huge unmapped regions will make the picture looks
only black.  Therefore you should do proper zoom in / zoom out using the axis
boundary-setting optional arguments.  To make this effort minimal, you can use
``--guide`` option.  For example::

    $ ./damo report heats --guide
    pid:1348
    time: 193485829398-198337863555 (4852034157)
    region   0: 00000094564599762944-00000094564622589952 (22827008)
    region   1: 00000140454009610240-00000140454016012288 (6402048)
    region   2: 00000140731597193216-00000140731597443072 (249856)

The output shows unions of monitored regions (start and end addresses in byte)
and union of monitored time duration (start and end time in nanoseconds) of
each target task.  Therefore, it would be wise to plot the data points in each
union.  If no axis boundary option is given, it will automatically find the
biggest union in ``--guide`` output and plot for it.


wss
~~~

The ``wss`` type extracts the distribution and chronological working set size
changes from the records.  For example::

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
above.  It shows 0th, 25th, 50th, 75th, and 100th percentile and the average of
the measured working set sizes in the access pattern records.  In this case,
the working set size was zero for 75th percentile but 1,920,615 bytes in max
and 66,228 bytes on average.

By setting the sort key of the percentile using '--sortby', you can show how
the working set size has chronologically changed.  For example::

    $ ./damo report wss --sortby time
    # <percentile> <wss>
    # pid   1348
    # avr:  66228
    0       0
    25      0
    50      0
    75      0
    100     0

The average is still 66,228.  And, because the access was spiked in very short
duration but we use only 4 data points, we cannot show when the access spikes
made.  Users can specify the resolution of the distribution (``--range``).  By
giving more fine resolution, users will be able to see the short duration
spikes.

Similar to that of ``heats --heatmap``, it also supports 'gnuplot' based simple
visualization of the distribution via ``--plot`` option.


DAMON-based Operation Schemes
-----------------------------

The ``schemes`` subcommand allows users to do DAMON-based memory management
optimizations in a few seconds.  Similar to ``record``, it receives monitoring
attributes and target.  However, in addition to those, ``schemes`` receives
data access pattern-based memory operation schemes, which describes what memory
operation action should be applied to memory regions showing specific data
access pattern.  Then, it starts the data access monitoring and automatically
applies the schemes to the targets.

The operation schemes should be saved in a text file in below format and passed
to ``schemes`` subcommand via ``--schemes`` option. ::

    min-size max-size min-acc max-acc min-age max-age action

The format also supports comments, several units for size and age of regions,
and human readable action names.  Currently supported operation actions are
``willneed``, ``cold``, ``pageout``, ``hugepage`` and ``nohugepage``.  Each of
the actions works same to the madvise() system call hints having the name.
Please also note that the range is inclusive (closed interval), and ``0`` for
max values means infinite. Below example schemes are possible. ::

    # format is:
    # <min/max size> <min/max frequency (0-99)> <min/max age> <action>
    #
    # B/K/M/G/T for Bytes/KiB/MiB/GiB/TiB
    # us/ms/s/m/h/d for micro-seconds/milli-seconds/seconds/minutes/hours/days
    # 'null' means zero for size and age.

    # if a region keeps a high access frequency for >=100ms, put the region on
    # the head of the LRU list (call madvise() with MADV_WILLNEED).
    null    null    80      null    100ms   0s      willneed

    # if a region keeps a low access frequency at least 200ms and at most one
    # hour, put the region on the tail of the LRU list (call madvise() with
    # MADV_COLD).
    0B      0B      10      20      200ms   1h cold

    # if a region keeps a very low access frequency for >=1 minute, swap
    # out the region immediately (call madvise() with MADV_PAGEOUT).
    0B      null    0       10      60s     0s pageout

    # if a region of a size >=2MiB keeps a very high access frequency for
    # >=100ms, let the region to use huge pages (call madvise() with
    # MADV_HUGEPAGE).
    2M      null    90      99      100ms   0s hugepage

    # If a region of a size >=2MiB keeps small access frequency for >=100ms,
    # avoid the region using huge pages (call madvise() with MADV_NOHUGEPAGE).
    2M      null    0       25      100ms   0s nohugepage

For example, you can make a running process named 'foo' to use huge pages for
memory regions keeping 2MB or larger size and having very high access frequency
for at least 100 milliseconds using below commands::

    $ echo "2M null 90 99 100ms 0s hugepage" > my_thp_scheme
    $ ./damo schemes --schemes my_thp_scheme `pidof foo`


debugfs Interface
=================

DAMON exports five files, ``attrs``, ``pids``, ``record``, ``schemes`` and
``monitor_on`` under its debugfs directory, ``<debugfs>/damon/``.


Attributes
----------

Users can get and set the ``sampling interval``, ``aggregation interval``,
``regions update interval``, and min/max number of monitoring target regions by
reading from and writing to the ``attrs`` file.  To know about the monitoring
attributes in detail, please refer to :doc:`mechanisms`.  For example, below
commands set those values to 5 ms, 100 ms, 1,000 ms, 10 and 1000, and then
check it again::

    # cd <debugfs>/damon
    # echo 5000 100000 1000000 10 1000 > attrs
    # cat attrs
    5000 100000 1000000 10 1000


Target PIDs
-----------

Users can get and set the pids of monitoring target processes by reading from
and writing to the ``pids`` file.  For example, below commands set processes
having pids 42 and 4242 as the processes to be monitored and check it again::

    # cd <debugfs>/damon
    # echo 42 4242 > pids
    # cat pids
    42 4242

Note that setting the pids doesn't start the monitoring.


Record
------

This debugfs file allows you to record monitored access patterns in a regular
binary file.  The recorded results are first written to an in-memory buffer and
flushed to a file in batch.  Users can get and set the size of the buffer and
the path to the result file by reading from and writing to the ``record`` file.
For example, below commands set the buffer to be 4 KiB and the result to be
saved in ``/damon.data``. ::

    # cd <debugfs>/damon
    # echo "4096 /damon.data" > record
    # cat record
    4096 /damon.data


Schemes
-------

For usual DAMON-based data access aware memory management optimizations, users
would simply want the system to apply a memory management action to a memory
region of a specific size having a specific access frequency for a specific
time.  DAMON receives such formalized operation schemes from the user and
applies those to the target processes.  It also counts the total number and
size of regions that each scheme is applied.  This statistics can be used for
online analysis or tuning of the schemes.

Users can get and set the schemes by reading from and writing to ``schemes``
debugfs file.  Reading the file also shows the statistics of each scheme.  To
the file, each of the schemes should be represented in each line in below form:

    min-size max-size min-acc max-acc min-age max-age action

Note that the ranges are closed interval.  Bytes for the size of regions
(``min-size`` and ``max-size``), number of monitored accesses per aggregate
interval for access frequency (``min-acc`` and ``max-acc``), number of
aggregate intervals for the age of regions (``min-age`` and ``max-age``), and a
predefined integer for memory management actions should be used.  The supported
numbers and their
meanings are as below.

 - 0: Call ``madvise()`` for the region with ``MADV_WILLNEED``
 - 1: Call ``madvise()`` for the region with ``MADV_COLD``
 - 2: Call ``madvise()`` for the region with ``MADV_PAGEOUT``
 - 3: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``
 - 4: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``
 - 5: Do nothing but count the statistics

You can disable schemes by simply writing an empty string to the file.  For
example, below commands applies a scheme saying "If a memory region >=4KiB is
showing <=5 accesses per aggregate interval (0 5) for >=5 aggregate interval (5
0), page out the region (2)", check the entered scheme again, and finally
remove the scheme. ::

    # cd <debugfs>/damon
    # echo "4096 0 0 5 5 0 2" > schemes
    # cat schemes
    4096 0 0 5 5 0 2 0 0
    # echo > schemes

The last two integers in the 4th line of above example is the total number and
the total size of the regions that the scheme is applied.

Turning On/Off
--------------

Setting the attributes and schemes as described above doesn't incur effect
unless you explicitly start the monitoring.  You can start, stop, and check
the current status of the monitoring by writing to and reading from the
``monitor_on`` file.  Writing ``on`` to the file make DAMON start monitoring
of the target processes with the attributes.  Recording and schemes applying
will also start if requested before.  Writing ``off`` to the file stops those.
DAMON also stops if every target process is terminated.  Below example
commands turn on, off, and check the status of DAMON::

    # cd <debugfs>/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

Please note that you cannot write to the above-mentioned debugfs files while
the monitoring is turned on.  If you write to the files while DAMON is running,
an error code such as ``-EBUSY`` will be returned.
