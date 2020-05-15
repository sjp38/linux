.. SPDX-License-Identifier: GPL-2.0

====================
The Beginner's Guide
====================

This document is to help you estimating amount of benefit that you could get
from DAMON, and to let you know how you could maximize the improvement.  You
are assumed to already read :doc:`start`.


Check The Signs
===============

DAMON cannot provide the same amount of benefit to every workload.  Therefore
you should first guess how much improvements you could get using DAMON.  If
some of below conditions match to your situation, you could consider use of
DAMON.


- *Low IPC and High Cache Miss Ratios.*  Low IPC means most of CPU time is
  spend waiting for completion of time consuming operations such as memory
  access, while high cache miss ratios means the caches need more help.  DAMON
  is not for cache level optimization, but for DRAM level.  However, improving
  DRAM management will also help this case by reducing the memory operation
  latency.
- *Memory Over-commitment and Unknown Users.*  If you are doing memory
  overcommitment and you cannot control every user of your system, memory bank
  run could happen at anytime.  You can estimate when it will happen based on
  DAMON's monitoring results and act earlier to avoid the pressure.
- *Frequent Memory Pressure.*  Frequent memory pressure means wrong configuration
  or existance of memory hog.  DAMON will help you finding right configuration
  and/or the criminal.
- *Heterogeneous Memory System.*  If your system is utilizing memory devices that
  placed between DRAM and traditional hard disk, such as non-volatile memory or
  fast SSDs, DAMON could help you utilizing the devices more efficiently.


Profile
=======

If you found some positive signals, you could start from profiling your
workloads using DAMON.  Find major workloads on your systems and analyze their
data access pattern to find something wrong or can be improved.  The DAMON user
space tool (``damo``) will be enough for this.

We recommend you to start from working set size distribution check using ``damo
report wss``.  If the distribution is ununiform, you could consider `Memory
Configuration`_ optimization.

Then, review the overall access pattern in heatmap form using ``damo report
heats``.  If it shows a simple pattern consists with small number of memory
regions having high contrast of access temperature, you could consider `Manual
Program Optimization`_.

If the access pattern is very frequently changing so that you cannot figure out
what is the performance important region and not using your human eye,
`Automated DAMON-based Memory Operations`_ might help owing to its machine
level micro scope view.

You don't need to take only one approach among above plans, but you could use
multiple of above approaches to maximize the benefit.  If you still want to
absorb more benefit, you should develop `Personalized DAMON Application`_ for
your special case.


Optimize
========

If you have confirmed the sign and the profiling result also says it's worth to
try some optimization, you could consider below optimizations.  Note that most
of below sections assume that your system has configured swap device or other
types of auxiliary memory so that you don't need to accommodate whole working
set in the main memory.  Most of the detailed optimization should be made on
your concrete understanding about the swap or the auxiliary memory performance.


Memory Configuration
--------------------

DRAM is highly performance critical but expensive and heavily consumes the
power.  However, knowing the real important working set size is hard and thus
people usually equips unnecessarily large DRAM or too small DRAM.  Actually,
so many memory management problem comes from this wrong configuration.

Using the working set size distribution report provided by ``damo report wss``,
you can know the real needs and further make various tradeoff.  For example,
roughly speaking, if you care only 95 percentile latency, you don't need to
equip DRAM of size larger than 95 percentile working set size.  If you were
suffered from frequent memory pressure, you will also easily know how much DRAM
you should buy.

Let's see a realistic example.  Below is the heatmap and the working set size
distributions/changes of ``freqmine`` workload in PARSEC3 benchmark suite.  The
working set is about 700 MiB at most, but less than 100 MiB for more than 95%
of the time.  Even though you give only 100 MiB of memory space to the
workload, it will work well for 95% of time, while you can save the 600 MiB of
memory space.

.. list-table::

   * - .. kernel-figure::  freqmine_heatmap.png
          :alt:   the access pattern in heatmap format
          :align: center

          The access pattern in heatmap format.

     - .. kernel-figure::  freqmine_wss_sz.png
          :alt:    the distribution of working set size
          :align: center

          The distribution of working set size.

     - .. kernel-figure::  freqmine_wss_time.png
          :alt:    the chronological changes of working set size
          :align: center

          The chronological changes of working set size.


