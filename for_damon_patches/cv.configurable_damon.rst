DAMON: Support Physical Memory Address Space Monitoring

Changes from Previous Version
=============================

- Use 42 as the fake target id for paddr instead of -1
- Fix a typo

Introduction
============

DAMON[1] programming interface users can extend DAMON for any address space by
configuring the address-space specific low level primitives with appropriate
ones including their own implementations.  However, because the implementation
for the virtual address space is only available now, the users should implement
their own for other address spaces.  Worse yet, the user space users who rely
on the debugfs interface and user space tool, cannot implement their own.

This patchset implements another reference implementation of the low level
primitives for the physical memory address space.  With this change, hence, the
kernel space users can monitor both the virtual and the physical address spaces
by simply changing the configuration in the runtime.  Further, this patchset
links the implementation to the debugfs interface and the user space tool for
the user space users.

Note that the implementation supports only the user memory, as same to the idle
page access tracking feature.

[1] https://lore.kernel.org/linux-mm/20200706115322.29598-1-sjpark@amazon.com/

Baseline and Complete Git Trees
===============================

The patches are based on the v5.8 plus DAMON v20 patchset[1] and DAMOS RFC v14
patchset[2].  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v7

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v7

[1] https://lore.kernel.org/linux-mm/20200817105137.19296-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200804142430.15384-1-sjpark@amazon.com/

Sequence of Patches
===================

The sequence of patches is as follow.

The first 5 patches allow the user space users manually set the monitoring
regions.  The 1st and 2nd patches implements the features in the debugfs
interface and the user space tool .  Following two patches each implement
unittests (the 3rd patch) and selftests (the 4th patch) for the new feature.
Finally, the 5th patch documents this new feature.

Following 5 patches implement the physical memory monitoring.  The 6th patch
implements the low level primitives.  The 7th and the 8th patches links the
primitives to the debugfs and the user space tool, respectively.  The 9th patch
further implement a handy NUMA specific memory monitoring feature on the user
space tool.  Finally, the 10th patch documents this new features.

Patch History
=============

Changes from RFC v6
(https://lore.kernel.org/linux-mm/20200805065951.18221-1-sjpark@amazon.com/)
- Use 42 as the fake target id for paddr instead of -1
- Fix typo

Changes from RFC v5
(https://lore.kernel.org/linux-mm/20200707144540.21216-1-sjpark@amazon.com/)
- Support nested iomem sections (Du Fan)
- Rebase on v5.8

Changes from RFC v4
(https://lore.kernel.org/linux-mm/20200616140813.17863-1-sjpark@amazon.com/)
 - Support NUMA specific physical memory monitoring

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
