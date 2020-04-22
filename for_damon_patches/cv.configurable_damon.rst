DAMON: Support Access Monitoring of Any Address Space Including Physical Memory

Currently, DAMON[1] supports only virtual memory address spaces because it
utilizes PTE Accessed bits as its low-level access check primitive and ``struct
vma`` as a way to address the monitoring target regions.  However, the core
idea of DAMON, which makes it able to provide the accurate, efficient, and
scalable monitoring, is in a separate higher layer.  Therefore, DAMON can be
extended for other various address spaces by changing the two low primitives to
others for the address spaces.

This patchset makes the DAMON's low level primitives configurable and provide
reference implementation of the primitives for the virtual memory address
spaces and the physical memory address space.  Therefore, users can monitor
both of the two address spaces by simply configuring the provided low level
primitives.  Note that only the user memory is supported, as same to the idle
page access tracking feature.

After this patchset, the programming interface users can implement the
primitives by themselves for their special use cases.  Clean/dirty/entire page
cache, NUMA nodes, specific files, or block devices would be examples of such
special use cases.

[1] https://lore.kernel.org/linux-mm/20200608114047.26589-1-sjpark@amazon.com/


Baseline and Complete Git Trees
===============================

The patches are based on the v5.7 plus DAMON v15 patchset[1] and DAMOS RFC v11
patchset[2].  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v3

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v3

[1] https://lore.kernel.org/linux-mm/20200608114047.26589-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200609065320.12941-1-sjpark@amazon.com/


Sequence of Patches
===================

The sequence of patches is as follow.  The 1st patch defines the monitoring
region again based on pure address range abstraction so that no assumption of
virtual memory is in there.

The 2nd patch allows users to configure the low level pritimives for
initialization and dynamic update of the target address regions, which were
previously coupled with the virtual memory.  Then, the 3rd and 4th patches
allow user space to also be able to set the monitoring target regions via the
debugfs and the user space tool.  The 5th patch documents this feature.

The 6th patch makes the access check primitives, which were coupled with the
virtual memory address, freely configurable.  Now any address space can be
supported.  The 7th patch provides the reference implementations of the
configurable primitives for the physical memory monitoring.  The 8th and 9th
patch makes the user space to be able to use the physical memory monitoring via
debugfs and the user space tool, respectively.  Finally, the 10th patch
documents the physical memory monitoring support.


Patch History
=============

Changes from RFC v2
(https://lore.kernel.org/linux-mm/20200603141135.10575-1-sjpark@amazon.com/)
 - Support the physical memory monitoring with the user space tool
 - Use 'pfn_to_online_page()' (David Hildenbrand)
 - Document more detail on random 'pfn' and its safeness (David Hildenbrand)

Changes from RFC v1
(https://lore.kernel.org/linux-mm/20200409094232.29680-1-sjpark@amazon.com/)
 - Provide the reference primitive implementations for the physical memory
 - Connect the extensions with the debugfs interface
