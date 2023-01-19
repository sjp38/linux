.. SPDX-License-Identifier: GPL-2.0

======
Design
======

Configurable Layers
===================

For data access monitoring and additional low level work, DAMON needs a set of
implementations for specific operations that are dependent on and optimized for
the given target address space.  On the other hand, the accuracy and overhead
tradeoff mechanism, which is the core logic of DAMON, is in the pure logic
space.  DAMON separates the two parts in different layers, namely DAMON
Operations Set and DAMON Core Logics Layers, respectively.  It further defines
the interface between the layers to allow various operations sets to be
configured with the core logic.

Due to this design, users can extend DAMON for any address space by configuring
the core logic to use the appropriate operations set.  If any appropriate set
is unavailable, users can implement one on their own.

For example, physical memory, virtual memory, swap space, those for specific
processes, NUMA nodes, files, and backing memory devices would be supportable.
Also, if some architectures or devices supporting special optimized access
check primitives, those will be easily configurable.


Operations Set Layer
====================

The monitoring operations are defined in two parts:

1. Identification of the monitoring target address range for the address space.
2. Access check of specific address range in the target space.

DAMON currently provides the implementations of the operations for the physical
and virtual address spaces. Below two subsections describe how those work.


VMA-based Target Address Range Construction
-------------------------------------------

This is only for the virtual address space monitoring operations
implementation.  That for the physical address space simply asks users to
manually set the monitoring target address ranges.

Only small parts in the super-huge virtual address space of the processes are
mapped to the physical memory and accessed.  Thus, tracking the unmapped
address regions is just wasteful.  However, because DAMON can deal with some
level of noise using the adaptive regions adjustment mechanism, tracking every
mapping is not strictly required but could even incur a high overhead in some
cases.  That said, too huge unmapped areas inside the monitoring target should
be removed to not take the time for the adaptive mechanism.

For the reason, this implementation converts the complex mappings to three
distinct regions that cover every mapped area of the address space.  The two
gaps between the three regions are the two biggest unmapped areas in the given
address space.  The two biggest unmapped areas would be the gap between the
heap and the uppermost mmap()-ed region, and the gap between the lowermost
mmap()-ed region and the stack in most of the cases.  Because these gaps are
exceptionally huge in usual address spaces, excluding these will be sufficient
to make a reasonable trade-off.  Below shows this in detail::

    <heap>
    <BIG UNMAPPED REGION 1>
    <uppermost mmap()-ed region>
    (small mmap()-ed regions and munmap()-ed regions)
    <lowermost mmap()-ed region>
    <BIG UNMAPPED REGION 2>
    <stack>


PTE Accessed-bit Based Access Check
-----------------------------------

Both of the implementations for physical and virtual address spaces use PTE
Accessed-bit for basic access checks.  Only one difference is the way of
finding the relevant PTE Accessed bit(s) from the address.  While the
implementation for the virtual address walks the page table for the target task
of the address, the implementation for the physical address walks every page
table having a mapping to the address.  In this way, the implementations find
and clear the bit(s) for next sampling target address and checks whether the
bit(s) set again after one sampling period.  This could disturb other kernel
subsystems using the Accessed bits, namely Idle page tracking and the reclaim
logic.  DAMON does nothing to avoid disturbing Idle page tracking, so handling
the interference is the responsibility of sysadmins.  However, it solves the
conflict with the reclaim logic using ``PG_idle`` and ``PG_young`` page flags,
as Idle page tracking does.


Monitoring Core Logics
======================

Below four sections describe each of the DAMON core mechanisms and the five
monitoring attributes, ``sampling interval``, ``aggregation interval``,
``update interval``, ``minimum number of regions``, and ``maximum number of
regions``.


Access Frequency Monitoring
---------------------------

The output of DAMON says what pages are how frequently accessed for a given
duration.  The resolution of the access frequency is controlled by setting
``sampling interval`` and ``aggregation interval``.  In detail, DAMON checks
access to each page per ``sampling interval`` and aggregates the results.  In
other words, counts the number of the accesses to each page.  After each
``aggregation interval`` passes, DAMON calls callback functions that previously
registered by users so that users can read the aggregated results and then
clears the results.  This can be described in below simple pseudo-code::

    while monitoring_on:
        for page in monitoring_target:
            if accessed(page):
                nr_accesses[page] += 1
        if time() % aggregation_interval == 0:
            for callback in user_registered_callbacks:
                callback(monitoring_target, nr_accesses)
            for page in monitoring_target:
                nr_accesses[page] = 0
        sleep(sampling interval)

The monitoring overhead of this mechanism will arbitrarily increase as the
size of the target workload grows.


Region Based Sampling
---------------------

To avoid the unbounded increase of the overhead, DAMON groups adjacent pages
that assumed to have the same access frequencies into a region.  As long as the
assumption (pages in a region have the same access frequencies) is kept, only
one page in the region is required to be checked.  Thus, for each ``sampling
interval``, DAMON randomly picks one page in each region, waits for one
``sampling interval``, checks whether the page is accessed meanwhile, and
increases the access frequency of the region if so.  Therefore, the monitoring
overhead is controllable by setting the number of regions.  DAMON allows users
to set the minimum and the maximum number of regions for the trade-off.

