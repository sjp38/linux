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
primitives.

Kernel space users can also implement the primitives by themselves for their
special use cases.  Clean/dirty/entire page cache, NUMA nodes, specific files,
or block devices would be examples of such special use cases.

[1] https://lore.kernel.org/linux-mm/20200602130125.20467-1-sjpark@amazon.com/


Baseline and Complete Git Trees
===============================

The patches are based on the v5.7 plus DAMON v15 patchset[1] and DAMOS RFC v11
patchset[2].  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v3

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v3

[1] TODO: Add DAMON v15 patchset link
[2] TODO: Add DAMOS v11 patchset link


Sequence of Patches
===================

The sequence of patches is as follow.  The 1st patch defines the monitoring
region again based on pure address range abstraction so that no assumption of
virtual memory is in there.  The following 2nd patch cleans up code using the
new abstraction.

The 3rd patch allows users to configure the low level pritimives for
initialization and dynamic update of the target address regions, which were
previously coupled with virtual memory area.  Then, the 4th patch allow user
space to also be able to set the monitoring target regions and document it in
the 5th patch.

The 5th patch further makes the access check primitives, which were based on
PTE Accessed bit, configurable.  Now any address space can be supported.  The
6th patch provides the reference implementations of the configurable primitives
for physical memory monitoring.  The 7th patch makes the debugfs interface to
be able to use the physical memory monitoring, and finally the 8th patch
documents this.


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
