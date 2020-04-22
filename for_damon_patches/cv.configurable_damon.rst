DAMON: Support Physical Memory Address Space Monitoring

DAMON[1] programming interface users can extend DAMON for any address space by
implementing and using their own address-space specific low level primitives.
However, the user space users who rely on the debugfs interface and user space
tool, can monitor the virtual address space only.  This is mainly due to DAMON
is providing the reference implementation of the low level primitives for the
virtual address space only.

This patchset implements another reference implementation of the low level
primitives for the physical memory address space.  Therefore, users can monitor
both of the virtual and the physical address spaces by simply configuring the
provided low level primitives.   Further, this patchset links the
implementation to the debugfs interface and the user space tool, so that user
space users can also use the features.

Note that the implementation supports only the user memory, as same to the idle
page access tracking feature.

[1] https://lore.kernel.org/linux-mm/20200615161927.12637-1-sjpark@amazon.com/


Baseline and Complete Git Trees
===============================

The patches are based on the v5.7 plus DAMON v16 patchset[1] and DAMOS RFC v12
patchset[2].  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v4

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v4

[1] https://lore.kernel.org/linux-mm/20200615161927.12637-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200616073828.16509-1-sjpark@amazon.com/


Sequence of Patches
===================

The sequence of patches is as follow.

The 1st and 2nd patches allow the debugfs interface and the user space tool to
be able to set the monitoring target regions as they want, respectively.  The
3rd patch documents the feature.

The 4th patch exports rmap essential functions to GPL modules as those are
required from the DAMON's reference implementation of the low level primitives
for the physical memory address space.  The 5th patch provides the reference
implementations of the configurable primitives for the physical memory
monitoring.  The 6th and 7th patches make the user space to be able to use the
physical memory monitoring via debugfs and the user space tool, respectively.
Finally, the 8th patch documents the physical memory monitoring support.


Patch History
=============

Changes from RFC v3
(https://lore.kernel.org/linux-mm/20200609141941.19184-1-sjpark@amazon.com/)
 - Export rmap functions
 - Reorganize for physical memory monitoring support only
 - Clean up debugfs code

Changes from RFC v2
(https://lore.kernel.org/linux-mm/20200603141135.10575-1-sjpark@amazon.com/)
 - Support the physical memory monitoring with the user space tool
 - Use 'pfn_to_online_page()' (David Hildenbrand)
 - Document more detail on random 'pfn' and its safeness (David Hildenbrand)

Changes from RFC v1
(https://lore.kernel.org/linux-mm/20200409094232.29680-1-sjpark@amazon.com/)
 - Provide the reference primitive implementations for the physical memory
 - Connect the extensions with the debugfs interface
