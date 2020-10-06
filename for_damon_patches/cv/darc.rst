Introduce DAMON-based Proactive Reclamation

NOTE: This is only an RFC for future features of DAMON patchset[1], which is
not merged in the mainline yet.  The aim of this RFC is to show how DAMON would
be evolved once it is merged in.  So, if you have some interest here, please
consider reviewing the DAMON patchset, either.

[1] https://lore.kernel.org/linux-mm/20210716081449.22187-1-sj38.park@gmail.com/

Changes from Previous Version (RFC v2)
======================================

Compared to the RFC v2
(https://lore.kernel.org/linux-mm/20210608115254.11930-1-sj38.park@gmail.com/),
this version contains below changes.

- Rebase on latest -mm tree (v5.14-rc1-mmots-2021-07-15-18-47)
- Implement a time quota (limits the time for trying reclamation of cold pages)
- Make reclamation restarts from exactly the point it stopped due to the limit

Introduction
============

In short, this patchset 1) makes the engine for general data access
pattern-oriented memory management be useful for production environments, and
2) implements a static kernel module for lightweight proactive reclamation
using the engine.

Proactive Reclamation
---------------------

On general memory over-committed systems, proactively reclaiming cold pages
helps saving memory and reducing latency spikes that incurred by the direct
reclaim or the CPU consumption of kswapd, while incurring only minimal
performance degradation[2].

Particularly, a Free Pages Reporting[9] based memory over-commit virtualization
system would be one of such use cases.  In the system, the guest VMs reports
their free memory to host, and the host reallocates the reported memory to
other guests.  As a result, the system's memory can be fully utilized.
However, the guests could be not so memory-frugal, mainly because some kernel
subsystems and user-space applications are designed to use as much memory as
available.  Then, guests would report only small amount of free memory to host,
and results in poor memory utilization.  Running the proactive reclamation in
guests could help mitigating this problem.

Google has implemented the general idea and using it in their data center.
They further proposed upstreaming it in LSFMM'19, and "the general consensus
was that, while this sort of proactive reclaim would be useful for a number of
users, the cost of this particular solution was too high to consider merging it
upstream"[3].  The cost mainly comes from the coldness tracking.  Roughly
speaking, the implementation periodically scans the 'Accessed' bit of each
page.  For the reason, the overhead linearly increases as the size of the
memory and the scanning frequency grows.  As a result, Google is known to
dedicating one CPU for the work.  That's a reasonable option to someone like
Google, but it wouldn't be so to some others.

DAMON and DAMOS: An engine for data access pattern-oriented memory management
-----------------------------------------------------------------------------

DAMON[4] is a framework for general data access monitoring.  Its adaptive
monitoring overhead control feature minimizes its monitoring overhead.  It also
let the upper-bounded of the overhead be configurable by clients, regardless of
the size of the monitoring target memory.  While monitoring 70 GB memory of a
production system every 5 milliseconds, it consumes less than 1% single CPU
time.  For this, it could sacrify some of the quality of the monitoring
results.  Nevertheless, the lower-bound of the quality is configurable, and it
uses a best-effort algorithm for better quality.  Our test results[5] show the
quality is practical enough.  From the production system monitoring, we were
able to find a 4 KB region in the 70 GB memory that shows highest access
frequency.  For people having different requirements, the features can
selectively turned off, and DAMON supports the page-granularity monitoring[6],
though it makes the overhead higher and proportional to the memory size again.

We normally don't monitor the data access pattern just for fun but to improve
something like memory management.  Proactive reclamation is one such usage.
For such general cases, DAMON provides a feature called DAMon-based Operation
Schemes (DAMOS)[7].  It makes DAMON an engine for general data access pattern
oriented memory management.  Using this, clients can ask DAMON to find memory
regions of specific data access pattern and apply some memory management action
(e.g., page out, move to head of the LRU list, use huge page, ...).  We call
the request 'scheme'.

Proactive Reclamation on top of DAMON/DAMOS
-------------------------------------------

Therefore, by using DAMON for the cold pages detection, the proactive
reclamation's monitoring overhead issue could be solved.  If someone like
Google is ok to dedicate some CPUs for the monitoring and wants
page-granularity monitoring, they can configure DAMON so.

