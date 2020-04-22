Implement Data Access Monitoring-based Memory Operation Schemes

Changes from Previous Version
=============================

- Drop loadable module support
- Use dedicated valid action checker function
- Rebase on v5.8 plus v19 DAMON

Introduction
============

DAMON[1] can be used as a primitive for data access awared memory management
optimizations.  For that, users who want such optimizations should run DAMON,
read the monitoring results, analyze it, plan a new memory management scheme,
and apply the new scheme by themselves.  Such efforts will be inevitable for
some complicated optimizations.

However, in many other cases, the users would simply want the system to apply a
memory management action to a memory region of a specific size having a
specific access frequency for a specific time.  For example, "page out a memory
region larger than 100 MiB keeping only rare accesses more than 2 minutes", or
"Do not use THP for a memory region larger than 2 MiB rarely accessed for more
than 1 seconds".

This RFC patchset makes DAMON to handle such data access monitoring-based
operation schemes.  With this change, users can do the data access aware
optimizations by simply specifying their schemes to DAMON.

[1] https://lore.kernel.org/linux-mm/20200706115322.29598-1-sjpark@amazon.com/

Evaluations
===========

We evaluated DAMON's overhead, monitoring quality and usefulness using 25
realistic workloads on my QEMU/KVM based virtual machine running a kernel that
v12 of this patchset is applied.

An experimental DAMON-based operation scheme for THP, ‘ethp’, removes 31.29% of
THP memory overheads while preserving 60.64% of THP speedup. Another
experimental DAMON-based ‘proactive reclamation’ implementation, ‘prcl’,
reduces 87.95% of residential sets and 29.52% of system memory footprint while
incurring only 2.15% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimentail THP optimization and proactive reclamation are not
for production, just only for proof of concepts.

Please refer to the official document[1] or "Documentation/admin-guide/mm: Add
a document for DAMON" patch in the latest DAMON patchset for detailed
evaluation setup and results.

[1] https://damonitor.github.io/doc/html/latest-damos

More Information
================

We prepared a showcase web site[1] that you can get more information.  There
are

- the official documentations[2],
- the heatmap format dynamic access pattern of various realistic workloads for
  heap area[3], mmap()-ed area[4], and stack[5] area,
- the dynamic working set size distribution[6] and chronological working set
  size changes[7], and
- the latest performance test results[8].

[1] https://damonitor.github.io/_index
[2] https://damonitor.github.io/doc/html/latest-damos
[3] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.0.html
[4] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.1.html
[5] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.2.html
[6] https://damonitor.github.io/test/result/visual/latest/rec.wss_sz.html
[7] https://damonitor.github.io/test/result/visual/latest/rec.wss_time.html
[8] https://damonitor.github.io/test/result/perf/latest/html/index.html

Baseline and Complete Git Tree
==============================

The patches are based on the v5.8 plus v19 DAMON patchset[1] and Minchan's
``do_madvise()`` patch[2], which retrieved from the -next tree.  You can also
clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v14

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v14

There are a couple of trees for entire DAMON patchset series that future
features are included.  The first one[3] contains the changes for latest
release, while the other one[4] contains the changes for next release.

[1] https://lore.kernel.org/linux-mm/20200804091416.31039-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200302193630.68771-2-minchan@kernel.org/
[3] https://github.com/sjp38/linux/tree/damon/master
[4] https://github.com/sjp38/linux/tree/damon/next

Sequence Of Patches
===================

The 1st patch accounts age of each region.  The 2nd patch implements the
handling of the schemes in DAMON and exports a kernel space programming
interface for it.  The 3rd patch implements a debugfs interface for the
privileged people and user programs.  The 4th patch implements schemes
statistics feature for easier tuning of the schemes and runtime access pattern
analysis.  The 5th patche adds selftests for these changes, and the 6th patch
adds human friendly schemes support to the user space tool for DAMON.  Finally,
the 7th patch documents this new feature in the document.

Patch History
=============

Changes from RFC v13
(https://lore.kernel.org/linux-mm/20200707093805.4775-1-sjpark@amazon.com/)
- Drop loadable module support
- Use dedicated valid action checker function
- Rebase on v5.8 plus v19 DAMON

Changes from RFC v12
(https://lore.kernel.org/linux-mm/20200616073828.16509-1-sjpark@amazon.com/)
 - Wordsmith the document, comment, commit messages
 - Support a scheme of max access count 0
 - Use 'unsigned long' for (min|max)_sz_region

Changes from RFC v11
(https://lore.kernel.org/linux-mm/20200609065320.12941-1-sjpark@amazon.com/)
 - Refine the commit messages (David Hildenbrand)
 - Clean up debugfs code

Changes from RFC v10
(https://lore.kernel.org/linux-mm/20200603071138.8152-1-sjpark@amazon.com/)
 - Fix the wrong error handling for schemes debugfs file
 - Handle the schemes stats from the user space tool
 - Remove the schemes implementation plan from the document

Changes from RFC v9
(https://lore.kernel.org/linux-mm/20200526075702.27339-1-sjpark@amazon.com/)
 - Rebase on v5.7
 - Fix wrong comments and documents for schemes apply conditions

Changes from RFC v8
(https://lore.kernel.org/linux-mm/20200512115343.27699-1-sjpark@amazon.com/)
 - Rewrite the document (Stefan Nuernberger)
 - Make 'damon_for_each_*' argument order consistent (Leonard Foerster)
 - Implement statistics for schemes
 - Avoid races between debugfs readers and writers
 - Reset age for only significant access frequency changes
 - Add kernel-doc comments in damon.h

Please refer to RFC v8 for previous history
