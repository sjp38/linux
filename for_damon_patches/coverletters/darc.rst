Introduce DAMON-based Proactive Reclamation

NOTE: This is only an RFC for future features of DAMON patchset[1], which is
not merged in the mainline yet.  The aim of this RFC is to show how DAMON would
be evolved once it is merged in.  So, if you have some interest here, please
consider reviewing the DAMON patchset, either.

[1] https://lore.kernel.org/linux-mm/20210520075629.4332-1-sj38.park@gmail.com/

Introduction
============

In short, this patchset 1) improves the engine for general data access
pattern-oriented memory management, and 2) implements a static kernel module
for lightweight proactive reclamation using the engine.

Proactive Reclamation
---------------------

On general memory over-committed systems, proactively reclaiming cold pages
helps saving memory and reducing latency spikes that incurred by the direct
reclaim of the process or CPU consumption of kswapd, while incurring only
minimal performance degradation[2].

For example, let's consider Free Pages Reporting[9] based memory over-commit
virtualization systems.  In the systems, the guest VMs reports their free
memory to host, and the host reallocates the reported memory to other guests.
As a result, the memory of the systems are fully utilized.  However, the guests
could be not so memory-frugal, mainly because some kernel subsystems and
user-space applications are designed to use as much memory as available.  Then,
guests could report only small amount of free memory to host, hence results in
poor memory utilization.  Running the proactive reclamation in guests could
mitigate this problem.

Google has implemented the idea and using it in their data center.  They
further proposed upstreaming it in LSFMM'19, and "the general consensus was
that, while this sort of proactive reclaim would be useful for a number of
users, the cost of this particular solution was too high to consider merging it
upstream"[3].  The cost mostly comes from the coldness tracking.  Roughly
speaking, the implementation periodically scans the 'Accessed' bit of each
page.  For the reason, the overhead linearly increases as the size of the
memory and the scanning frequency grows.  As a result, Google is known to
dedicating one CPU for the work.  That's a reasonable option to at least
Google, but it wouldn't be so to someone.

DAMON and DAMOS: An engine for data access pattern-oriented memory management
-----------------------------------------------------------------------------

DAMON[4] is a framework for general data access monitoring.  When it's adaptive
monitoring overhead control is used, it incurs minimized monitoring overhead.
It's not only small, but also upper-bounded, regardless of the monitoring
target memory size.  Clients can set the upper-limit as they want.  While
monitoring 70 GB memory of a production system every 5 milliseconds, it
consumes less than 1% single CPU time.  For this, it could sacrify some of the
quality of the monitoring results.  Nevertheless, the lower-bound of the
quality is configurable, and it has a best-effort algorithm for maximizing the
quality.  Our test results[5] show the quality is practical enough.  From the
production system monitoring, we were able to find a 4 KB region in the 70 GB
memory that shows highest access frequency.  For someone who still couldn't be
convinced, DAMON also supports a page-granularity monitoring[6], though it
makes the overhead much higher and proportional to the memory size again.

We normally don't monitor the data access pattern just for fun but to use it
for something like memory management.  Proactive reclamation is one such usage.
For such general cases, DAMON provides a feature called DAMon-based Operation
Schemes (DAMOS)[7], which makes DAMON an engine for general data access pattern
oriented memory management.  Using this, clients can ask DAMON to find memory
regions of specific data access pattern and apply some memory management action
(e.g., page out, move to head of the LRU list, use huge page, ...).  We call
the request 'scheme'.

Proactive Reclamation on top of DAMON/DAMOS
-------------------------------------------

Therefore, by using DAMON for the cold pages detection, the proactive
reclamation's monitoring overhead issue could be solved.  If someone like
Google is ok to dedicate CPUs for the monitoring and wants page-granularity
monitoring, they could configure DAMON so.