Actually, we previously implemented a version of proactive reclamation using
DAMOS and achieved noticeable improvements with our evaluation setup[5].
Nevertheless, it was only for a proof-of-concept.  It supports only virtual
address spaces of processes, and require additional tuning efforts for given
workloads and the hardware.  For the tuning, we recently introduced a simple
auto-tuning user space tool[8].  Google is also known to using a ML-based
similar approach for their fleets[2].  But, making it just works in the kernel
would be more convenient for general users.

To this end, this patchset improves DAMOS to be ready for such production
usages, and implements another version of the proactive reclamation, namely
DAMON_RECLAIM, on top of it.

DAMOS Improvements: Speed Limit, Prioritization, and Watermarks
---------------------------------------------------------------

First of all, the current version of DAMOS supports only virtual address
spaces.  This patchset makes it supports the physical address space for the
page out action.

One major problem of the current version of DAMOS is the lack of the
aggressiveness control, which can results in arbitrary overhead.  For example,
if huge memory regions having the data access pattern of interest are found,
applying the requested action to all of the regions could incur significant
overhead.  It can be controlled by modifying the target data access pattern
with manual or automated approaches[2,8].  But, some people would prefer the
kernel to just work with only intuitive tuning or default values.

For this, this patchset implements a safeguard time/size quota.  Using this,
the clients can specify up to how much time can be used for applying the
action, and/or up to how much memory regions the action can be applied within
specific time duration.  A followup question is, to which memory regions should
the action applied within the limits?  We implement a simple regions
prioritization mechanism for each action and make DAMOS to apply the action to
high priority regions first.  It also allows clients tune the prioritization
mechanism to use different weights for region's size, access frequency, and
age.  This means we could use not only LRU but also LFU or some fancy
algorithms like CAR[10] with lightweight overhead.

Though DAMON is lightweight, someone would want to remove even the overhead
when it is unnecessary.  Currently, it should manually turned on and off by
clients, but some clients would simply want to turn it on and off based on some
metrics like free memory ratio or memory fragmentation.  For such cases, this
patchset implements a watermarks-based automatic activation feature.  It allows
the clients configure the metric of their interest, and three watermarks of the
metric.  If the metric is higher than the high watermark or lower than the low
watermark, the scheme is deactivated.  If the metric is lower than the mid
watermark but higher than the low watermark, the scheme is activated.

DAMON-based Reclaim
-------------------

Using the improved DAMOS, this patchset implements a static kernel module
called 'damon_reclaim'.  It finds memory regions that didn't accessed for
specific time duration and page out.  Consuming too much CPU for the paging out
operations, or invoking it too frequently can be critical for systems
configuring its swap devices with software-defined in-memory block devices like
zram or total number of writes limited devices like SSDs, respectively.  To
avoid the problems, the time and/or size quotas can be configured.  Under the
quotas, it pages out memory regions that didn't accessed longer first.  Also,
to remove the monitoring overhead under peaceful situation, and to fall back to
the LRU-list based page granularity reclamation when it doesn't make progress,
the three watermarks based activation mechanism is used, with the free memory
ratio as the watermark metric.

For convenient configurations, it provides several module parameters.  Using
these, sysadmins can enable/disable it and tune the coldness identification
time threshold, the time/size quotas, and the three watermarks.  In detail,
sysadmins can use the kernel command line for a boot time tuning, or the sysfs
('/sys/modules/damon_reclaimparameters/') for overriding those in runtime.

Evaluation
==========

In short, DAMON_RECLAIM on v5.13 Linux kernel with ZRAM swap device and 50ms/s
time quota achieves 40.34% memory saving with only 3.38% runtime overhead.  For
this, DAMON_RECLAIM consumes only 5.16% of single CPU time.  Among the CPU
consumption, only up to about 1.448% of single CPU time is expected to be used
for the access pattern monitoring.

Setup
-----

We evaluate DAMON_RECLAIM to show how each of the DAMOS improvements make
effect.  For this, we measure entire system memory footprint and runtime of 24
realistic workloads in PARSEC3 and SPLASH-2X benchmark suites on my QEMU/KVM
based virtual machine.  The virtual machine runs on an i3.metal AWS instance
and has 130GiB memory.  It also utilizes a 4 GiB ZRAM swap device.  We do the
measurement 5 times and use averages.  We also measure the CPU consumption of
DAMON_RECLAIM.

