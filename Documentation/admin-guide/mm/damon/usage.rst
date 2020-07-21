.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below three interfaces for different users.

- *DAMON user space tool.*
  This is for privileged people such as system administrators who want a
  just-working human-friendly interface.  Using this, users can use the DAMON’s
  major features in a human-friendly way.  It may not be highly tuned for
  special cases, though.  It supports both virtual and physical address spaces
  monitoring.
- *debugfs interface.*
  This is for privileged user space programmers who want more optimized use of
  DAMON.  Using this, users can use DAMON’s major features by reading
  from and writing to special debugfs files.  Therefore, you can write and use
  your personalized DAMON debugfs wrapper programs that reads/writes the
  debugfs files instead of you.  The DAMON user space tool is also a reference
  implementation of such programs.  It supports both virtual and physical
  address spaces monitoring.
- *Kernel Space Programming Interface.*
  This is for kernel space programmers.  Using this, users can utilize every
  feature of DAMON most flexibly and efficiently by writing kernel space
  DAMON application programs for you.  You can even extend DAMON for various
  address spaces.

This document does not describe the kernel space programming interface in
detail.  For that, please refer to the :doc:`/vm/damon/api`.


DAMON User Space Tool
=====================

A reference implementation of the DAMON user space tools which provides a
convenient user interface is in the kernel source tree.  It is located at
``tools/damon/damo`` of the tree.

The tool provides a subcommands based interface.  Every subcommand provides
``-h`` option, which provides the minimal usage of it.  Currently, the tool
supports two subcommands, ``record`` and ``report``.

Below example commands assume you set ``$PATH`` to point ``tools/damon/`` for
brevity.  It is not mandatory for use of ``damo``, though.


Recording Data Access Pattern
-----------------------------

The ``record`` subcommand records the data access pattern of target workloads
in a file (``./damon.data`` by default).  You can specify the target with 1)
the command for execution of the monitoring target process, 2) pid of running
target process, or 3) the special keyword, 'paddr', if you want to monitor the
system's physical memory address space.  Below example shows a command target
usage::

    # cd <kernel>/tools/damon/
    # damo record "sleep 5"

The tool will execute ``sleep 5`` by itself and record the data access patterns
of the process.  Below example shows a pid target usage::

    # sleep 5 &
    # damo record `pidof sleep`

Finally, below example shows the use of the special keyword, 'paddr'::

    # damo record paddr

In this case, the monitoring target regions defaults to the largetst 'System
RAM' region specified in '/proc/iomem' file.  Note that the initial monitoring
target region is maintained rather than dynamically updated like the virtual
memory address spaces monitoring case.

The location of the recorded file can be explicitly set using ``-o`` option.
You can further tune this by setting the monitoring attributes.  To know about
the monitoring attributes in detail, please refer to the
:doc:`/vm/damon/design`.


Analyzing Data Access Pattern
-----------------------------

The ``report`` subcommand reads a data access pattern record file (if not
explicitly specified using ``-i`` option, reads ``./damon.data`` file by
default) and generates human-readable reports.  You can specify what type of
report you want using a sub-subcommand to ``report`` subcommand.  ``raw``,
``heats``, and ``wss`` report types are supported for now.


raw
~~~

``raw`` sub-subcommand simply transforms the binary record into a
human-readable text.  For example::

    $ damo report raw
    start_time:  193485829398
    rel time:                0
    nr_tasks:  1
    target_id:  1348
    nr_regions:  4
    560189609000-56018abce000(  22827008):  0
    7fbdff59a000-7fbdffaf1a00(   5601792):  0
    7fbdffaf1a00-7fbdffbb5000(    800256):  1
    7ffea0dc0000-7ffea0dfd000(    249856):  0

    rel time:        100000731
    nr_tasks:  1
    target_id:  1348
    nr_regions:  6
    560189609000-56018abce000(  22827008):  0
    7fbdff59a000-7fbdff8ce933(   3361075):  0
    7fbdff8ce933-7fbdffaf1a00(   2240717):  1
    7fbdffaf1a00-7fbdffb66d99(    480153):  0
    7fbdffb66d99-7fbdffbb5000(    320103):  1
    7ffea0dc0000-7ffea0dfd000(    249856):  0

The first line shows the recording started timestamp (nanosecond).  Records of
data access patterns follows.  Each record is separated by a blank line.  Each
record first specifies the recorded time (``rel time``) in relative to the
start time, the number of monitored tasks in this record (``nr_tasks``).
Recorded data access patterns of each task follow.  Each data access pattern
for each task shows the target's pid (``target_id``) and a number of monitored
address regions in this access pattern (``nr_regions``) first.  After that,
each line shows the start/end address, size, and the number of observed
accesses of each region.


heats
~~~~~

