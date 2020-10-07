Subject: Introduce Data Access MONitor (DAMON)

Changes from Previous Version (v20)
===================================

- s/snprintf()/scnprintf() (Marco Elver)
- Support multiple contexts for user space users (Shakeel Butt)
- Export pid of monitoring thread to user space (Shakeel Butt)
- Let coexistable with Idle Page Tracking
- Place three parts of DAMON (core, primitives, and dbgfs) in different files

Introduction
============

DAMON is a data access monitoring framework for the Linux kernel.  The core
mechanisms of DAMON called 'region based sampling' and 'adaptive regions
adjustment' (refer to 'mechanisms.rst' in the 11th patch of this patchset for
the detail) make it

 - accurate (The monitored information is useful for DRAM level memory
   management. It might not appropriate for Cache-level accuracy, though.),
 - light-weight (The monitoring overhead is low enough to be applied online
   while making no impact on the performance of the target workloads.), and
 - scalable (the upper-bound of the instrumentation overhead is controllable
   regardless of the size of target workloads.).

Using this framework, therefore, several memory management mechanisms such as
reclamation and THP can be optimized to aware real data access patterns.
Experimental access pattern aware memory management optimization works that
incurring high instrumentation overhead will be able to have another try.

Though DAMON is for kernel subsystems, writing a DAMON-wrapper kernel subsystem
exposing DAMON to user space is straightforward, due to DAMON's simple
interface.  Then, user space users who have some special workloads will be able
to write personalized tools or applications for deeper understanding and
specialized optimizations of their systems.

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

Comparison with Idle Page Tracking
==================================

Idle Page Tracking allow users to set and read idleness of pages using a
bitmap file which represents each page with each bit of the file.  One
recommended usage of it is working set size detection.  Users can do that by

    1. find PFN of all pages for workloads in interest,
    2. set all the pages as idle by doing writes to the bitmap file,
    3. wait until the workload accesses its working set, and
    4. read the idleness of the pages again and count pages became not idle.

NOTE: DAMON is primarily for kernel code, but the interface can easily be
exposed to user space.  This section only assumes such user space use of DAMON.

For what use cases Idle Page Tracking would be better?
------------------------------------------------------

1. Page granularity working set size detection.

DAMON maintains additional metadata for each of the monitoring target regions.
So, in this page granularity monitoring use case, DAMON would incur (number of
monitoring target pages * sizeof metadata) memory overhead.  Size of the single
metadata item is about 54 bytes, so assuming 4KB pages, about 1.3% of
monitoring target pages will be additionally used.

All essential metadata for Idle Page Tracking are embedded in 'struct page' and
page table entries.  Therefore, in this use case, only one counter variable for
working set size account is additionally used.

There are more detail to consider, but roughly speaking, this is true in most
cases.

2. Physical memory monitoring.

Idle Page Tracking receives PFN range as input, so natively supports physical
memory monitoring.

DAMON is instead designed to be extensible for multiple address spaces and use
cases by implementing and using primitives for the given use case.  Therefore,
by theory, DAMON has no limitation in the type of target address space as long
as primitives for the given address space exists.  However, this patchset
provides only one implementation of primitives for virtual address spaces.

Therefore, for physical memory monitoring, you should implement your own
primitives and use it, or simply use Idle Page Tracking.

Nonetheless, RFC patchsets[1] for the physical memory address space primitives
is already available.  It also supports user memory same to Idle Page Tracking.

[1] https://lore.kernel.org/linux-mm/20200831104730.28970-1-sjpark@amazon.com/

For what use cases DAMON is better?
-----------------------------------

1. Hotness Monitoring.

Idle Page Tracking let users know only if a page frame is accessed or not.  For
hotness check, the user should write more code.  DAMON do that by itself.

2. Low Monitoring Overhead

DAMON receives user's monitoring request with one step and then provide the
results.  So, roughly speaking, DAMON require only O(1) user/kernel context
switches.

In case of Idle Page Tracking, however, because it works with contiguous page
frames, the number of user/kernel context switches increases as the monitoring
target becomes complex and huge.  As a result, the context switch overhead
could be not negligible.

