DAMON: Support Physical Memory Address Space Monitoring

Changes from Previous Version (RFC[1])
======================================

- Rebase on latest -mm tree
- Remove page granularity idleness monitoring part

[1] https://lore.kernel.org/linux-mm/20201216094221.11898-1-sjpark@amazon.com/

Introduction
============

DAMON is currently supporting only virtual address spaces monitoring.  It can
be easily extended for various use cases and address spaces by configuring its
monitoring primitives layer to use appropriate primitives implementations,
though.  This patchset implements monitoring primitives for the physical
address space monitoring using the structure.

Baseline and Complete Git Trees
===============================

The patches are based on the latest -mm tree
(v5.15-rc4-mmots-2021-10-10-18-34)[1].  You can also clone the complete git
tree:

    $ git clone git://git.kernel.org/pub/scm/linux/kernel/git/sj/linux.git -b damon-paddr/patches/v1

The web is also available:
https://git.kernel.org/pub/scm/linux/kernel/git/sj/linux.git/tag/?h=damon-paddr/patches/v1

[1] https://github.com/hnaz/linux-mm/tree/v5.15-rc4-mmots-2021-10-10-18-34

Sequence of Patches
===================

The first 3 patches allow the user space users manually set the monitoring
regions.  The 1st patch implements the feature in the 'damon-dbgfs'.  Then,
patches for adding a unit tests (the 2nd patch) and updating the documentation
(the 3rd patch) follow.

Following 4 patches implement the physical address space monitoring primitives.
The 4th patch makes some primitive functions for the virtual address spaces
primitives reusable.  The 5th patch implements the physical address space
monitoring primitives.  The 6th patch links the primitives to the
'damon-dbgfs'.  Finally, 7th patch documents this new features.

Patch History
=============

Changes from RFC v10
(https://lore.kernel.org/linux-mm/20201216094221.11898-1-sjpark@amazon.com/)
- Rebase on latest -mm tree
- Remove page granularity idleness monitoring part

Please refer to RFC v10 for previous history.
