Subject: Introduce Data Access MONitor (DAMON)

Changes from Previous Version
=============================

- Wordsmith/cleanup the documentations and the code
- user space tool: Cleanup and add an option for reuse histogram (patch 11)
- recording: Check disablement condition properly
- recording: Force minimal recording buffer size (1KB)

Introduction
============

DAMON is a data access monitoring framework subsystem for the Linux kernel.
The core mechanisms of DAMON called 'region based sampling' and 'adaptive
regions adjustment' (refer to 'mechanisms.rst' in the 11th patch of this
patchset for the detail) make it

 - accurate (The monitored information is useful for DRAM level memory
   management. It might not appropriate for Cache-level accuracy, though.),
 - light-weight (The monitoring overhead is low enough to be applied online
   while making no impact on the performance of the target workloads.), and
 - scalable (the upper-bound of the instrumentation overhead is controllable
   regardless of the size of target workloads.).

Using this framework, therefore, the kernel's core memory management mechanisms
such as reclamation and THP can be optimized for better memory management.  The
experimental memory management optimization works that incurring high
instrumentation overhead will be able to have another try.  In user space,
meanwhile, users who have some special workloads will be able to write
personalized tools or applications for deeper understanding and specialized
optimizations of their systems.

Evaluations
===========

We evaluated DAMON's overhead, monitoring quality and usefulness using 25
realistic workloads on my QEMU/KVM based virtual machine running a kernel that
v16 DAMON patchset is applied.

DAMON is lightweight. It increases system memory usage by only -0.25% and
consumes less than 1% CPU time in most case. It slows target workloads down by
only 0.94%.

DAMON is accurate and useful for memory management optimizations. An
experimental DAMON-based operation scheme for THP, 'ethp', removes 31.29% of
THP memory overheads while preserving 60.64% of THP speedup. Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 87.95% of residential sets and 29.52% of system memory footprint while
incurring only 2.15% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimentail THP optimization and proactive reclamation are not
for production, just only for proof of concepts.

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
[2] https://damonitor.github.io/doc/html/latest-damon
[3] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.0.png.html
[4] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.1.png.html
[5] https://damonitor.github.io/test/result/visual/latest/rec.heatmap.2.png.html
[6] https://damonitor.github.io/test/result/visual/latest/rec.wss_sz.png.html
[7] https://damonitor.github.io/test/result/visual/latest/rec.wss_time.png.html
[8] https://damonitor.github.io/test/result/perf/latest/html/index.html

Baseline and Complete Git Trees
===============================

The patches are based on the v5.7.  You can also clone the complete git
tree:

    $ git clone git://github.com/sjp38/linux -b damon/patches/v17

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v17

There are a couple of trees for entire DAMON patchset series.  It includes
future features.  The first one[1] contains the changes for latest release,
while the other one[2] contains the changes for next release.

[1] https://github.com/sjp38/linux/tree/damon/master
[2] https://github.com/sjp38/linux/tree/damon/next

Sequence Of Patches
===================

The 1st patch exports 'lookup_page_ext()' to GPL modules so that it can be used
by DAMON even though it is built as a loadable module.

Next four patches implement the target address space independent core logics of
DAMON and it's programming interface.  The 2nd patch introduces DAMON module,
it's data structures, and data structure related common functions.  Following
three patches (3rd to 5th) implements the core mechanisms of DAMON, namely
regions based sampling (patch 3), adaptive regions adjustment (patch 4), and
dynamic memory mapping chage adoption (patch 5).

The following one (patch 6) implements the virtual memory address space
specific functions.

Following four patches are for more user friendly interfaces.  The 7th patch
implements recording of access patterns in DAMON.  Each of next two patches
(8th and 9th) respectively adds a tracepoint for other tracepoints supporting
tracers such as perf, and a debugfs interface for privileged people and/or
programs in user space.

Three patches for high level users of DAMON follows.  To provide a minimal
reference to the debugfs interface and for high level use/tests of the DAMON,
the next patch (10th) implements an user space tool.  The 11th patch adds one
more option to the tool for reuse histogram plot.  The 12nd patch adds a
document for administrators of DAMON.

Next two patches are for tests.  The 13th patch provides unit tests (based on
the kunit) while the 14th patch adds user space tests (based on the kselftest).

Finally, the last patch (15th) updates the MAINTAINERS file.

Patch History
=============

This version of patchset merges first, second, and sixth patches in the CDAMON
patchset[1] for better readability.  As the changes are not subtle,
'Reviewed-by's I previously received on the changes affected patches are also
dropped.

[1] https://lore.kernel.org/linux-mm/20200609141941.19184-1-sjpark@amazon.com/

Changes from v16
(https://lore.kernel.org/linux-mm/20200615161927.12637-1-sjpark@amazon.com/)
 - Wordsmith/cleanup the documentations and the code
 - user space tool: Simplify the code and add wss option for reuse histogram
 - recording: Check disablement condition properly
 - recording: Force minimal recording buffer size (1KB)

Changes from v15
(https://lore.kernel.org/linux-mm/20200608114047.26589-1-sjpark@amazon.com/)
 - Refine commit messages (David Hildenbrand)
 - Optimizes three vma regions search (Varad Gautam)
 - Support static granularity monitoring (Shakeel Butt)
 - Cleanup code and re-organize the sequence of patches

Changes from v14
(https://lore.kernel.org/linux-mm/20200602130125.20467-1-sjpark@amazon.com/)
 - Directly pass region and task to tracepoint (Steven Rostedt)
 - Refine comments for better read
 - Add more 'Reviewed-by's (Leonard Foerster, Brendan Higgins)

Changes from v13
(https://lore.kernel.org/linux-mm/20200525091512.30391-1-sjpark@amazon.com/)
 - Fix a typo (Leonard Foerster)
 - Fix wring condition of three sub ranges split (Leonard Foerster)
 - Rebase on v5.7

Changes from v12
(https://lore.kernel.org/linux-mm/20200518100018.2293-1-sjpark@amazon.com/)
 - Avoid races between debugfs readers and writers
 - Add kernel-doc comments in damon.h

Changes from v11
(https://lore.kernel.org/linux-mm/20200511123302.12520-1-sjpark@amazon.com/)
 - Rewrite the document (Stefan Nuernberger)
 - Make 'damon_for_each_*' argument order consistent (Leonard Foerster)
 - Fix wrong comment in 'kdamond_merge_regions()' (Leonard Foerster)

Changes from v10
(https://lore.kernel.org/linux-mm/20200505110815.10532-1-sjpark@amazon.com/)
 - Reduce aggressive split overhead by doing it only if required

Changes from v9
(https://lore.kernel.org/linux-mm/20200427120442.24179-1-sjpark@amazon.com/)
 - Split each region into 4 subregions if possible (Jonathan Cameraon)
 - Update kunit test for the split code change

Please refer to the v9 patchset to get older history.