This scheme, however, cannot preserve the quality of the output if the
assumption is not guaranteed.


Adaptive Regions Adjustment
---------------------------

Even somehow the initial monitoring target regions are well constructed to
fulfill the assumption (pages in same region have similar access frequencies),
the data access pattern can be dynamically changed.  This will result in low
monitoring quality.  To keep the assumption as much as possible, DAMON
adaptively merges and splits each region based on their access frequency.

For each ``aggregation interval``, it compares the access frequencies of
adjacent regions and merges those if the frequency difference is small.  Then,
after it reports and clears the aggregated access frequency of each region, it
splits each region into two or three regions if the total number of regions
will not exceed the user-specified maximum number of regions after the split.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the bounds users set for their trade-off.


Dynamic Target Space Updates Handling
-------------------------------------

The monitoring target address range could dynamically changed.  For example,
virtual memory could be dynamically mapped and unmapped.  Physical memory could
be hot-plugged.

As the changes could be quite frequent in some cases, DAMON allows the
monitoring operations to check dynamic changes including memory mapping changes
and applies it to monitoring operations-related data structures such as the
abstracted monitoring target memory area only for each of a user-specified time
interval (``update interval``).


Access-aware Schemes
====================

The reason why users want to use DAMON would be utilizing the monitoring
results for data access-aware system operations.  For example,

    page out memory regions that are not accessed for more than 10 minutes

or

    use THP for memory regions that are larger than 2 MiB and showing a high
    access frequency for more than 2 seconds.

One of the simplest implementations of such schemes would be a sort of
profiling-guided optimization.  That is, profile data access patterns of the
workload or system using DAMON, and then make some system operation changes.
The changes could be made by invoking system calls like ``madvise()``, using
system tuning knobs like ``sysctl``, or adding or removing devices, such as
DRAM.  Both offline and online approaches would be available.

Such approaches could impose unnecessary redundancy and inefficiency, though.
The monitoring and finding regions of the interest could be redundant if the
type of the interests is somewhat common.  Communicating the information
including monitoring results and management actions request between kernel and
user spaces could be inefficient.

Such redundancy and inefficiencies can be mitigated by offloading the works to
DAMON.  For that, DAMO provides a feature called DAMON-based Operation Schemes
(DAMOS).  The feature asks users to specify their desired schemes at a high
level.  Then, DAMOS runs DAMON, finds regions having the access pattern of the
interest from the monitoring results, and applies the scheme-requested system
operation actions to the regions.

Operation Action
----------------

DAMOS users should describe what action they want to apply to the regions of
their interest.  Then, DAMOS applies the action to the memory regions, as soon
as found.  The DAMOS-supporting operation actions include hinting khugepaged to
collapse or split the regions to/from hugepages, paging out those, marking as
active or inactive, and doing nothing but count statistics of the found
regions.

The implementation of each action is in the DAMON operations set layer, because
it normally depends on the monitoring target address space.  Hence, different
monitoring operations implementation sets support different lists of the
actions.

To avoid repeatedly applying an action to a region due to its old access
pattern, DAMOS resets the age of regions when an action is applied to.  In
other words, DAMOS considers a region where an action is applied as a new one.


Target Access Pattern
---------------------

DAMOS users can specify to what regions they have interests by specifying the
access patterns of the regions.  The pattern is constructed with the DAMON's
monitoring results providing information, specifically the size, the access
frequency (``nr_accesses``), and the age.  The age of each region means the
amount of time that the region has maintained current size and the access
frequency.  Users can describe their access pattern of interest by setting
minimum and maximum values of the three characteristics.  If a region is having
all the three characteristics in the ranges, DAMOS classifies it as one of the
regions that the scheme is having the interest in.



Quotas
------

The access pattern should be carefully set.  Otherwise, the scheme could incur
a high overhead coming from becoming unexpectedly aggressive.  For example, if
a huge memory region having the access pattern of interest is found, applying
the requested action to all pages of the huge region could incur unacceptable
overhead.  Controlling it with only the access pattern can require quite a lot
of expertise and experiences, especially when the access pattern of the system
can dynamically and unexpectedly change.

For such cases, DAMOS lets users set safety guards for each scheme, namely time
and size quotas.  Using the two quotas, users can specify an upper-limit of
time that DAMOS can use for applying the action, and/or a maximum bytes of
memory regions that the action can be applied within a user-specified time
duration, respectively.  In other words, users can control the upper-bound
overhead of their DAMOS schemes by setting the quotas.


Prioritization
~~~~~~~~~~~~~~

One followup question of the quotas feature would be, to which memory regions
DAMOS will apply the action under the limit.  To make a good decision for the
case, DAMOS calculates priority scores for the regions of the scheme-specified
access pattern.  Then, it further finds to what priority score regions the
action can be applied without breaking the limit, and applies the action to
regions of the safe cores.

