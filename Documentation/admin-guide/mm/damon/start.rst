.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

This document briefly describes how you can use DAMON by demonstrating its
default user space tool.  Please note that this document describes only a part
of its features for brevity.  Please refer to :doc:`usage` for more details.


TL; DR
======

Follow below 5 commands to monitor and visualize the access pattern of your
workload. ::

    $ git clone https://github.com/sjp38/linux -b damon/master
    /* build the kernel with CONFIG_DAMON=y, install, reboot */
    $ mount -t debugfs none /sys/kernel/debug/
    $ cd linux/tools/damon
    $ ./damo record $(pidof <your workload>)
    $ ./damo report heats --heatmap access_pattern.png


Prerequisites
=============

Kernel
------

You should first ensure your system is running on a kernel built with
``CONFIG_DAMON=y``.


User Space Tool
---------------

For the demonstration, we will use the default user space tool for DAMON,
called DAMON Operator (DAMO).  It is located at ``tools/damon/damo`` of the
DAMON development kernel source tree (``damon/master`` branch of
https://github.com/sjp38/linux).  For brevity, below examples assume you set
``$PATH`` to point it.  It's not mandatory, though.

Because DAMO is using the debugfs interface (refer to :doc:`usage` for the
detail) of DAMON, you should ensure debugfs is mounted.  Mount it manually as
below::

    # mount -t debugfs none /sys/kernel/debug/

or append below line to your ``/etc/fstab`` file so that your system can
automatically mount debugfs from next booting::

    debugfs /sys/kernel/debug debugfs defaults 0 0


Recording Data Access Patterns
==============================

Below commands record memory access pattern of a program and save the
monitoring results in a file. ::

    $ git clone https://github.com/sjp38/masim
    $ cd masim; make; ./masim ./configs/zigzag.cfg &
    $ sudo damo record -o damon.data $(pidof masim)

The first two lines of the commands get an artificial memory access generator
program and runs it in the background.  It will repeatedly access two 100 MiB
sized memory regions one by one.  You can substitute this with your real
workload.  The last line asks ``damo`` to record the access pattern in
``damon.data`` file.


Visualizing Recorded Patterns
=============================

Below three commands visualize the recorded access patterns into three
image files. ::

    $ damo report heats --heatmap access_pattern_heatmap.png
    $ damo report wss --range 0 101 1 --plot wss_dist.png
    $ damo report wss --range 0 101 1 --sortby time --plot wss_chron_change.png

- ``access_pattern_heatmap.png`` will show the data access pattern in a
  heatmap, which shows when (x-axis) what memory region (y-axis) is how
  frequently accessed (color).
- ``wss_dist.png`` will show the distribution of the working set size.
- ``wss_chron_change.png`` will show how the working set size has
  chronologically changed.

You can show the images in a web page [1]_ .  Those made with other realistic
workloads are also available [2]_ [3]_ [4]_.

.. [1] https://damonitor.github.io/doc/html/v17/admin-guide/mm/damon/start.html#visualizing-recorded-patterns
.. [2] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.1.png.html
.. [3] https://damonitor.github.io/test/result/visual/latest/rec.wss_sz.png.html
.. [4] https://damonitor.github.io/test/result/visual/latest/rec.wss_time.png.html