Program Modification
--------------------

If the data access pattern heatmap plotted using ``damo report heats`` is quite
simple and clear, you could be able to know how thing is going in the workload,
and how you could make optimize memory management.

For example, suppose that the workload has two big memory object but only one
object is frequently accessed while the other is only occasionally scanned.
Than, you could modify the program source code to invoke ``mlock()`` or
``madvise()`` with ``WILLNEED`` system call for the hot object.  Or, you could
use ``madvise()`` with ``MADV_COLD`` or ``MADV_PAGEOUT`` for the cold objects.
Using both together would be also worth to try.

A previous work [1]_ using the ``mlock()`` achieved up to 2.55x performance
speedup.

Let's see another realistic example access pattern for this kind of
optimizations.  Below are the visualized access pattern of streamcluster
workload in PARSEC3 benchmark suite.  We can easily see a 100 MiB sized memory
object is the hot object.

.. list-table::

   * - .. kernel-figure::  streamcluster_heatmap.png
          :alt:   the access pattern in heatmap format
          :align: center

          The access pattern in heatmap format.

     - .. kernel-figure::  streamcluster_wss_sz.png
          :alt:    the distribution of working set size
          :align: center

          The distribution of working set size.

     - .. kernel-figure::  streamcluster_wss_time.png
          :alt:    the chronological changes of working set size
          :align: center

          The chronological changes of working set size.


Automated DAMON-based Memory Operations
---------------------------------------

Though `Manual Program Optimization` works well in many cases and DAMON can
help it, modifying the source code is not a good option in many cases.  First
of all, the source code could be too old or unavailable.  And, many workloads
will have complex data access pattern that even hard to distinguish hot memory
objects and cold memory objects with human eye.  Finding the mapping from the
visualized access pattern to the source code and injecting the hinting system
calls inside the code will also be quite challenging.

By using DAMON-based operation schemes (DAMOS) via ``damo schemes``, you will
be able to easily optimize your workload in such a cases.  Our example schemes
called 'efficient THP' and 'proactive reclamation' achieved significant speedup
and memory space save against 25 realistic workloads [2]_, [3]_.

That said, note that you need careful tune of the schemes (e.g., target region
size and age) and monitoring attributes for successful use of this approach.
Because the optimal values of the parameters will be dependent on each system
and workload, misconfiguring the parameters could result in worse memory
management.

For the tuning, you could measure the performance metrics such as IPC, TLB
misses, and swap in/out events for varying parameters.  Writing a program
automating this optimal parameter could be an option.


Personalized DAMON Application
------------------------------

DAMOS will work well for many cases, but would not be able to squeeze the last
benefit in some special cases because only very simple schemes are specifiable.
The parameter tuning problem is also one of the example.

To mitigate this limitation, you should forgive the comfortable use of the user
space tool and start use of the debugfs interface.  You will be able to write
your personalized DAMON application that interact with DAMON via the debugfs
interface (refer to :doc:`usage` for detail).  Using this, you will be able to
overcome the limitation of the simple DAMOS specification and make more
creative and wise optimizations.

If you are kernel space programmer, writing kernel space DAMON applications
using the API (refer to :doc:`api` for more detail) would be also an option.


Previous Practices for References
=================================

Referencing previously done successful practices could help you getting the
sense for this kind of optimizations.
There is an academic paper [1]_ previously reported the visualized access
pattern and `Manual Program Optimization`_ results for a number of realistic
workloads.  You can also get the visualized access pattern [4]_, [5]_, [6]_ and
`Automated DAMON-based Memory Operations`_ results for other realistic
workloads that collected with latest version of DAMON [2]_, [3]_.

.. [1] https://dl.acm.org/doi/10.1145/3366626.3368125
.. [2] https://damonitor.github.io/test/result/perf/latest/html/
.. [3] https://lore.kernel.org/linux-mm/20200512115343.27699-1-sjpark@amazon.com/
.. [4] https://damonitor.github.io/test/result/visual/latest/heatmap.1.html
.. [5] https://damonitor.github.io/test/result/visual/latest/wss_sz.html
.. [6] https://damonitor.github.io/test/result/visual/latest/wss_time.html
