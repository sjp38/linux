.. SPDX-License-Identifier: GPL-2.0

============
Future Plans
============

DAMON is still on its first stage.  Below plans are still under development.


Automate Data Access Monitoring-based Memory Operation Schemes Execution
========================================================================

The ultimate goal of DAMON is being used as a building block of the data access
pattern aware kernel memory management subsystem optimization.  However, as
always, some users having very special workloads will want to do their
optimization.  DAMON will automate most of the tasks for such manual
optimizations soon.  Users will be required to only describe what kind of data
access pattern-based operation schemes they want in a simple form.

By applying a very simple scheme for THP promotion/demotion with a prototype
implementation, DAMON reduced 60% of THP memory footprint overhead while
preserving 50% of the THP performance benefit.  The detailed results can be
seen on an external web page [1]_.

Several RFC patchsets for this plan are available [2]_.


Support Various Address Spaces
==============================

Currently, DAMON supports only virtual memory address spaces because it
utilizes PTE Accessed bits as its low-level access check primitive and ``struct
vma`` as a way to address the monitoring target regions.  However, the core
idea of DAMON is in a separate higher layer.  Therefore, DAMON can support
other various address spaces by changing the two low primitives to others for
the address spaces.

In the future, DAMON will make the lower level primitives configurable so that
it can support various address spaces including physical memory.  The
configuration will be highly flexible so that users can even implement the
primitives by themselves for their special use cases.  Monitoring of
clean/dirty/entire page cache, NUMA nodes, specific files, or block devices
would be examples of such use cases.

An RFC patchset for this plan is available [3]_.

.. [1] https://damonitor.github.io/test/result/perf/latest/html/
.. [2] https://lore.kernel.org/linux-mm/20200429124540.32232-1-sjpark@amazon.com/
.. [3] https://lore.kernel.org/linux-mm/20200409094232.29680-1-sjpark@amazon.com/