Moreover, DAMON is born to handle with the monitoring overhead.  Because the
core mechanism is pure logical, Idle Page Tracking users might be able to
implement the mechanism on thier own on the user space, but it would be time
consuming.  Also, the user/kernel context switching costs would not
disappeared.

3. More future usecases

While Idle Page Tracking has tight coupling with base primitive (PG_Idle),
DAMON is designed to be easily expandsble for many use cases and address
spaces.  If you need some special address type or want to use special h/w
access check primitives, you can write your own primitives for that and
configure DAMON with it.  Therefore, if your use case could be changed a lot in
future, using DAMON could be better.

Can I use both Idle Page Tracking and DAMON?
--------------------------------------------

Because DAMON could interfere Idle Page Tracking, v20 of this patchset made
those exclusive in the kernel config.  However, this patchset solves the
problem.  So, yes, you can use both Idle Page Tracking and DAMON on single
system as you want.

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

    $ git clone git://github.com/sjp38/linux -b damon/patches/v21

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v21

There are a couple of trees for entire DAMON patchset series.  It includes
future features.  The first one[1] contains the changes for latest release,
while the other one[2] contains the changes for next release.

[1] https://github.com/sjp38/linux/tree/damon/master
[2] https://github.com/sjp38/linux/tree/damon/next

Sequence Of Patches
===================

First four patches implement the target address space independent core logics
of DAMON and it's programming interface.  The 1st patch introduces DAMON data
structures and functions for manipulation of the structures.
Following three patches (2nd to 4th) implements the core mechanisms of DAMON,
namely regions based sampling (patch 2), adaptive regions adjustment (patch 3),
and dynamic memory mapping change adoption (patch 4).

Now the essential parts of DAMON is complete, but it cannot work unless someone
provide primitives for specific use case.  The following two patches make it
just work for virtual address spaces monitoring.  The 5th patch makes 'PG_idle'
could be used by DAMON and the 6th patch implements the virtual memory address
space specific low primitives using page table Accessed bits and the 'PG_idle'
page flag.  As use of 'PG_idle' could it interfere Idle Page Tracking, the
primitives are configured to be exclusive with Idle Page Tracking.

As there are some cases Idle Page Tracking could do better, next two patches
make DAMON coexistable with Idle Page Tracking.  The 7th patch introduces a
synchronization primitives for concurrent PG_Idle users, and the 8th patch
makes the primitives for DAMON to synchronize with Idle Page Tracking using
it.

Now DAMON just works for virtual address space monitoring via the kernel space
api.  Following six patches adds interfaces for the users in the user space.
The 9th patch adds a tracepoint for other tracepoints supporting tracers.  The
10th patch implements  a DAMON application kernel module, namely damon-dbgfs,
that exposes DAMON interface to the user space via the debugfs interface.  To
let user space get the monitoring results more easily, the 11th patch implement
a simple recording feature in 'damon-dbgfs'.  The 12nd patch further export pid
of monitoring thread (kdamond) to user space for easier cpu usage account, and
the 13rd patch makes the debugfs interface to support multiple contexts.  Then,
the 14th patch implements an user space tool to provide a minimal reference to
the debugfs interface and for high level use/tests of the DAMON.

Three patches for maintainability follows.  The 15th patch adds documentations
for both the user space and the kernel space.  The 16th patch provides unit
tests (based on the kunit) while the 17th patch adds user space tests (based on
the kselftest).

Finally, the last patch (18th) updates the MAINTAINERS file.

Patch History
=============

Changes from v20
(https://lore.kernel.org/linux-mm/20200817105137.19296-1-sjpark@amazon.com/)
- s/snprintf()/scnprintf() (Marco Elver)
- Support multiple contexts for user space users (Shakeel Butt)
- Export pid of monitoring thread to user space (Shakeel Butt)
- Let coexistable with Idle Page Tracking
- Place three parts of DAMON (core, primitives, and dbgfs) in different files

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

Please refer to the v15 patchset to get older history.