Actually, we previously implemented a version of proactive reclamation using
DAMOS and shared its evaluation results[5], which show noticeable achievements.
Nevertheless, it was only for a proof-of-concept.  It supports only virtual
address spaces of processes, and require additional tuning efforts for given
workloads and the hardware.  For the tuning, we recently introduced a simple
auto-tuning user space tool[8].  Google is also known to using a ML-based
similar approach for their data center[2].  But, making it just works in the
kernel would be more convenient for more general users.

To this end, this patchset improves DAMOS to be more proper for such production
usages, and implements another version of the proactive reclamation, namely
DAMON_RECLAIM, on top of it.

DAMOS Improvements: Speed Limit, Prioritization, and Watermarks
---------------------------------------------------------------

First of all, the current version of DAMOS supports only virtual address
spaces.  This patchset makes it supports the physical address space for the
page out action.

One major problem of the current version of DAMOS is the lack of the
aggressiveness control.  This means it could incur arbitrarily high overhead.
For example, if huge memory regions of the specified data access pattern are
found, applying the requested action to the regions could incur significant
overhead.  It could be controlled by modifying the target data access pattern
and some auto-tuning approaches[2,8] are available.  But, someone would unable
to use such tools, and some others would prefer things just works with only
intuitive tuning or default values.

For this, this patchset implements a safeguard, namely speed limit.  Using
this, the clients can specify up to how much amount of memory the action is
allowed to be applied within specific time duration.  A followup question is,
to which memory regions should the action applied within the limit?  We
implement a simple regions prioritization mechanism for each action and make
DAMOS to apply the action to high priority regions first.  It also allows users
tune the prioritization by giving different weights to region's size, access
frequency, and age.  This means we can use not only LRU but also LFU or some
fancy algorithms like CAR[10] with lightweight overhead.

Another problem of the current version of DAMOS is that it should manually
turned on and off, by clients.  Though DAMON is lightweight, someone would want
to remove even the overhead when it is unnecessary.  For such cases, this
patchset implements a watermarks-based automatic activation feature.  It allows
the clients configure the metric of their interest and three watermarks of the
metric.  If the metric is higher than the high watermark or lower than the low
watermark, the scheme is deactivated.  If the metric is lower than the mid
watermark but higher than the low watermark, the scheme is activated.

DAMON-based Reclaim
-------------------

Using the improved DAMOS, this patchset implements a static kernel module
called 'damon_reclaim'.  It finds memory regions that didn't accessed for
specific time duration and page out.  To avoid it consuming too much CPU for
the paging out operations, the speed limit can be configured.  Under the speed
limit, it pages out memory regions that didn't accessed longer time first.
Also, to remove the monitoring overhead under peaceful situation, and to fall
back to the LRU-list based page granularity reclamation when it doesn't make
progress, the three watermarks based activation mechanism is used, with the
free memory ratio as the watermark metric.

For convenient configuration, it utilizes module parameters.  Using these,
sysadmins can enable/disable it and tune the coldness identification time
threshold, the speed limit, and the three watermarks for their system.  They
can use the kernel command line for boot time tuning, or the sysfs
('/sys/modules/damon_reclaimparameters/') for runtime tuning.

Evaluation
==========

In short, DAMON_RECLAIM on v5.12 Linux kernel with 10GB/s speed limit achieves
32% memory saving with only 1.91% runtime overhead.  For this, DAMON_RECLAIM
consumed only 5.72% of single CPU time.  Among the CPU consumption, only about
1.448% of single CPU time is expected to be used for the access pattern
monitoring.

Setup
-----

We evaluate DAMON_RECLAIM to show how each of the DAMOS improvements make
effect.  For this, we measure entire system memory footprint and runtime of 24
realistic workloads in PARSEC3 and SPLASH-2X benchmark suites on my QEMU/KVM
based virtual machine.  The virtual machine runs on an i3.metal AWS instance
and has 130GiB memory.  It utilizes a 4 GiB zram swap device.  We do the
measurement 5 times and use averages.  We also measure the CPU consumption of
DAMON_RECLAIM.

