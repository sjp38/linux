.. SPDX-License-Identifier: GPL-2.0

============
Future Plans
============

DAMON is still on its early stage.  Below plans are still under development.


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

An RFC patchset for this plan is available [1]_.

.. [1] https://lore.kernel.org/linux-mm/20200409094232.29680-1-sjpark@amazon.com/
