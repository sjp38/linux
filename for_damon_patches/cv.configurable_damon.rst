DAMON: Make Configurable for Various Address Spaces Including Physical Memory

DAMON[1] is currently supporing only virtual memory address spaces of several
target processes.  Therefore, the user of DAMON should first select the target
processes.  This could be cumbersome in some cases and even makes no sense if
the user want to monitor non-virtual address spaces.  Especially, there were
many requests and questions for support of physical memory monitoring.

There were also many questions about use of different access check mechanisms
such as dedicated H/W features[2], idle page tracking, or perf-mem, instead of
the PTE Accessed bit checking, which is currently used by DAMON.  Supporting
various access check mechanisms will make DAMON to be highly tunable for
specific cases.

Fortunately, the core mechanisms of DAMON, the region-based sampling and
adaptive regions adjustment, are not coupled with the virtual memory spaces and
Accessed bit based access check.  As long as there is a way to 1) address every
region in the space and 2) check access to specific address, the core
mechanisms could be applied.  Nonetheless, current implementation of DAMON is
highly coupled with the virtual memory address spaces.

[1] https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/
[2] https://images.anandtech.com/doci/10591/HC28.AMD.Mike%20Clark.final-page-016.jpg


Baseline and Complete Git Trees
===============================

The patches are based on the v5.6 plus DAMON v8 patchset[1] and DAMOS RFC v6[2]
patchset.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v1

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v1

This patchset breaks the couplings and allows the target region definition and
the access check to be configurable by users so that it can support various
types of address spaces and use cases.  Based on this patchset, you can
configure DAMON to monitor physical memory or other special address spaces with
your preferred access check mechanism.

[1] https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200407100007.3894-1-sjpark@amazon.com/


Sequence of Patches
===================

The sequence of patches is as follow.  The first patch defines the monitoring
region again based on pure address range abstraction so that there is no
assumption of virtual memory in there.  The following patch cleans up code
using the new abstraction.  The third patch allows users to configure the
initialization and dynamic update of the target address regions, which were
highly coupled with virtual memory area, with their own things.  Finally, the
fourth patch further make the access check mechanism, which were based on PTE
Accessed bit, configurable.