The ``raw`` output is very detailed but hard to manually read.  ``heats``
sub-subcommand plots the data in 3-dimensional form, which represents the time
in x-axis, address of regions in y-axis, and the access frequency in z-axis.
Users can set the resolution of the map (``--tres`` and ``--ares``) and
start/end point of each axis (``--tmin``, ``--tmax``, ``--amin``, and
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
points.  The three numbers in each line represent time in nanosecond, address,
and the observed access frequency.

Users will be able to convert this text output into a heatmap image (represents
z-axis values with colors) or other 3D representations using various tools such
as 'gnuplot'.  For more convenience, ``heats`` sub-subcommand provides the
'gnuplot' based heatmap image creation.  For this, you can use ``--heatmap``
option.  Also, note that because it uses 'gnuplot' internally, it will fail if
'gnuplot' is not installed on your system.  For example::

    $ ./damo report heats --heatmap heatmap.png

Creates the heatmap image in ``heatmap.png`` file.  It supports ``pdf``,
``png``, ``jpeg``, and ``svg``.

If the target address space is virtual memory address space and you plot the
entire address space, the huge unmapped regions will make the picture looks
only black.  Therefore you should do proper zoom in / zoom out using the
resolution and axis boundary-setting arguments.  To make this effort minimal,
you can use ``--guide`` option as below::

    $ ./damo report heats --guide
    target_id:1348
    time: 193485829398-198337863555 (4852034157)
    region   0: 00000094564599762944-00000094564622589952 (22827008)
    region   1: 00000140454009610240-00000140454016012288 (6402048)
    region   2: 00000140731597193216-00000140731597443072 (249856)

The output shows unions of monitored regions (start and end addresses in byte)
and the union of monitored time duration (start and end time in nanoseconds) of
each target task.  Therefore, it would be wise to plot the data points in each
union.  If no axis boundary option is given, it will automatically find the
biggest union in ``--guide`` output and set the boundary in it.


wss
~~~

The ``wss`` type extracts the distribution and chronological working set size
changes from the records.  For example::

    $ ./damo report wss
    # <percentile> <wss>
    # target_id   1348
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
    # target_id   1348
    # avr:  66228
    0       0
    25      0
    50      0
    75      0
    100     0

The average is still 66,228.  And, because the access was spiked in very short
duration and this command plots only 4 data points, we cannot show when the
access spikes made.  Users can specify the resolution of the distribution
(``--range``).  By giving more fine resolution, the short duration spikes could
be found.

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
    # <min/max size> <min/max frequency (0-100)> <min/max age> <action>
    #
    # B/K/M/G/T for Bytes/KiB/MiB/GiB/TiB
    # us/ms/s/m/h/d for micro-seconds/milli-seconds/seconds/minutes/hours/days
    # 'min/max' for possible min/max value.

    # if a region keeps a high access frequency for >=100ms, put the region on
    # the head of the LRU list (call madvise() with MADV_WILLNEED).
    min    max      80      max     100ms   max willneed

    # if a region keeps a low access frequency for >=200ms and <=one hour, put
    # the region on the tail of the LRU list (call madvise() with MADV_COLD).
    min     max     10      20      200ms   1h  cold

    # if a region keeps a very low access frequency for >=60 seconds, swap out
    # the region immediately (call madvise() with MADV_PAGEOUT).
    min     max     0       10      60s     max pageout

    # if a region of a size >=2MiB keeps a very high access frequency for
    # >=100ms, let the region to use huge pages (call madvise() with
    # MADV_HUGEPAGE).
    2M      max     90      100     100ms   max hugepage

    # If a regions of a size >=2MiB keeps small access frequency for >=100ms,
    # avoid the region using huge pages (call madvise() with MADV_NOHUGEPAGE).
    2M      max     0       25      100ms   max nohugepage

For example, you can make a running process named 'foo' to use huge pages for
memory regions keeping 2MB or larger size and having very high access frequency
for at least 100 milliseconds using below commands::

    $ echo "2M max    90 max    100ms max    hugepage" > my_thp_scheme
    $ ./damo schemes --schemes my_thp_scheme `pidof foo`


debugfs Interface
=================

DAMON exports six files, ``attrs``, ``target_ids``, ``init_regions``,
``record``, ``schemes`` and ``monitor_on`` under its debugfs directory,
``<debugfs>/damon/``.


Attributes
----------

Users can get and set the ``sampling interval``, ``aggregation interval``,
``regions update interval``, and min/max number of monitoring target regions by
reading from and writing to the ``attrs`` file.  To know about the monitoring
attributes in detail, please refer to the :doc:`/vm/damon/design`.  For
example, below commands set those values to 5 ms, 100 ms, 1,000 ms, 10 and
1000, and then check it again::

    # cd <debugfs>/damon
    # echo 5000 100000 1000000 10 1000 > attrs
    # cat attrs
    5000 100000 1000000 10 1000


Target IDs
----------

Some types of address spaces supports multiple monitoring target.  For example,
the virtual memory address spaces monitoring can have multiple processes as the
monitoring targets.  Users can set the targets by writing relevant id values of
the targets to, and get the ids of the current targets by reading from the
``target_ids`` file.  In case of the virtual address spaces monitoring, the
values should be pids of the monitoring target processes.  For example, below
commands set processes having pids 42 and 4242 as the monitoring targets and
check it again::

    # cd <debugfs>/damon
    # echo 42 4242 > target_ids
    # cat target_ids
    42 4242

Users can also monitor the physical memory address space of the system by
writing a special keyword, "``paddr\n``" to the file.  Because physical address
space monitoring doesn't support multiple targets, reading the file will show a
fake value, ``42``, as below::

    # cd <debugfs>/damon
    # echo paddr > target_ids
    # cat target_ids
    42

Note that setting the target ids doesn't start the monitoring.


Initial Monitoring Target Regions
---------------------------------

In case of the virtual address space monitoring, DAMON automatically sets and
updates the monitoring target regions so that entire memory mappings of target
processes can be covered.  However, users might want to limit the monitoring
region to specific address ranges, such as the heap, the stack, or specific
file-mapped area.  Or, some users might know the initial access pattern of
their workloads and therefore want to set optimal initial regions for the
'adaptive regions adjustment'.

In contrast, DAMON do not automatically sets and updates the monitoring target
regions in case of physical memory monitoring.  Therefore, users should set the
monitoring target regions by themselves.

In such cases, users can explicitly set the initial monitoring target regions
as they want, by writing proper values to the ``init_regions`` file.  Each line
of the input should represent one region in below form.::

    <target id> <start address> <end address>

The ``target id`` should already in ``target_ids`` file, and the regions should
be passed in address order.  For example, below commands will set a couple of
address ranges, ``1-100`` and ``100-200`` as the initial monitoring target
region of process 42, and another couple of address ranges, ``20-40`` and
``50-100`` as that of process 4242.::

    # cd <debugfs>/damon
    # echo "42   1       100
            42   100     200
            4242 20      40
            4242 50      100" > init_regions

Note that this sets the initial monitoring target regions only.  In case of
virtual memory monitoring, DAMON will automatically updates the boundary of the
regions after one ``regions update interval``.  Therefore, users should set the
``regions update interval`` large enough in this case, if they don't want the
update.


Record
------

This debugfs file allows you to record monitored access patterns in a regular
binary file.  The recorded results are first written in an in-memory buffer and
flushed to a file in batch.  Users can get and set the size of the buffer and
the path to the result file by reading from and writing to the ``record`` file.
For example, below commands set the buffer to be 4 KiB and the result to be
saved in ``/damon.data``. ::

    # cd <debugfs>/damon
    # echo "4096 /damon.data" > record
    # cat record
    4096 /damon.data

The recording can be disabled by setting the buffer size zero.


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
numbers and their meanings are as below.

 - 0: Call ``madvise()`` for the region with ``MADV_WILLNEED``
 - 1: Call ``madvise()`` for the region with ``MADV_COLD``
 - 2: Call ``madvise()`` for the region with ``MADV_PAGEOUT``
 - 3: Call ``madvise()`` for the region with ``MADV_HUGEPAGE``
 - 4: Call ``madvise()`` for the region with ``MADV_NOHUGEPAGE``
 - 5: Do nothing but count the statistics

You can disable schemes by simply writing an empty string to the file.  For
example, below commands applies a scheme saying "If a memory region of size in
[4KiB, 8KiB] is showing accesses per aggregate interval in [0, 5] for aggregate
interval in [10, 20], page out the region", check the entered scheme again, and
finally remove the scheme. ::

    # cd <debugfs>/damon
    # echo "4096 8192    0 5    10 20    2" > schemes
    # cat schemes
    4096 8192 0 5 10 20 2 0 0
    # echo > schemes

The last two integers in the 4th line of above example is the total number and
the total size of the regions that the scheme is applied.

Turning On/Off
--------------

Setting the files as described above doesn't incur effect unless you explicitly
start the monitoring.  You can start, stop, and check the current status of the
monitoring by writing to and reading from the ``monitor_on`` file.  Writing
``on`` to the file starts the monitoring of the targets with the attributes.
Writing ``off`` to the file stops those.  DAMON also stops if every target
process is terminated.  Below example commands turn on, off, and check the
status of DAMON::

    # cd <debugfs>/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

Please note that you cannot write to the above-mentioned debugfs files while
the monitoring is turned on.  If you write to the files while DAMON is running,
an error code such as ``-EBUSY`` will be returned.
