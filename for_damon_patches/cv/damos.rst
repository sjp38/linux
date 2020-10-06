Implement Data Access Monitoring-based Memory Operation Schemes

NOTE: This is only an RFC for future features of DAMON patchset[1], which is
not merged in the mainline yet.  The aim of this RFC is to show how DAMON would
be evolved once it is merged in.  So, if you have some interest in this RFC,
please consider reviewing the DAMON patchset, either.

Changes from Previous Version
=============================

Only below minor changes made on v15 of this patchset.  Therefore I'm setting
the version number to v15.1 rather than v16.

- Rebase on v5.10 + DAMON v23[1]
- Drop the dependency on Minchan's out-of-tree patch (It's merged in v5.10)

Introduction
============

DAMON[1] can be used as a primitive for data access aware memory management
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
Operation Schemes (DAMOS).  With this change, users can do the data access
aware optimizations by simply specifying their schemes.

[1] https://lore.kernel.org/linux-mm/20201215115448.25633-1-sjpark@amazon.com

Evaluations
===========

We evaluated DAMON's overhead, monitoring quality and usefulness using 24
realistic workloads on my QEMU/KVM based virtual machine running a kernel that
v23 DAMON patchset is applied.

DAMON is lightweight.  It increases system memory usage by 0.42% and slows
target workloads down by 0.39%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 81.45% of
THP memory overheads while preserving 50.09% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 91.45% of residential sets and 22.91% of system memory footprint while
incurring only 2.43% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimental THP optimization and proactive reclamation are not
for production but only for proof of concepts.

Please refer to the official document[1] or "Documentation/admin-guide/mm: Add
a document for DAMON" patch in this patchset for detailed evaluation setup and
results.

[1] https://damonitor.github.io/doc/html/latest-damon/admin-guide/mm/damon/eval.html

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

The patches are based on the v5.10 plus v23 DAMON patchset[1].  You can also
clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v15.1

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v15.1

[1] https://lore.kernel.org/linux-doc/20201005105522.23841-1-sjpark@amazon.com/

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

Sequence Of Patches
===================

The 1st patch accounts age of each region.  The 2nd patch implements the
core of the DAMON-based operation schemes feature.  The 3rd patch makes
the default monitoring primitives for virtual address spaces to support the
schemes.  From this point, the kernel space users can use DAMOS.  The 4th patch
exports the feature to the user space via the debugfs interface.  The 5th patch
implements schemes statistics feature for easier tuning of the schemes and
runtime access pattern analysis.  The 6th patch adds selftests for these
changes, and the 7th patch adds human friendly schemes support to the user
space tool for DAMON.  Finally, the 8th patch documents this new feature.

Patch History
=============

Changes from RFC v15
(https://lore.kernel.org/linux-mm/20201006123931.5847-1-sjpark@amazon.com/)
- Rebase on v5.10 + DAMON v23[1]
- Drop the dependency on Minchan's out-of-tree patch (It's merged in v5.10)

Changes from RFC v14
(https://lore.kernel.org/linux-mm/20200804142430.15384-1-sjpark@amazon.com/)
- s/snprintf()/scnprintf() (Marco Elver)
- Place three parts of DAMON (core, primitives, and dbgfs) in different files

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
