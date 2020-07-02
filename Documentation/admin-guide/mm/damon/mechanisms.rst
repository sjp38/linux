.. SPDX-License-Identifier: GPL-2.0

==========
Mechanisms
==========

Address Space Specific Low Level Access Monitoring Primitives
=============================================================

The target address space independent core logics of DAMON, which mainly
controls the accuracy/overhead of the monitoring, are separated from the
address space dependent low level access monitoring primitives.  The primitives
are defined in two parts as below.

1. Identification of the monitoring target address range for the address space.
2. Access check of specific address range in the target space.

For flexible support of various address spaces, DAMON's API further export an
interface for configuration of the primitives.  Therefore, anyone can configure
DAMON with appropriate implementations for their use cases and even use their
own implementation if necessary. In this way, DAMON can be expanded for any
address space while keeping the optimized performance.  For example, physical
memory, virtual memory, swap space, those for specific processes, NUMA nodes,
files, and backing devices would be supportable.  Also, if some architectures
or kernel module support special access check primitives for specific address
space, those will be easily configurable.

DAMON currently provides an implementation of the primitives for the physical
and virtual address spaces.  The implementation for the physical address space
ask users to manually set the monitoring target address ranges while the
implementation for the virtual address space uses VMA for the target address
range identification.  Both uses PTE Accessed bit for the access check.

Below four sections describe the address independent core mechanisms and the
five knobs for tuning, that is, ``sampling interval``, ``aggregation
interval``, ``regions update interval``, ``minimum number of regions``, and
``maximum number of regions``.  After that, more details about the DAMON's
reference implementation of the primitives for the virtual address space
follows.


Basic Access Check
==================

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
=====================

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
===========================

At the beginning of the monitoring, DAMON constructs the initial regions by
evenly splitting the monitoring target memory region into the user-specified
minimum number of regions.  In this initial state, the assumption is normally
not kept and therefore the quality would be low.  To keep the assumption as
much as possible, DAMON adaptively merges and splits each region based on their
access frequency.

For each ``aggregation interval``, it compares the access frequencies of
adjacent regions and merges those if the frequency difference is small.  Then,
after it reports and clears the aggregated access frequency of each region, it
splits each region into two or three regions if the total number of regions
will not exceed the user-specified maximum number of regions after the split.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the bounds users set for their trade-off.


Handling Dynamic Target Space Changes
=====================================

The monitoring target address range could dynamically changed.  For example,
virtual memory could be dynamically mapped and unmapped.  Physical memory could
be hot-plugged.

As the changes could be quite frequent in some cases, DAMON checks the dynamic
memory mapping changes and applies it to the abstracted target area only for
each of a user-specified time interval (``regions update interval``).


Address Space Specific Low Primitives
=====================================

This is for the DAMON's reference implementation of the address space specific
low level primitive only.


PTE Accessed-bit Based Access Check
-----------------------------------

Both of the implementations for physical and virtual address spaces use PTE
Accessed-bit for basic access checks.  That is, those clears the bit for next
sampling target page and checks whether it set again after one sampling period.
To avoid disturbing other Accessed bit users such as the reclamation logic, the
implementations adjust the ``PG_Idle`` and ``PG_Young`` appropriately, as same
to the 'Idle Page Tracking'.


VMA-based Target Address Range Construction
-------------------------------------------

This is for the virtual address space specific primitives implementation.

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
