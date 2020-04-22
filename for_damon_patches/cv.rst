Subject: Introduce Data Access MONitor (DAMON)

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
v13 DAMON patchset is applied.

DAMON is lightweight.  It increases system memory usage by only -0.39% and
consumes less than 1% CPU time in most case.  It slows target workloads down by
only 0.63%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 69.43% of
THP memory overheads while preserving 37.11% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 89.30% of residential sets and 22.40% of system memory footprint while
incurring only 1.98% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimentail THP optimization and proactive reclamation are not
for production, just only for proof of concepts.

Please refer to the official document[1] or "Documentation/admin-guide/mm: Add
a document for DAMON" patch in this patchset for detailed evaluation setup and
results.

[1] https://damonitor.github.io/doc/html/latest-damon

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
[3] https://damonitor.github.io/test/result/visual/latest/heatmap.0.html
[4] https://damonitor.github.io/test/result/visual/latest/heatmap.1.html
[5] https://damonitor.github.io/test/result/visual/latest/heatmap.2.html
[6] https://damonitor.github.io/test/result/visual/latest/wss_sz.html
[7] https://damonitor.github.io/test/result/visual/latest/wss_time.html
[8] https://damonitor.github.io/test/result/perf/latest/html/index.html

Baseline and Complete Git Trees
===============================

The patches are based on the v5.7.  You can also clone the complete git
tree:

    $ git clone git://github.com/sjp38/linux -b damon/patches/v15

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v15

There are a couple of trees for entire DAMON patchset series.  It includes
future features.  The first one[1] contains the changes for latest release,
while the other one[2] contains the changes for next release.

[1] https://github.com/sjp38/linux/tree/damon/master
[2] https://github.com/sjp38/linux/tree/damon/next

Sequence Of Patches
===================

The 1st patch exports 'lookup_page_ext()' to GPL modules so that it can be used
by DAMON even though it is built as a loadable module.

Next four patches implement the core of DAMON and it's programming interface.
The 2nd patch introduces DAMON module, it's data structures, and data structure
related common functions.  Following three patches (3rd to 5th) implements the
core mechanisms of DAMON, namely regions based sampling (patch 3), adaptive
regions adjustment (patch 4), and dynamic memory mapping chage adoption
(patch 5).

Following four patches are for low level users of DAMON.  The 6th patch
implements callbacks for each of monitoring steps so that users can do whatever
they want with the access patterns.  The 7th one implements recording of access
patterns in DAMON for better convenience and efficiency.  Each of next two
patches (8th and 9th) respectively adds a debugfs interface for privileged
people and/or programs in user space, and a tracepoint for other tracepoints
supporting tracers such as perf.

Two patches for high level users of DAMON follows.  To provide a minimal
reference to the debugfs interface and for high level use/tests of the DAMON,
the next patch (10th) implements an user space tool.  The 11th patch adds a
document for administrators of DAMON.

Next two patches are for tests.  The 12th patch provides unit tests (based on
the kunit) while the 13th patch adds user space tests (based on the kselftest).

Finally, the last patch (14th) updates the MAINTAINERS file.

Patch History
=============

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

Changes from v8
(https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/)
 - Make regions always aligned by minimal region size that can be changed
   (Stefan Nuernberger)
 - Store binary format version in the recording file (Stefan Nuernberger)
 - Use 'int' for pid instead of 'unsigned long' (Stefan Nuernberger)
 - Fix a race condition in damon thread termination (Stefan Nuernberger)
 - Optimize random value generation and recording (Stefan Nuernberger)
 - Clean up commit messages and comments (Stefan Nuernberger)
 - Clean up code (Stefan Nuernberger)
 - Use explicit signalling and 'do_exit()' for damon thread termination 
 - Add more typos to spelling.txt
 - Update the performance evaluation results
 - Describe future plans in the cover letter

Please refer to the v8 patchset to get older history.
