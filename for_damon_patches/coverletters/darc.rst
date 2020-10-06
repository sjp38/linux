Introduce DAMON-based Proactive Reclamation

NOTE: This is only an RFC for future features of DAMON patchset[1], which is
not merged in the mainline yet.  The aim of this RFC is to show how DAMON would
be evolved once it is merged in.  So, if you have some interest here, please
consider reviewing the DAMON patchset, either.

[1] https://lore.kernel.org/linux-mm/20210520075629.4332-1-sj38.park@gmail.com/

Introduction
============

In short, this patchset improves the engine for general data access
pattern-oriented memory management for production quality and implements the
monitoring issue solved version of proactive reclamation on top of it.

Proactive Reclamation
---------------------

Proactively reclaiming cold pages helps saving memory and reducing latency
spikes that incurred by the direct reclaim of the process or CPU consumption of
kswapd, while incurring only minimal performance degradation for memory
over-committed systems[2].

Free Pages Reporting[9] based memory over-commit virtualization systems is
another use case of it.  In the configuration, the guest VMs are supposed to
report free memory to host, so that host can reallocate the memory to other
guests.  However, because Linux is designed to cache things in memory
aggressively, no guest would voluntarily free much memory without host's
intervention.  Proactive reclamation could make the situation much better.

Google has implemented the idea and using it in their data center.  They
further proposed upstreaming it in LSFMM'19, and "the general consensus was
that, while this sort of proactive reclaim would be useful for a number of
users, the cost of this particular solution was too high to consider merging it
upstream"[3].  The cost mostly comes from the coldness tracking.  Roughly
speaking, the implementation periodically scans the 'Accessed' bit of each
page.  For the reason, the overhead linearly increases as the size of the
memory and the scanning frequency grows.  As a result, Google is known to
dedicating a CPU for the work.  That's reasonable for Google, but it wouldn't
for someone.

DAMON and DAMOS: An engine for data access pattern-oriented memory management
-----------------------------------------------------------------------------

DAMON[4] is a framework for general data access monitoring.  When it's adaptive
monitoring overhead control feature is used, it incurs minimized monitoring
overhead.  It's not only small, but also upper-bounded, regardless of the
monitoring target memory size.  Clients can set the upper-limit as they want.
While monitoring 70 GB memory of a production system every 5 milliseconds, it
consumes less than 1% single CPU time.  For this, it sacrifies some of the
quality of the monitoring results.  Nevertheless, the lower-bound of the
quality is configurable, and it has a best-effort algorithm for the quality.
Our test results[5] show the quality is practical enough.  From the production
system monitoring, we were able to find a 4 KB memory region that shows highest
access frequency in the 70 GB memory.  For someone still couldn't be convinced,
DAMON also supports the page-granularity monitoring[6], though it makes the
overhead much higher and proportional to the memory size again.

We normally don't monitor the data access pattern just for fun but to use if
for something like memory management.  Proactive reclamation is one such usage.
For such general cases, DAMON provides a feature called DAMon-based Operation
Schemes (DAMOS)[7], which makes DAMON as an engine for general data access
pattern oriented memory management.  Using this, clients can ask DAMON to find
memory regions of specific data access pattern and apply some memory management
action (e.g., paging out, move to head of the LRU list, use huge page, ...).
The request is called 'scheme' below.

DAMON-based Reclaim
-------------------

Therefore, by using DAMON in the cold pages detection, the proactive
reclamation's monitoring overhead issue could be solved.  If someone like
Google is ok to dedicate CPUs for the monitoring and wants page-granularity
quality, they could configure DAMON so.

Actually, we already implemented a version of proactive reclamation on it and
shared its evaluation results before[5], which show noticeable achievements.
Nevertheless, it is only in a proof-of-concept level.  Recently we further
introduced a user space tool[8] that automatically tunes schemes for specific
workloads and systems.  Google's proactive reclamation also uses another
ML-based similar approach[2].  But, making it just works in the kernel would be
more convenient for more general users.

To this end, this patchset improves DAMOS to be proper for such production use,
and implements another version of the proactive reclamation, namely
DAMON_RECLAIM, on top of it.

DAMOS Improvements: Speed Limit, Prioritization, and Watermarks
---------------------------------------------------------------

One major problem of current version of DAMOS is the absence of the
aggressiveness control.  For example, if huge memory regions of the specified
data access pattern is found, applying the action to the huge memory regions
could incur overhead.  It could controlled by modifying the target data access
pattern and some auto-tuning approaches are available.  But, for someone who
unable to use such tools or people who want it just works with only intuitive
tuning or default values, at least some safeguards are required.

For this, we provide speed limit.  Using this, the client can specify up to how
much amount of memory the action is allowed to be applied within specific time
duration.  Followup question is, to which memory regions should the action
applied within the limit?  We implement a simple regions prioritization
mechanism for each action and make DAMOS to apply the action to high priority
regions first.  It also allows users tune the prioritization by giving
different weights to region's size, access frequency, and age.

Another problem of current version of DAMOS is that it should manually turned
on and off, by clients.  Though DAMON is very lightweight, someone would not
convinced.  For such cases, we implement watermarks-based automatic schemes
activation.  It allows the clients configuring the metric of their interest and
three watermarks.  If the metric is higher than the high watermark or lower
than the low watermark, the scheme is deactivated.  If the metric is lower than
the mid watermark but higher than the low watermark, the scheme is activated.

For example, in case of the proactive reclamation, the metric could be amount
of free memory.  Using the watermarks, the sysadmin would be able to set it do
nothing at all when free memory is enough, but starts proactive reclamation
under light memory pressure.  Then, if it doesn't works well enough and the
free memory becomes lower than the low watermark, we fall back to the LRU-based
page-granularity reclamation.

Evaluation
==========

We measured system memory usage and runtime of 24 realistic workloads in
PARSEC3 and SPLASH-2X benchmark suites on my QEMU/KVM based virtual machine.
The virtual machine runs on an i3.metal AWS instance and has 130GiB memory.  It
utilizes 4 GiB zram as its swap device.  We do the measurement 5 times and use
averages.  We also measured the CPU consumption of DAMON_RECLAIM.

Compared to v5.12, DAMON_RECLAIM achieves 33% memory saving with only 2%
performance degradation.  For this, DAMON_RECLAIM consumed only 5.72% of single
CPU time.  Among the CPU consumption, only about 1.448% of single CPU time is
expected to be used for the monitoring.

Baseline and Complete Git Tree
==============================

The patches are based on the v5.12 plus DAMON patchset[1] plus DAMOS
patchset[7] plus physical address space support patchset[6].  You can also
clone the complete git tree from:

    $ git clone git://github.com/sjp38/linux -b damon_reclaim/rfc/v1

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon_reclaim/rfc/v1

Development Trees
-----------------

There are a couple of trees for entire DAMON patchset series and
features for future release.

- For latest release: https://github.com/sjp38/linux/tree/damon/master
- For next release: https://github.com/sjp38/linux/tree/damon/next

Long-term Support Trees
-----------------------

For people who want to test DAMON patchset series but using LTS kernels, there
are another couple of trees based on two latest LTS kernels respectively and
containing the 'damon/master' backports.

- For v5.4.y: https://github.com/sjp38/linux/tree/damon/for-v5.4.y
- For v5.10.y: https://github.com/sjp38/linux/tree/damon/for-v5.10.y

Sequence Of Patches
===================

The first patch makes DAMOS users able to described pages to be paged out cold
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
