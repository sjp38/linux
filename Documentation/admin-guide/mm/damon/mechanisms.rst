.. SPDX-License-Identifier: GPL-2.0

==========
Mechanisms
==========

This document describes the core mechanisms of DAMON and the five knobs for
tuning, that is, ``sampling interval``, ``aggregation interval``, ``regions
update interval``, ``minimum number of regions``, and ``maximum number of
regions``.


Basic Access Check
==================

The output of DAMON says what pages are how frequently accessed for a given
duration.  The resolution of the access frequency is controlled by setting
``sampling interval`` and ``aggregation interval``.  In detail,
DAMON checks access to each page per ``sampling interval``, aggregates the
results.  In other words, counts the number of the accesses to each page.
After each ``aggregation interval`` passes, DAMON signals users to read and
save the aggregated access frequency if they want and then clears those.  For
the access check of each page, DAMON uses the Accessed bits of PTEs.  This can
be described in below simple pseudo-code::

    while monitoring_on:
        for page in monitoring_target:
            if accessed(page):
                nr_accesses[page] += 1
        if time() % aggregation_interval == 0:
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


Handling Virtual Memory Mappings
================================

Note that this is used by the DAMON's reference implementation of the virtual
memory address specific low level primitive only.

Only small parts in the super-huge virtual address space of the processes are
mapped to physical memory and accessed.  Thus, tracking the unmapped address
regions is just wasteful.  However, because DAMON can deal with some level of
noise using the adaptive regions adjustment mechanism, tracking every mapping
is not strictly required but could even incur a high overhead in some cases.
That said, too huge unmapped areas inside the monitoring target should be
removed to not take the time for the adaptive mechanism.

For the reason, DAMON converts the complex mappings to three distinct regions
that cover every mapped area of the address space.  Also, the two gaps between
the three regions are the two biggest unmapped areas in the given address
space.  The two biggest unmapped areas might be the gap between the heap and
the uppermost mmap()-ed region, and the gap between the lowermost mmap()-ed
region and the stack will be two biggest unmapped regions.  Because these gaps
are exceptionally huge areas in usual address space, excluding these two
biggest unmapped regions will be sufficient to make a trade-off.  Below shows
this in detail::

    <heap>
    <BIG UNMAPPED REGION 1>
    <uppermost mmap()-ed region>
    (small mmap()-ed regions and munmap()-ed regions)
    <lowermost mmap()-ed region>
    <BIG UNMAPPED REGION 2>
    <stack>

To further minimize dynamic mapping changes applying overhead, DAMON checks the
dynamic memory mapping changes and applies it to the abstracted target area
only for each of a user-specified time interval (``regions update interval``).
