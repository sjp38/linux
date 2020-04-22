Subject: Introduce Data Access MONitor (DAMON)

Introduction
============

DAMON is a data access monitoring framework subsystem for the Linux kernel.
The core mechanisms of DAMON called 'region based sampling' and adaptive
regions adjustment' (refer to :doc:`mechanisms` for the detail) make it
accurate, efficient, and scalable.  Using this framework, therefore, the
kernel's core memory management mechanisms including reclamation and THP can be
optimized for better memory management.  The memory management optimization
works that have not merged into the mainline due to their high data access
monitoring overhead will be able to have another try.  In user space,
meanwhile, users who have some special workloads will be able to write
personalized tools or applications for more understanding and specialized
optimizations of their systems using the DAMON as a framework.

More Information
================

We prepared a showcase web site[1] that you can get more information of DAMON.
There are

- the official documentation of DAMON[2],
- the heatmap format dynamic access pattern of various realistic workloads for
  heap area[3], mmap()-ed area[4], and stack[5] area,
- the dynamic working set size distribution[6] and chronological working set
  size changes[7], and
- the latest performance test results[8].

[1] https://damonitor.github.io
[2] https://damonitor.github.io/doc/html/latest
[3] https://damonitor.github.io/test/result/visual/latest/heatmap.0.html
[4] https://damonitor.github.io/test/result/visual/latest/heatmap.1.html
[5] https://damonitor.github.io/test/result/visual/latest/heatmap.2.html
[6] https://damonitor.github.io/test/result/visual/latest/wss_sz.html
[7] https://damonitor.github.io/test/result/visual/latest/wss_time.html
[8] https://damonitor.github.io/test/result/perf/latest/html/index.html

Evaluations
===========

We evaluated DAMON's overhead, monitoring quality and usefulness using 25
realistic workloads on my QEMU/KVM based virtual machine.

DAMON is lightweight.  It consumes only 0.03% more system memory and up to 1%
CPU time.  It makes target worloads only 0.7% slower.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP removes 63.12% of THP memory
overheads while preserving 49.15% of THP speedup.  Another experimental
DAMON-based 'proactive reclamation' implementation reduces 85.85% of
residentail sets and 21.98% of system memory footprint while incurring only
2.42% runtime overhead in best case (parsec3/freqmine).

NOTE that the experimentail THP optimization and proactive reclamation are not
for production, just only for proof of concepts.

Please refer to 'Appendix C' for detailed evaluation setup and results.

Baseline and Complete Git Trees
===============================

The patches are based on the v5.6.  You can also clone the complete git
tree:

    $ git clone git://github.com/sjp38/linux -b damon/patches/v12

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v12

There are a couple of trees for entire DAMON patchset series.  The first one[1]
contains the changes for latest release, while the other one[2] contains the
changes for next release.

[1] https://github.com/sjp38/linux/tree/damon/master
[2] https://github.com/sjp38/linux/tree/damon/next

Sequence Of Patches
===================

The patches are organized in the following sequence.  The first two patches are
preparation of DAMON patchset.  The 1st patch adds typos found in previous
versions of DAMON patchset to 'scripts/spelling.txt' so that the typos can be
caught by 'checkpatch.pl'.  The 2nd patch exports 'lookup_page_ext()' to GPL
modules so that it can be used by DAMON even though it is built as a loadable
module.

Next five patches implement the core of DAMON and it's programming interface.
The 3rd patch introduces DAMON module, it's data structures, and data structure
related common functions.  Following four patches (4nd to 7th) implements the
core mechanisms of DAMON, namely regions based sampling (patch 4), adaptive
regions adjustment (patches 5-6), and dynamic memory mapping chage adoption
(patch 7).

Following four patches are for low level users of DAMON.  The 8th patch
implements callbacks for each of monitoring steps so that users can do whatever
they want with the access patterns.  The 9th one implements recording of access
patterns in DAMON for better convenience and efficiency.  Each of next two
patches (10th and 11th) respectively adds a debugfs interface for privileged
people and/or programs in user space, and a tracepoint for other tracepoints
supporting tracers such as perf.

Two patches for high level users of DAMON follows.  To provide a minimal
reference to the debugfs interface and for high level use/tests of the DAMON,
the next patch (12th) implements an user space tool.  The 13th patch adds a
document for administrators of DAMON.

Next two patches are for tests.  The 14th and 15th patches provide unit tests
(based on kunit) and user space tests (based on kselftest), respectively.

Finally, the last patch (16th) updates the MAINTAINERS file.

Patch History
=============

Below are main changes for recent 5 versions of this patchset.  Please refer to
oldest patchset listed here to get older history.

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

Changes from v7
(https://lore.kernel.org/linux-mm/20200318112722.30143-1-sjpark@amazon.com/)
 - Cleanup variable names (Jonathan Cameron)
 - Split sampling address setup from access_check() (Jonathan Cameron)
 - Make sampling address to always locate in the region (Jonathan Cameron)
 - Make initial region's sampling addr to be old (Jonathan Cameron)
 - Split kdamond on/off function to seperate functions (Jonathan Cameron)
 - Fix wrong kernel doc comments (Jonathan Cameron)
 - Reset 'last_accessed' to false in kdamond_check_access() if necessary
 - Rebase on v5.6
