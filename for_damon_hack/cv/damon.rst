Subject: Introduce Data Access MONitor (DAMON)

Changes from Previous Version (v33)
===================================

Compared to the v33
(https://lore.kernel.org/linux-mm/20210713123356.6924-1-sj38.park@gmail.com/),
this version contains below minor changes.

- Rebase on latest -mm tree (v5.14-rc1-mmots-2021-07-15-18-47)
- Remove unnecessary asterisks from the MAINTAINERS file update (Joe Perches)

Now all patches of this patchset has at least one 'Reviewed-by:' or 'Acked-by:'
tags.  Andrew, could you please consider merging this into the -mm tree?

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

Though DAMON is for kernel subsystems, it can be easily exposed to the user
space by writing a DAMON-wrapper kernel subsystem.  Then, user space users who
have some special workloads will be able to write personalized tools or
applications for deeper understanding and specialized optimizations of their
systems.

DAMON is also merged in two public Amazon Linux kernel trees that based on
v5.4.y[1] and v5.10.y[2].

[1] https://github.com/amazonlinux/linux/tree/amazon-5.4.y/master/mm/damon
[2] https://github.com/amazonlinux/linux/tree/amazon-5.10.y/master/mm/damon

Long-term Plan
--------------

DAMON is a part of a project called Data Access-aware Operating System (DAOS).
As the name implies, I want to improve the performance and efficiency of
systems using fine-grained data access patterns.  The optimizations are for
both kernel and user spaces.  I will therefore modify or create kernel
subsystems, export some of those to user space and implement user space library
/ tools.  Below shows the layers and components for the project.

    ---------------------------------------------------------------------------
    Primitives:     PTE Accessed bit, PG_idle, rmap, (Intel CMT), ...
    Framework:      DAMON
    Features:       DAMOS, virtual addr, physical addr, ...
    Applications:   DAMON-debugfs, (DARC), ...
    ^^^^^^^^^^^^^^^^^^^^^^^    KERNEL SPACE    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    Raw Interface:  debugfs, (sysfs), (damonfs), tracepoints, (sys_damon), ...

    vvvvvvvvvvvvvvvvvvvvvvv    USER SPACE      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    Library:        (libdamon), ...
    Tools:          DAMO, (perf), ...
    ---------------------------------------------------------------------------

The components in parentheses or marked as '...' are not implemented yet but in
the future plan.  IOW, those are the TODO tasks of DAOS project.  For more
detail, please refer to the plans:
https://lore.kernel.org/linux-mm/20201202082731.24828-1-sjpark@amazon.com/

Evaluations
===========

We evaluated DAMON's overhead, monitoring quality and usefulness using 24
realistic workloads on my QEMU/KVM based virtual machine running a kernel that
v24 DAMON patchset is applied.

DAMON is lightweight.  It increases system memory usage by 0.39% and slows
target workloads down by 1.16%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, namely 'ethp', removes
76.15% of THP memory overheads while preserving 51.25% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 93.38% of residential sets and 23.63% of system memory footprint while
incurring only 1.22% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimental THP optimization and proactive reclamation are not
for production but only for proof of concepts.

Please refer to the official document[1] or "Documentation/admin-guide/mm: Add
a document for DAMON" patch in this patchset for detailed evaluation setup and
results.

[1] https://damonitor.github.io/doc/html/latest-damon/admin-guide/mm/damon/eval.html

Real-world User Story
=====================

In summary, DAMON has used on production systems and proved its usefulness.

DAMON as a profiler
-------------------

We analyzed characteristics of a large scale production systems of our
customers using DAMON.  The systems utilize 70GB DRAM and 36 CPUs.  From this,
we were able to find interesting things below.

There were obviously different access pattern under idle workload and active
workload.  Under the idle workload, it accessed large memory regions with low
frequency, while the active workload accessed small memory regions with high
freuqnecy.

DAMON found a 7GB memory region that showing obviously high access frequency
under the active workload.  We believe this is the performance-effective
working set and need to be protected.

There was a 4KB memory region that showing highest access frequency under not
only active but also idle workloads.  We think this must be a hottest code
section like thing that should never be paged out.

For this analysis, DAMON used only 0.3-1% of single CPU time.  Because we used
recording-based analysis, it consumed about 3-12 MB of disk space per 20
minutes.  This is only small amount of disk space, but we can further reduce
the disk usage by using non-recording-based DAMON features.  I'd like to argue
that only DAMON can do such detailed analysis (finding 4KB highest region in
70GB memory) with the light overhead.

DAMON as a system optimization tool
-----------------------------------

We also found below potential performance problems on the systems and made
DAMON-based solutions.

The system doesn't want to make the workload suffer from the page reclamation
and thus it utilizes enough DRAM but no swap device.  However, we found the
system is actively reclaiming file-backed pages, because the system has
intensive file IO.  The file IO turned out to be not performance critical for
the workload, but the customer wanted to ensure performance critical
file-backed pages like code section to not mistakenly be evicted.

Using direct IO should or `mlock()` would be a straightforward solution, but
modifying the user space code is not easy for the customer.  Alternatively, we
could use DAMON-based operation scheme[1].  By using it, we can ask DAMON to
track access frequency of each region and make
'process_madvise(MADV_WILLNEED)[2]' call for regions having specific size and
access frequency for a time interval.

We also found the system is having high number of TLB misses.  We tried
'always' THP enabled policy and it greatly reduced TLB misses, but the page
reclamation also been more frequent due to the THP internal fragmentation
caused memory bloat.  We could try another DAMON-based operation scheme that
applies 'MADV_HUGEPAGE' to memory regions having >=2MB size and high access
frequency, while applying 'MADV_NOHUGEPAGE' to regions having <2MB size and low
access frequency.

We do not own the systems so we only reported the analysis results and possible
optimization solutions to the customers.  The customers satisfied about the
analysis results and promised to try the optimization guides.

[1] https://lore.kernel.org/linux-mm/20201006123931.5847-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-api/20200622192900.22757-4-minchan@kernel.org/

Comparison with Idle Page Tracking
==================================

Idle Page Tracking allows users to set and read idleness of pages using a
bitmap file which represents each page with each bit of the file.  One
recommended usage of it is working set size detection.  Users can do that by

    1. find PFN of each page for workloads in interest,
    2. set all the pages as idle by doing writes to the bitmap file,
    3. wait until the workload accesses its working set, and
    4. read the idleness of the pages again and count pages became not idle.

NOTE: While Idle Page Tracking is for user space users, DAMON is primarily
designed for kernel subsystems though it can easily exposed to the user space.
Hence, this section only assumes such user space use of DAMON.

For what use cases Idle Page Tracking would be better?
------------------------------------------------------

1. Flexible usecases other than hotness monitoring.

Because Idle Page Tracking allows users to control the primitive (Page
idleness) by themselves, Idle Page Tracking users can do anything they want.
Meanwhile, DAMON is primarily designed to monitor the hotness of each memory
region.  For this, DAMON asks users to provide sampling interval and
aggregation interval.  For the reason, there could be some use case that using
Idle Page Tracking is simpler.

2. Physical memory monitoring.

Idle Page Tracking receives PFN range as input, so natively supports physical
memory monitoring.

DAMON is designed to be extensible for multiple address spaces and use cases by
implementing and using primitives for the given use case.  Therefore, by
theory, DAMON has no limitation in the type of target address space as long as
primitives for the given address space exists.  However, the default primitives
introduced by this patchset supports only virtual address spaces.

Therefore, for physical memory monitoring, you should implement your own
primitives and use it, or simply use Idle Page Tracking.

Nonetheless, RFC patchsets[1] for the physical memory address space primitives
is already available.  It also supports user memory same to Idle Page Tracking.

[1] https://lore.kernel.org/linux-mm/20200831104730.28970-1-sjpark@amazon.com/

For what use cases DAMON is better?
-----------------------------------

1. Hotness Monitoring.

Idle Page Tracking let users know only if a page frame is accessed or not.  For
hotness check, the user should write more code and use more memory.  DAMON do
that by itself.

2. Low Monitoring Overhead

DAMON receives user's monitoring request with one step and then provide the
results.  So, roughly speaking, DAMON require only O(1) user/kernel context
switches.

In case of Idle Page Tracking, however, because the interface receives
contiguous page frames, the number of user/kernel context switches increases as
the monitoring target becomes complex and huge.  As a result, the context
switch overhead could be not negligible.

Moreover, DAMON is born to handle with the monitoring overhead.  Because the
core mechanism is pure logical, Idle Page Tracking users might be able to
implement the mechanism on thier own, but it would be time consuming and the
user/kernel context switching will still more frequent than that of DAMON.
Also, the kernel subsystems cannot use the logic in this case.

3. Page granularity working set size detection.

Until v22 of this patchset, this was categorized as the thing Idle Page
Tracking could do better, because DAMON basically maintains additional metadata
for each of the monitoring target regions.  So, in the page granularity working
set size detection use case, DAMON would incur (number of monitoring target
pages * size of metadata) memory overhead.  Size of the single metadata item is
about 54 bytes, so assuming 4KB pages, about 1.3% of monitoring target pages
will be additionally used.

All essential metadata for Idle Page Tracking are embedded in 'struct page' and
page table entries.  Therefore, in this use case, only one counter variable for
working set size accounting is required if Idle Page Tracking is used.

There are more details to consider, but roughly speaking, this is true in most
cases.

However, the situation changed from v23.  Now DAMON supports arbitrary types of
monitoring targets, which don't use the metadata.  Using that, DAMON can do the
working set size detection with no additional space overhead but less
user-kernel context switch.  A first draft for the implementation of monitoring
primitives for this usage is available in a DAMON development tree[1].  An RFC
patchset for it based on this patchset will also be available soon.

From v24, the arbitrary type support is dropped from this patchset because this
patchset doesn't introduce real use of the type.  You can still get it from the
DAMON development tree[2], though.

[1] https://github.com/sjp38/linux/tree/damon/pgidle_hack
[2] https://github.com/sjp38/linux/tree/damon/master

4. More future usecases

While Idle Page Tracking has tight coupling with base primitives (PG_Idle and
page table Accessed bits), DAMON is designed to be extensible for many use
cases and address spaces.  If you need some special address type or want to use
special h/w access check primitives, you can write your own primitives for that
and configure DAMON to use those.  Therefore, if your use case could be changed
a lot in future, using DAMON could be better.

Can I use both Idle Page Tracking and DAMON?
--------------------------------------------

Yes, though using them concurrently for overlapping memory regions could result
in interference to each other.  Nevertheless, such use case would be rare or
makes no sense at all.  Even in the case, the noise would bot be really
significant.  So, you can choose whatever you want depending on the
characteristics of your use cases.

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

The patches are based on the latest -mm tree, specifically
v5.14-rc1-mmots-2021-07-15-18-47 of https://github.com/hnaz/linux-mm.  You can
also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damon/patches/v34

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v34

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

Amazon Linux Kernel Trees
-------------------------

DAMON is also merged in two public Amazon Linux kernel trees that based on
v5.4.y[1] and v5.10.y[2].

[1] https://github.com/amazonlinux/linux/tree/amazon-5.4.y/master/mm/damon
[2] https://github.com/amazonlinux/linux/tree/amazon-5.10.y/master/mm/damon

Git Tree for Diff of Patches
============================

For easy review of diff between different versions of each patch, I prepared a
git tree containing all versions of the DAMON patchset series:
https://github.com/sjp38/damon-patches

You can clone it and use 'diff' for easy review of changes between different
versions of the patchset.  For example:

    $ git clone https://github.com/sjp38/damon-patches && cd damon-patches
    $ diff -u damon/v33 damon/v34

Sequence Of Patches
===================

First three patches implement the core logics of DAMON.  The 1st patch
introduces basic sampling based hotness monitoring for arbitrary types of
targets.  Following two patches implement the core mechanisms for control of
overhead and accuracy, namely regions based sampling (patch 2) and adaptive
regions adjustment (patch 3).

Now the essential parts of DAMON is complete, but it cannot work unless someone
provides monitoring primitives for a specific use case.  The following two
patches make it just work for virtual address spaces monitoring.  The 4th patch
makes 'PG_idle' can be used by DAMON and the 5th patch implements the virtual
memory address space specific monitoring primitives using page table Accessed
bits and the 'PG_idle' page flag.

Now DAMON just works for virtual address space monitoring via the kernel space
api.  To let the user space users can use DAMON, following four patches add
interfaces for them.  The 6th patch adds a tracepoint for monitoring results.
The 7th patch implements a DAMON application kernel module, namely damon-dbgfs,
that simply wraps DAMON and exposes DAMON interface to the user space via the
debugfs interface.  The 8th patch further exports pid of monitoring thread
(kdamond) to user space for easier cpu usage accounting, and the 9th patch
makes the debugfs interface to support multiple contexts.

Three patches for maintainability follows.  The 10th patch adds documentations
for both the user space and the kernel space.  The 11th patch provides unit
tests (based on the kunit) while the 12th patch adds user space tests (based on
the kselftest).

Finally, the last patch (13th) updates the MAINTAINERS file.

Patch History
=============

Changes from v33
(https://lore.kernel.org/linux-mm/20210713123356.6924-1-sj38.park@gmail.com/),
- Rebase on latest -mm tree (v5.14-rc1-mmots-2021-07-15-18-47)
- Remove unnecessary asterisks from the MAINTAINERS file update (Joe Perches)

Changes from v32
(https://lore.kernel.org/linux-mm/20210628133355.18576-1-sj38.park@gmail.com/)
- Rebase on latest mainline (7d0fc5c62385)
- Collect 'Acked-by:' tags from Shakeel Butt

Chages from v31
(https://lore.kernel.org/linux-mm/20210621083108.17589-1-sj38.park@gmail.com/)
- Rebase on latest -mm tree (v5.13-rc7-mmots-2021-06-24-20-54)
- Add 'Acked-by:' tags from Shakeel Butt
- Use 'kthread_run()' (Shakeel Butt)
- Change default 'update_interval' to 60 seconds (Shakeel Butt)
- Utilize 'nr_regions' field in each 'damon_target' object (Shakeel Butt)
- Remove unused parameters in some functions (Shakeel Butt)
- Use variable name 'ctx' for 'damon_ctx' (Shakeel Butt)
- Make 'dbgfs' to completely manage pid reference counting (Shakeel Butt)
- Remove '.owner' of debugfs files (Shakeel Butt)

Changes from v30
(https://lore.kernel.org/linux-mm/20210616073119.16758-1-sj38.park@gmail.com/)
- Rebase on latest -mm tree (v5.13-rc6-mmots-2021-06-16-22-17)
- selftest: Fix wrong file content comparison (Markus Boehme)
- Collect 'Reviewed-by:' tags from Markus

Changes from v29
(https://lore.kernel.org/linux-mm/20210520075629.4332-1-sj38.park@gmail.com/)
- Rebase on latest -mm tree (v5.13-rc6-mmots-2021-06-15-20-28)
- Remove unnecessary documents
- Wordsmith commit message for PAGE_IDLE separation (Amit Shah)
- selftests: Fix shellcheck warnings and cleanup (Maximilian Heyne)
- Wordsmith the document (Markus Boehme)
- Fix a typo in comments (Fernand Sieber)
- Collect 'Reviewed-by:' tags from "Fernand Sieber <sieberf@amazon.com>"

Please refer to the v29 patchset to get older history.
