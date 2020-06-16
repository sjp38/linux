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

.. [1] https://damonitor.github.io/test/result/perf/latest/html/
.. [2] https://lore.kernel.org/linux-mm/20200429124540.32232-1-sjpark@amazon.com/
