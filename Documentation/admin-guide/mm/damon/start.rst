.. SPDX-License-Identifier: GPL-2.0

===============
Getting Started
===============

This document briefly describes how you can use DAMON by demonstrating the user
space tool.  Please note that this document describes only a part of the
features for brevity.  Please refer to :doc:`usage` for more details.


TL; DR
======

Simply follow below 5 commands.  Don't forget replacing ``<your workload>``
with your *real* workload, though. ::

    $ git clone https://github.com/sjp38/linux -b damon/master
    /* build the kernel with CONFIG_DAMON=y, install, reboot */
    $ cd linux/tools/damon
    $ ./damo record $(pidof <your workload>)
    $ ./damo report heats --heatmap access_pattern.pdf
    $ ./damo report wss --range 0 101 1 --plot wss_dist.pdf


Prerequisites
=============

Kernel
------

You should first ensure your system is running on a kernel built with
``CONFIG_DAMON``.  If the value is set as ``m``, you should load the module
first::

    # modprobe damon


User Space Tool
---------------

For the demonstration, we will use a user space tool for the convenient use of
DAMON, called DAMON Operator (DAMO).  It is located at ``tools/damon/damo`` of
the kernel source tree.  We assume that you set ``$PATH`` to point it.  The
``$PATH`` setting is not mandatory but we make the assumption here for the
brevity of the below examples.

Because DAMO is using the debugfs interface (refer to :doc:`usage` for the
detail) of DAMON, you should also ensure debugfs is mounted.  Mount it manually
as below::

    # mount -t debugfs none /sys/kernel/debug/

or append below line to your ``/etc/fstab`` file so that your system can
automatically mount debugfs from next booting::

    debugfs /sys/kernel/debug debugfs defaults 0 0


Recording Data Access Patterns
==============================

Below commands record memory access pattern of a program, ``masim``, and save
it in a file, ``damon.data``.  The program will access two 100 MiB memory
regions one by one. ::

    $ git clone https://github.com/sjp38/masim
    $ cd masim; make; ./masim ./configs/zigzag.cfg &
    $ sudo damo record -o damon.data $(pidof masim)

The first two lines of commands start the monitoring target process in the
background.  You can substitute this with your real workload.  The last line
asks ``damo record`` to record the access pattern.


Visualizing Recorded Patterns
=============================

Below three commands visualize the recorded access patterns into three
image files, ``access_pattern_heatmap.png``, ``wss_dist.png``, and
``wss_chron_change.png``. ::

    $ damo report heats --heatmap -i damon.data access_pattern_heatmap.png
    $ damo report wss --range 0 101 1 --plot wss_dist.png
    $ damo report wss --range 0 101 1 --sortby time --plot wss_chron_change.png

- ``access_pattern_heatmap.png`` will show the data access pattern in a
  heatmap, which shows when (x-axis) what memory region (y-axis) is how
  frequently accessed (color).
- ``wss_dist.png`` will show the distribution of the working set size.
- ``wss_chron_change.png`` will show how the working set size has
  chronologically changed.

Below are the three images.  You can show those made with other realistic
workloads at external web pages [1]_ [2]_ [3]_.

.. list-table::

   * - .. kernel-figure::  damon_heatmap.png
          :alt:   the access pattern in heatmap format
          :align: center

          The access pattern in heatmap format.

     - .. kernel-figure::  damon_wss_dist.png
          :alt:    the distribution of working set size
          :align: center

          The distribution of working set size.

     - .. kernel-figure::  damon_wss_change.png
          :alt:    the chronological changes of working set size
          :align: center

          The chronological changes of working set size.

.. [1] https://damonitor.github.io/test/result/visual/latest/heatmap.1.html
.. [2] https://damonitor.github.io/test/result/visual/latest/wss_sz.html
.. [3] https://damonitor.github.io/test/result/visual/latest/wss_time.html