The prioritization mechanism should be different for each action.  For example,
less frequently and less recently accessed (colder) memory regions would need
to be prioritized for page out scheme action.  In contrast, the colder regions
would need to be deprioritized for huge page collapse scheme action.  Hence,
the prioritization mechanisms for each action are implemented in each DAMON
operations set, together with the actions.

Though the implementation is up to the DAMON operations set, it could normally
be expected to use parts of, or all of the access pattern of the regions.  Some
users would want the mechanisms to be personalized for their specific case.
For example, some users would want the mechanism to prioritize access frequency
(``nr_accesses``) more than the recency (``age``).  DAMOS allows users to
specify the weight of each access pattern characteristic for the case, and
passes the information to the prioritization mechanism in the underlying
operations set.  Nevertheless, how and even whether the weight will be
respected are up to the underlying prioritization mechanism implementation.


Watermarks
----------

DAMON-based operation schemes wouldn't be needed always.  For example, when a
sufficient amount of free memory is guaranteed, a scheme for proactive
reclamation might not be needed.  To avoid any unnecessary overhead coming from
DAMON and DAMOS schemes, the user would need to manually monitor some system
status metrics such as free memory or memory fragmentation ratio, and turn
DAMON and DAMOS schemes on or off.

To make it automated, DAMOS provides a watermarks-based automatic activation
feature.  It allows the users to configure the metric of their interest, and
three watermark values.  If the value of the metric becomes higher than the
high watermark or lower than the low watermark, the scheme is deactivated.  If
the metric becomes lower than the mid watermark, the scheme is activated.  If a
DAMON context is running with one or more schemes and all schemes are
deactivated by the watermarks, the monitoring is also deactivated.  In this
case, the DAMON worker thread incurs only nearly zero overhead, because it does
nothing but just the watermarks checks.


Filters
-------

In some situations, users could have special information that kernel cannot
know, including the future access patterns or some special requirements for
some types of memory of programs or systems if they wrote or configured those
themselves, or have good profiling results.  In this case, users could want to
control DAMOS schemes using not only the access pattern but also the additional
special information.  For example, some users would have slow swap devices with
fast storage devices for file systems, and know a list of latency-critical
processes.  In such cases, the users may want to avoid their DAMOS schemes
reclaiming anonymous pages of latency-critical processes.

To help such cases, DAMOS provides a feature called DAMOS filters.  The feature
allows users to set an arbitrary number of filters for each scheme.  Each
filter specifies the type of memory on which the filter should operate for, and
whether the scheme's behavior should not apply to that type of memory
(filter-out) or to all types of memory except that type (filter-in).  Based on
the type of the filter, additional arguments can be required.  For example,
memory cgroup type filters request users to specify the memory cgroup file path
of their interest.

As of this writing, anonymous page type and memory cgroup type are supported by
the filters feature.  Hence, users can apply specific schemes to only anonymous
pages, non-anonymous pages, pages of specific cgroups, all pages but those of
specific cgroups, and any combination of those.


User Kernel Components
======================

Because DAMON is a framework in the kernel, its direct users are other kernel
components such as subsystems and modules.  For those, DAMON provides an API,
namely ``include/linux/damon.h``.  Please refer to the API :doc:`document
</mm/damon/api>` for details of the interface.


General Purpose User ABI
------------------------

DAMON user interface modules, namely 'DAMON sysfs interface' and 'DAMON debugfs
interface' are DAMON API user kernel modules that provide DAMON ABIs to the
user-space.  Please note that DAMON debugfs is deprecated.

Like many other ABIs, the modules create files on sysfs and debugfs, allow
users to specify their requests to and get the answers from DAMON by writing to
and reading from the files.  As a response to such user-space users' IOs, DAMON
user interface modules control DAMON as requested using the DAMON API, and
return the results to the user-space.  The ABIs exposed by the DAMON user
interface modules are far from human-friendly interfaces, because those are
designed for being used by user space tool programs, rather than human beings'
fingers.  Human users are encouraged to use such user space tool.  One such
Python-written user space tool is available at Github
(https://github.com/awslabs/damo) and Pypi
(https://pypistats.org/packages/damo).

Please refer to the ABI :doc:`document </admin-guide/mm/damon/usage>` for
details of the interfaces.


Special-Purpose Access-aware Kernel Modules
-------------------------------------------

DAMON sysfs/debugfs user interfaces are for full control of all DAMON features
in runtime.  For system-wide boot time DAMON utilization for specific purposes,
e.g., proactive reclamation or LRU lists balancing, the interfaces could be
unnecessarily complicated because those require setting unnecessarily many
configurations for the simple purpose, and restricted because those support
only runtime manipulation.

To support such specific purpose usages of DAMON with only essential simple
user interface, yet more DAMON API user kernel modules are implemented.  The
simpler interface can be provided via module parameters, which can also be set
at boot time via kernel command line.  Currently, two modules for proactive
reclamation and LRU lists manipulation are provided.  For more detail, read the
usage documents for the modules (:doc:`/admin-guide/mm/damon/reclaim` and
:doc:`/admin-guide/mm/damon/lru_sort`).