Detailed Results
----------------

The result numbers are shown in below table.

DAMON_RECLAIM without the speed limit achieves 47.16% memory saving, but incur
5.4% runtime slowdown to the workloads on average.  For this, DAMON_RECLAIM
consumes about 11.62% single CPU time.

Applying 10ms/s, 50ms/s, and 200ms/s time quotas without the regions
prioritization reduces the slowdown to 2.51%, 4.53%, and 4.69%, respectively.
DAMON_RECLAIM's CPU utilization also similarly reduced: 1.78%, 5.7%, and 10.92%
of single CPU time.  That is, the overhead is proportional to the speed limit.
Nevertheless, it also reduces the memory saving because it becomes less
aggressive.  In detail, the three variants show 4.55%, 40.84%, and 48.42%
memory saving, respectively.

Applying the regions prioritization (page out regions that not accessed longer
first within the time quota) further reduces the performance degradation.
Runtime slowdowns has been 2.51% -> 1.84% (10ms/s), 4.53% -> 3.38% (50ms/s), and
4.69% -> 5.1% (200ms/s).  Interestingly, prioritization also reduced memory
saving a little bit.  I think that's because already paged out regions are
prioritized again.

    time quota   prioritization  memory_saving  cpu_util  slowdown
    N            N               47.16%         11.62%    5.4%
    10ms/s       N               4.55%          1.78%     2.51%
    50ms/s       N               40.84%         5.7%      4.53%
    200ms/s      N               48.42%         10.92%    4.69%
    10ms/s       Y               0.77%          1.37%     1.84%
    50ms/s       Y               40.34%         5.16%     3.38%
    200ms/s      Y               47.99%         10.41%    5.1%

Baseline and Complete Git Trees
===============================

The patches are based on the latest -mm tree (v5.14-rc1-mmots-2021-07-15-18-47)
plus DAMON patchset[1], DAMOS patchset[7], and physical address space support
patchset[6].  You can also clone the complete git tree from:

    $ git clone git://github.com/sjp38/linux -b damon_reclaim/rfc/v3

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon_reclaim/rfc/v3

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

The first patch makes DAMOS to support the physical address space for the page
out action.  Following five patches (patches 2-6) implement the time/size
quotas.  Next four patches (patches 7-10) implement the memory regions
prioritization within the limit.  Then, three following patches (patches 11-13)
implement the watermarks-based schemes activation.  Finally, the last two
patches (patches 14-15) implement and document the DAMON-based reclamation on
top of the advanced DAMOS.

[1] https://lore.kernel.org/linux-mm/20210716081449.22187-1-sj38.park@gmail.com/
[2] https://research.google/pubs/pub48551/
[3] https://lwn.net/Articles/787611/
[4] https://damonitor.github.io
[5] https://damonitor.github.io/doc/html/latest/vm/damon/eval.html
[6] https://lore.kernel.org/linux-mm/20201216094221.11898-1-sjpark@amazon.com/
[7] https://lore.kernel.org/linux-mm/20201216084404.23183-1-sjpark@amazon.com/
[8] https://github.com/awslabs/damoos
[9] https://www.kernel.org/doc/html/latest/vm/free_page_reporting.html
[10] https://www.usenix.org/conference/fast-04/car-clock-adaptive-replacement

Patch History
=============

Changes from RFC v2
(https://lore.kernel.org/linux-mm/20210608115254.11930-1-sj38.park@gmail.com/)
- Rebase on latest -mm tree (v5.14-rc1-mmots-2021-07-15-18-47)
- Make reclamation restarts from exactly the point it stopped due to the limit
- Implement a time quota (limits the time for trying reclamation of cold pages)

[1] https://lore.kernel.org/linux-mm/20210716081449.22187-1-sj38.park@gmail.com/

Changes from RFC v1
(https://lore.kernel.org/linux-mm/20210531133816.12689-1-sj38.park@gmail.com/)
- Avoid fake I/O load reporting (James Gowans)
- Remove kernel configs for the build time enabling and the parameters setting
- Export kdamond pid via a readonly parameter file
- Elaborate coverletter, especially for evaluation and DAMON_RECLAIM interface
- Add documentation
- Rebase on -mm tree
- Cleanup code
