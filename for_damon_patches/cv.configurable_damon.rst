DAMON: Support Monitoring of Various Address Spaces Including Physical Memory

DAMON[1] is currently supporing only virtual memory address spaces of several
target processes.  Therefore, the user of DAMON should first select the target
processes.  This could be cumbersome in some cases.

There were also many questions about use of different access check mechanisms
such as dedicated H/W features[2], idle page tracking, or perf-mem, instead of
the PTE Accessed bit checking, which is currently used by DAMON.  Supporting
various access check mechanisms will make DAMON to be highly tunable for
special use cases.

Conceptually, the core mechanisms of DAMON, the region-based sampling and
adaptive regions adjustment, are not coupled with the virtual memory spaces and
Accessed bit based access check.  As long as there is a way to 1) address every
region in the space and 2) check access to specific address, the core
mechanisms could be applied.  However, current implementation of DAMON is
unnecessarily coupled with those.

This patchset therefore breaks the couplings and allows the target region
definition and the access check to be configurable by users.  With this
patchset, DAMON can thus support various types of address spaces and use cases.
Also, we further implement the physical memory monitoring support using this
configuration feature, so that users can just use.


[1] https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/
[2] https://images.anandtech.com/doci/10591/HC28.AMD.Mike%20Clark.final-page-016.jpg


Baseline and Complete Git Trees
===============================

The patches are based on the v5.7 plus DAMON v14 patchset and DAMOS RFC v10
patchset.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b cdamon/rfc/v2

The web is also available:
https://github.com/sjp38/linux/releases/tag/cdamon/rfc/v2

[1] https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200407100007.3894-1-sjpark@amazon.com/


Sequence of Patches
===================

The sequence of patches is as follow.  The 1st patch defines the monitoring
region again based on pure address range abstraction so that no
assumption of virtual memory is in there.  The following 2nd patch cleans up code
using the new abstraction.  The 3rd patch allows users to configure the
initialization and dynamic update of the target address regions, which were
previously coupled with virtual memory area, as they want.  Then, the
4th patch further make the access check mechanism, which were based on PTE
Accessed bit, configurable.

Following patches implements physical memory monitoring using this
configuration feature.  5th patch implements the physical memory access check
functions.  Using this, kernel space code can just use the physical memory
monitoring.  For user space, 6th-11th patches implements debugfs interface for
the physical memory monitoring and documents the feature.
