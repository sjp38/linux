.. SPDX-License-Identifier: GPL-2.0

==========================
Frequently Asked Questions
==========================

Why a new module, instead of extending perf or other user space tools?
======================================================================

First, because it needs to be lightweight as much as possible so that it can be
used online, any unnecessary overhead such as kernel - user space context
switching cost should be avoided.  Second, DAMON aims to be used by other
programs including the kernel.  Therefore, having a dependency on specific
tools like perf is not desirable.  These are the two biggest reasons why DAMON
is implemented in the kernel space.


Can 'idle pages tracking' or 'perf mem' substitute DAMON?
=========================================================

Idle page tracking is a low level primitive for access check of each physical
page frame.  'perf mem' is similar, though it can use sampling to minimize the
overhead.  DAMON is a higher-level framework for access patterns of data objects
that focused on memory management optimization and providing sophisticated
features for that including the overhead handling.  Therefore, 'idle pages
tracking' and 'perf mem' could provide a subset of DAMON's output, but cannot
substitute DAMON.  Rather than that, DAMON could use those as it's low-level
primitives.


How can I optimize my system's memory management using DAMON?
=============================================================

Because there are several ways for the DAMON-based optimizations, we wrote a
separate document, :doc:`guide`.  Please refer to that.


Does DAMON support virtual memory only?
========================================

For now, yes.  But, DAMON will be able to support various address spaces
including physical memory in near future.  An RFC patchset [1]_ for this
extension is already available.  Please refer :doc:`plans` for detailed plan
for this.

.. [1] https://lore.kernel.org/linux-mm/20200409094232.29680-1-sjpark@amazon.com/