Detailed Results
----------------

DAMON_RECLAIM without speed limit achieves 46.50% memory saving, but incur
4.79% runtime slowdown to the workloads.  For this, DAMON_RECLAIM consumes
about 11% single CPU time.

Applying 1GB/s, 5GB/s, and 10GB/s speed limits without the regions
prioritization incur slowdown 1.76%, 2.26%, and 2.88%, respectively.
DAMON_RECLAIM's CPU utilization also shows similar trend.  The variants make it
to utilize 1.98%, 4.34%, and 6.27% of single CPU time.  That is, applying
smaller speed limit reduces more performance degradation.  Nevertheless, it
also reduces the memory saving because it becomes less aggressive.  In detail,
the three variants show 11.66%, 26.73%, and 36.42% memory saving, respectively.

Applying regions prioritization (page out regions that not accessed for more
time first within the speed limit) further reduces the performance degradation.
Runtime slowdowns has been 1.76% -> 0.91% (1GB/s), 2.26% -> 1.93% (5GB/s), and
2.88% -> 1.91% (10GB/s).  Interestingly, prioritization also reduced memory
saving a little bit.  I think that's because already paged out region is
prioritized again.  All detailed numbers are below.

    speed_limit  prioritization  memory_saving  cpu_util  slowdown
    N            N               46.50%         11.00%    4.79%
    1GB/s        N               11.66%         1.98%     1.76%
    5GB/s        N               26.73%         4.34%     2.26%
    10GB/s       N               36.42%         6.27%     2.88%
    1GB/s        Y               3.50%          1.55%     0.91%
    5GB/s        Y               19.11%         3.45%     1.93%
    10GB/s       Y               32.07%         5.39%     1.91%

Baseline and Complete Git Tree
==============================

The patches are based on the latest -mm tree plus DAMON patchset[1], DAMOS
patchset[7], and physical address space support patchset[6].  You can also
clone the complete git tree from:

    $ git clone git://github.com/sjp38/linux -b damon_reclaim/rfc/v2

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon_reclaim/rfc/v2

Development Trees
-----------------

There are a couple of trees for entire DAMON patchset series and
features for future release.

- For latest release: https://github.com/sjp38/linux/tree/damon/master
- For next release: https://github.com/sjp38/linux/tree/damon/next

Long-term Support Trees
-----------------------

For people who want to test DAMON patchset series but using only LTS kernels,
there are another couple of trees based on two latest LTS kernels respectively
and containing the 'damon/master' backports.

- For v5.4.y: https://github.com/sjp38/linux/tree/damon/for-v5.4.y
- For v5.10.y: https://github.com/sjp38/linux/tree/damon/for-v5.10.y

Sequence Of Patches
===================

The first patch makes DAMOS users to be able to describe pages to be paged out
via physical address.  Following four patches (patches 2-5) implement the speed
limit.  Next four patches (patches 6-9) implement the memory regions
prioritization within the limit.  Then, three patches (patches 10-12)
implementing the watermarks-based schemes activation follow.  Finally, the 13th
patch implements the DAMON-based reclamation on top of DAMOS.


[1] https://lore.kernel.org/linux-mm/20210520075629.4332-1-sj38.park@gmail.com/
[2] https://research.google/pubs/pub48551/
[3] https://lwn.net/Articles/787611/
[4] https://damonitor.github.io
[5] https://damonitor.github.io/doc/html/latest/vm/damon/eval.html
[6] https://lore.kernel.org/linux-mm/20201216094221.11898-1-sjpark@amazon.com/
[7] https://lore.kernel.org/linux-mm/20201216084404.23183-1-sjpark@amazon.com/
[8] https://github.com/awslabs/damoos
[9] https://www.kernel.org/doc/html/latest/vm/free_page_reporting.html
[10] https://www.usenix.org/conference/fast-04/car-clock-adaptive-replacement
