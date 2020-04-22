Subject: Introduce Data Access MONitor (DAMON)

Changes from Previous Version
=============================

- Place 'CREATE_TRACE_POINTS' after '#include' statements (Steven Rostedt)
- Support large record file (Alkaid)
- Place 'put_pid()' of virtual monitoring targets in 'cleanup' callback
- Avoid conflict between concurrent DAMON users
- Update evaluation result document

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
v20 DAMON patchset is applied.

DAMON is lightweight.  It increases system memory usage by 0.12% and slows
target workloads down by 1.39%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 88.16% of
THP memory overheads while preserving 88.73% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 91.34% of residential sets and 25.59% of system memory footprint while
incurring only 1.58% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimentail THP optimization and proactive reclamation are not
for production but just only for proof of concepts.

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

The patches are based on the v5.8.  You can also clone the complete git
tree:

    $ git clone git://github.com/sjp38/linux -b damon/patches/v20

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v20

There are a couple of trees for entire DAMON patchset series.  It includes
future features.  The first one[1] contains the changes for latest release,
while the other one[2] contains the changes for next release.

[1] https://github.com/sjp38/linux/tree/damon/master
[2] https://github.com/sjp38/linux/tree/damon/next

Sequence Of Patches
===================

First four patches implement the target address space independent core logics
of DAMON and it's programming interface.  The 1st patch introduces DAMON
subsystem, it's data structures, and the data structure related basic
manipulation functions.  Following three patches (2nd to 4th) implements the
core mechanisms of DAMON, namely regions based sampling (patch 2), adaptive
regions adjustment (patch 3), and dynamic memory mapping change adoption (patch
4).

Now the essential parts of DAMON is complete but require low level primitives
to be implemented and configured with DAMON to just work.  The following two
patches makes it just work for virtual address spaces monitoring.  The 5th
patch makes 'PG_idle' could be used by DAMON and the 6th patch implements the
virtual memory address space specific low primitives using page table Accessed
bits and the 'PG_idle' page flag.

Now DAMON just works for virtual address space monitoring via the kernel space
api.  Following six patches adds interfaces for the users in the user space.
The 7th patch implements recording of access patterns in DAMON.  Each of next
two patches (8th and 9th) respectively adds a tracepoint for other tracepoints
supporting tracers such as perf, and a debugfs interface for privileged people
and/or programs in user space.  10th patch makes the debugfs interface further
support pidfd.  And, the 11th patch implements an user space tool to provide a
minimal reference to the debugfs interface and for high level use/tests of the
DAMON.

Three patches for maintainability follows.  The 12th patch adds documentations
for both the user space and the kernel space.  The 13th patch provides unit
tests (based on the kunit) while the 14th patch adds user space tests (based on
the kselftest).

Finally, the last patch (15th) updates the MAINTAINERS file.

Patch History
=============

Changes from v19
(https://lore.kernel.org/linux-mm/20200804091416.31039-1-sjpark@amazon.com/)
- Place 'CREATE_TRACE_POINTS' after '#include' statements (Steven Rostedt)
- Support large record file (Alkaid)
- Place 'put_pid()' of virtual monitoring targets in 'cleanup' callback
- Avoid conflict between concurrent DAMON users
- Update evaluation result document

Changes from v18
(https://lore.kernel.org/linux-mm/20200713084144.4430-1-sjpark@amazon.com/)
- Drop loadable module support (Mike Rapoport)
- Select PAGE_EXTENSION if !64BIT for 'set_page_young()'
- Take care of the MMU notification subscribers (Shakeel Butt)
- Substitute 'struct damon_task' with 'struct damon_target' for better abstract
- Use 'struct pid' instead of 'pid_t' as the target (Shakeel Butt)
- Support pidfd from the debugfs interface (Shakeel Butt)
- Fix typos (Greg Thelen)
- Properly isolate DAMON from other pmd/pte Accessed bit users (Greg Thelen)
- Rebase on v5.8

Changes from v17
(https://lore.kernel.org/linux-mm/20200706115322.29598-1-sjpark@amazon.com/)
- Reorganize the doc and remove png blobs (Mike Rapoport)
- Wordsmith mechnisms doc and commit messages
- tools/wss: Set default working set access frequency threshold
- Avoid race in damon deamon start

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

Please refer to the v13 patchset to get older history.
