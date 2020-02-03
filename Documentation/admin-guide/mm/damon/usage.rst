.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below three interfaces for different users.

- *DAMON user space tool.*
  This is for privileged people such as system administrators who want a
  just-working human-friendly interface.  Using this, users can use the DAMON’s
  major features in a human-friendly way.  It may not be highly tuned for
  special cases, though.  It supports only virtual address spaces monitoring.
- *debugfs interface.*
  This is for privileged user space programmers who want more optimized use of
  DAMON.  Using this, users can use DAMON’s major features by reading
  from and writing to special debugfs files.  Therefore, you can write and use
  your personalized DAMON debugfs wrapper programs that reads/writes the
  debugfs files instead of you.  The DAMON user space tool is also a reference
  implementation of such programs.  It supports only virtual address spaces
  monitoring.
- *Kernel Space Programming Interface.*
  This is for kernel space programmers.  Using this, users can utilize every
  feature of DAMON most flexibly and efficiently by writing kernel space
  DAMON application programs for you.  You can even extend DAMON for various
  address spaces.

This document does not describe the kernel space programming interface in
detail.  For that, please refer to the :doc:`/vm/damon/api`.


DAMON User Space Tool
=====================

There is a reference implementation of the DAMON user space tools, namely
``damo``, which provides a convenient user interface.  You can get it from
``tools/damon/`` directory in the DAMON development kernel source tree
(``damon/next`` branch of https://github.com/sjp38/linux.git).

The tool provides a subcommands based interface.  Every subcommand provides
``-h`` option, which provides the minimal usage of it.  Currently, the tool
supports two subcommands, ``record`` and ``report``.

Below example commands assume you set ``$PATH`` to point ``tools/damon/`` of
the development tree for brevity.  It is not mandatory for use of ``damo``,
though.


Recording Data Access Pattern
-----------------------------

The ``record`` subcommand records the data access pattern of target workloads
in a file (``./damon.data`` by default).  You can specify the target with 1)
the command for execution of the monitoring target process, or 2) pid of
running target process.  Below example shows a command target usage::

    # cd <kernel>/tools/damon/
    # damo record "sleep 5"

The tool will execute ``sleep 5`` by itself and record the data access patterns
of the process.  Below example shows a pid target usage::

    # sleep 5 &
    # damo record `pidof sleep`

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


debugfs Interface
=================

DAMON exports four files, ``attrs``, ``target_ids``, ``record``, and
``monitor_on`` under its debugfs directory, ``<debugfs>/damon/``.


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

Note that setting the target ids doesn't start the monitoring.


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
