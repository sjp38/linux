DAMON: Support Physical Memory Address Space and Page-granularity Idleness Monitoring

NOTE: This is only an RFC for future features of DAMON patchset[1], which is
not merged in the mainline yet.  The aim of this RFC is to show how DAMON would
be evolved once it is merged in.  So, if you have some interest in this RFC,
please consider reviewing the DAMON patchset, either.

Changes from Previous Version
=============================

- Rebase on v5.10 + DAMON v23[1] + DAMOS v15.1[2]
- Implement primitives for page granularity idleness monitoring

Introduction
============

DAMON[1] programming interface users can extend DAMON for any address space by
configuring the address-space specific low level primitives with appropriate
ones.  However, because only the implementation for the virtual address spaces
is available now, the users should implement their own for other address
spaces.  Worse yet, the user space users who are using the debugfs interface of
'damon-dbgfs' module or the DAMON user space tool, cannot implement their own.
Therefore, this patchset implements reference implementation of the low level
primitives for two other common use cases.

First common use case is the physical memory address space monitoring.  This
patchset, hence, implements monitoring primitives for the physical address
space.  Further, this patchset links the implementation to the debugfs
interface and the user space tool for the user space users.

Another common use case is page-granularity idleness monitoring.  This is
already available via 'Idle Page Tracking', but it requires frequent
kernel-user context change.  DAMON can efficiently support the usecase via its
arbitrary type monitoring target feature.  This patchset, therefore, provides
yet another implementation of the monitoring primitives for the use case.
Nonetheless, this version of the patchset doesn't provide user space friendly
interface for that.  It would be included in a future version of this patchset.

Note that the primitives support only the online user memory, as same to the
Idle Page Tracking.

Baseline and Complete Git Trees
===============================

The patches are based on the v5.10 linux kernel plus DAMON v23 patchset[1] and
DAMOS RFC v15.1 patchset[2].  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v10

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v10

[1] https://lore.kernel.org/linux-doc/20201005105522.23841-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20201006123931.5847-1-sjpark@amazon.com/

Development Trees
-----------------

There are a couple of trees for entire DAMON patchset series and
features for future release.

- For latest release: https://github.com/sjp38/linux/tree/damon/master
- For next release: https://github.com/sjp38/linux/tree/damon/next

Long-term Support Trees
-----------------------

For people who want to test DAMON but using LTS kernels, there are another
couple of trees based on two latest LTS kernels respectively and containing the
'damon/master' backports.

- For v5.4.y: https://github.com/sjp38/linux/tree/damon/for-v5.4.y
- For v5.10.y: https://github.com/sjp38/linux/tree/damon/for-v5.10.y

Sequence of Patches
===================

The sequence of patches is as follow.

The first 5 patches allow the user space users manually set the monitoring
regions.  The 1st and 2nd patches implements the features in the 'damon-dbgfs'
and the user space tool, respectively.  Following two patches update
unittests (the 3rd patch) and selftests (the 4th patch) for the new feature.
Finally, the 5th patch documents this new feature.

Following 5 patches implement the physical memory monitoring.  The 6th patch
makes some primitive functions for the virtual address spaces primitive
reusable.  The 7th patch implements the physical address space monitoring
primitive using it.  The 8th and the 9th patches links the primitives to the
'damon-dbgfs' and the user space tool, respectively.  The 10th patch further
implement a handy NUMA specific memory monitoring feature on the user space
tool.  The 11th patch documents this new features.

The 12th patch again separates some functions of the physical address space
primitives to reusable, and the 13th patch implements the page granularity
idleness monitoring primitives using those.

Patch History
=============

Changes from RFC v9
(https://lore.kernel.org/linux-mm/20201007071409.12174-1-sjpark@amazon.com/)
- Rebase on v5.10 + DAMON v23[1] + DAMOS v15.1[2]
- Implement primitives for page granularity idleness monitoring

Changes from RFC v8
(https://lore.kernel.org/linux-mm/20200831104730.28970-1-sjpark@amazon.com/)
- s/snprintf()/scnprintf() (Marco Elver)
- Place three parts of DAMON (core, primitives, and dbgfs) in different files

Changes from RFC v7
(https://lore.kernel.org/linux-mm/20200818072501.30396-1-sjpark@amazon.com/)
- Add missed 'put_page()' calls
- Support unmapped LRU pages

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
