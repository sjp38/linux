Subject: Introduce Data Access MONitor (DAMON)

Introduction
============

Memory management decisions can be improved if finer data access information is
available.  However, because such finer information usually comes with higher
overhead, most systems including Linux forgives the potential benefit and rely
on only coarse information or some light-weight heuristics.  The pseudo-LRU and
the aggressive THP promotions are such examples.

A number of data access pattern awared memory management optimizations (refer
to 'Appendix A' for more details) consistently say the potential benefit is not
small.  However, none of those has successfully merged to the mainline Linux
kernel mainly due to the absence of a scalable and efficient data access
monitoring mechanism.  Refer to 'Appendix B' to see the limitations of existing
memory monitoring mechanisms.

DAMON is a data access monitoring subsystem for the problem.  It is 1) accurate
enough to be used for the DRAM level memory management (a straightforward
DAMON-based optimization achieved up to 2.55x speedup), 2) light-weight enough
to be applied online (compared to a straightforward access monitoring scheme,
DAMON is up to 94,242.42x lighter) and 3) keeps predefined upper-bound overhead
regardless of the size of target workloads (thus scalable).  Refer to 'Appendix
C' if you interested in how it is possible, and 'Appendix F' to know how the
numbers collected.

DAMON has mainly designed for the kernel's memory management mechanisms.
However, because it is implemented as a standalone kernel module and provides
several interfaces, it can be used by a wide range of users including kernel
space programs, user space programs, programmers, and administrators.  DAMON
is now supporting the monitoring only, but it will also provide simple and
convenient data access pattern awared memory managements by itself.  Refer to
'Appendix D' for more detailed expected usages of DAMON.


Visualized Outputs of DAMON
===========================

For intuitively understanding of DAMON, I made web pages[1-8] showing the
visualized dynamic data access pattern of various realistic workloads, which I
picked up from PARSEC3 and SPLASH-2X bechmark suites.  The figures are
generated using the user space tool in 10th patch of this patchset.

There are pages showing the heatmap format dynamic access pattern of each
workload for heap area[1], mmap()-ed area[2], and stack[3] area.  I splitted
the entire address space to the three area because there are huge unmapped
regions between the areas.

You can also show how the dynamic working set size of each workload is
distributed[4], and how it is chronologically changing[5].

The most important characteristic of DAMON is its promise of the upperbound of
the monitoring overhead.  To show whether DAMON keeps the promise well, I
visualized the number of monitoring operations required for each 5
milliseconds, which is configured to not exceed 1000.  You can show the
distribution of the numbers[6] and how it changes chronologically[7].

[1] https://damonitor.github.io/reports/latest/by_image/heatmap.0.png.html
[2] https://damonitor.github.io/reports/latest/by_image/heatmap.1.png.html
[3] https://damonitor.github.io/reports/latest/by_image/heatmap.2.png.html
[4] https://damonitor.github.io/reports/latest/by_image/wss_sz.png.html
[5] https://damonitor.github.io/reports/latest/by_image/wss_time.png.html
[6] https://damonitor.github.io/reports/latest/by_image/nr_regions_sz.png.html
[7] https://damonitor.github.io/reports/latest/by_image/nr_regions_time.png.html


Data Access Monitoring-based Operation Schemes
==============================================

As 'Appendix D' describes, DAMON can be used for data access monitoring-based
operation schemes (DAMOS).  RFC patchsets for DAMOS are already available
(https://lore.kernel.org/linux-mm/20200218085309.18346-1-sjpark@amazon.com/).

By applying a very simple scheme for THP promotion/demotion with a latest
version of the patchset (not posted yet), DAMON achieved 18x lower memory space
overhead compared to THP while preserving about 50% of the THP performance
benefit with SPLASH-2X benchmark suite.

The detailed setup and number will be posted soon with the next RFC patchset
for DAMOS.  The posting is currently scheduled for tomorrow.


Frequently Asked Questions
==========================

Q: Why DAMON is not integrated with perf?
A: From the perspective of perf like profilers, DAMON can be thought of as a
data source in kernel, like the tracepoints, the pressure stall information
(psi), or the idle page tracking.  Thus, it is easy to integrate DAMON with the
profilers.  However, this patchset doesn't provide a fancy perf integration
because current step of DAMON development is focused on its core logic only.
That said, DAMON already provides two interfaces for user space programs, which
based on debugfs and tracepoint, respectively.  Using the tracepoint interface,
you can use DAMON with perf.  This patchset also provides a debugfs interface
based user space tool for DAMON.  It can be used to record, visualize, and
analyze data access patterns of target processes in a convenient way.

Q: Why a new module, instead of extending perf or other tools?
A: First, DAMON aims to be used by other programs including the kernel.
Therefore, having dependency to specific tools like perf is not desirable.
Second, because it need to be lightweight as much as possible so that it can be
used online, any unnecessary overhead such as kernel - user space context
switching cost should be avoided.  These are the two most biggest reasons why
DAMON is implemented in the kernel space.  The idle page tracking subsystem
would be the kernel module that most seems similar to DAMON.  However, its own
interface is not compatible with DAMON.  Also, the internal implementation of
it has no common part to be reused by DAMON.

Q: Can 'perf mem' provide the data required for DAMON?
A: On the systems supporting 'perf mem', yes.  DAMON is using the PTE Accessed
bits in low level.  Other H/W or S/W features that can be used for the purpose
could be used.  However, as explained with above question, DAMON need to be
implemented in the kernel space.


Evaluations
===========

We evaluated DAMON's overhead, monitoring quality and usefulness using 25
realistic workloads on my QEMU/KVM based virtual machine.

DAMON is lightweight.  It consumes only -0.08% more system memory and up to 1%
CPU time.  It makes target worloads only 0.45% slower.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP removes 68.22% of THP memory
overheads while preserving 51.88% of THP speedup.  Another experimental
DAMON-based 'proactive reclamation' implementation reduced 17.91% of system
memory footprint and 71.44% of residential sets while incurring only 1.25%
runtime overhead in best case (parsec3/freqmine).

NOTE that the experimentail THP optimization and proactive reclamation are not
for production, just only for proof of concepts.

Please refer to 'Appendix E' for detailed evaluation setup and results.


References
==========

Prototypes of DAMON have introduced by an LPC kernel summit track talk[1] and
two academic papers[2,3].  Please refer to those for more detailed information,
especially the evaluations.  The latest version of the patchsets has also
introduced by an LWN artice[4].

[1] SeongJae Park, Tracing Data Access Pattern with Bounded Overhead and
    Best-effort Accuracy. In The Linux Kernel Summit, September 2019.
    https://linuxplumbersconf.org/event/4/contributions/548/
[2] SeongJae Park, Yunjae Lee, Heon Y. Yeom, Profiling Dynamic Data Access
    Patterns with Controlled Overhead and Quality. In 20th ACM/IFIP
    International Middleware Conference Industry, December 2019.
    https://dl.acm.org/doi/10.1145/3366626.3368125
[3] SeongJae Park, Yunjae Lee, Yunhee Kim, Heon Y. Yeom, Profiling Dynamic Data
    Access Patterns with Bounded Overhead and Accuracy. In IEEE International
    Workshop on Foundations and Applications of Self- Systems (FAS 2019), June
    2019.
[4] Jonathan Corbet, Memory-management optimization with DAMON. In Linux Weekly
    News (LWN), Feb 2020. https://lwn.net/Articles/812707/


Sequence Of Patches
===================

The patches are organized in the following sequence.  The first two patches are
preparation of DAMON patchset.  The 1st patch adds typos found in previous
versions of DAMON patchset to 'scripts/spelling.txt' so that the typos can be
caught by 'checkpatch.pl'.  The 2nd patch exports 'lookup_page_ext()' to GPL
modules so that it can be used by DAMON even though it is built as a loadable
module.

Next four patches implement the core of DAMON and it's programming interface.
The 3rd patch introduces DAMON module, it's data structures, and data structure
related common functions.  Each of following three patches (4nd to 6th)
implements the core mechanisms of DAMON, namely regions based sampling,
adaptive regions adjustment, and dynamic memory mapping chage adoption,
respectively, with programming interface supports of those.

Following four patches are for low level users of DAMON.  The 7th patch
implements callbacks for each of monitoring steps so that users can do whatever
they want with the access patterns.  The 8th one implements recording of access
patterns in DAMON for better convenience and efficiency.  Each of next two
patches (9th and 10th) respectively adds a debugfs interface for privileged
people and/or programs in user space, and a tracepoint for other tracepoints
supporting tracers such as perf.

Two patches for high level users of DAMON follows.  To provide a minimal
reference to the debugfs interface and for high level use/tests of the DAMON,
the next patch (11th) implements an user space tool.  The 12th patch adds a
document for administrators of DAMON.

Next two patches are for tests.  The 13th and 14th patches provide unit tests
(based on kunit) and user space tests (based on kselftest), respectively.

Finally, the last patch (15th) updates the MAINTAINERS file.

The patches are based on the v5.5.  You can also clone the complete git
tree:

    $ git clone git://github.com/sjp38/linux -b damon/patches/v8

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon/patches/v8


Patch History
=============

Changes from v7
(https://lore.kernel.org/linux-mm/20200318112722.30143-1-sjpark@amazon.com/)
 - Cleanup variable names (Jonathan Cameron)
 - Split sampling address setup from access_check() (Jonathan Cameron)
 - Make sampling address to always locate in the region (Jonathan Cameron)
 - Make initial region's sampling addr to be old (Jonathan Cameron)
 - Split kdamond on/off function to seperate functions (Jonathan Cameron)
 - Fix wrong kernel doc comments (Jonathan Cameron)
 - Reset 'last_accessed' to false in kdamond_check_access() if necessary
 - Rebase on v5.6

Changes from v6
(https://lore.kernel.org/linux-mm/20200224123047.32506-1-sjpark@amazon.com/)
 - Wordsmith cover letter (Shakeel Butt)
 - Cleanup code and commit messages (Jonathan Cameron)
 - Avoid kthread_run() under spinlock critical section (Jonathan Cameron)
 - Use kthread_stop() (Jonathan Cameron)
 - Change tracepoint to trace regions (Jonathan Cameron)
 - Implement API from the beginning (Jonathan Cameron)
 - Fix typos (Jonathan Cameron)
 - Fix access checking to properly handle regions smaller than single page
   (Jonathan Cameron)
 - Add found typos to 'scripts/spelling.txt'
 - Add recent evaluation results including DAMON-based Operation Schemes

Changes from v5
(https://lore.kernel.org/linux-mm/20200217103110.30817-1-sjpark@amazon.com/)
 - Fix minor bugs (sampling, record attributes, debugfs and user space tool)
 - selftests: Add debugfs interface tests for the bugs
 - Modify the user space tool to use its self default values for parameters
 - Fix pmg huge page access check

Changes from v4
(https://lore.kernel.org/linux-mm/20200210144812.26845-1-sjpark@amazon.com/)
 - Add 'Reviewed-by' for the kunit tests patch (Brendan Higgins)
 - Make the unit test to depedns on 'DAMON=y' (Randy Dunlap and kbuild bot)
   Reported-by: kbuild test robot <lkp@intel.com>
 - Fix m68k module build issue
   Reported-by: kbuild test robot <lkp@intel.com>
 - Add selftests
 - Seperate patches for low level users from core logics for better reading
 - Clean up debugfs interface
 - Trivial nitpicks

Changes from v3
(https://lore.kernel.org/linux-mm/20200204062312.19913-1-sj38.park@gmail.com/)
 - Fix i386 build issue
   Reported-by: kbuild test robot <lkp@intel.com>
 - Increase the default size of the monitoring result buffer to 1 MiB
 - Fix misc bugs in debugfs interface

Changes from v2
(https://lore.kernel.org/linux-mm/20200128085742.14566-1-sjpark@amazon.com/)
 - Move MAINTAINERS changes to last commit (Brendan Higgins)
 - Add descriptions for kunittest: why not only entire mappings and what the 4
   input sets are trying to test (Brendan Higgins)
 - Remove 'kdamond_need_stop()' test (Brendan Higgins)
 - Discuss about the 'perf mem' and DAMON (Peter Zijlstra)
 - Make CV clearly say what it actually does (Peter Zijlstra)
 - Answer why new module (Qian Cai)
 - Diable DAMON by default (Randy Dunlap)
 - Change the interface: Seperate recording attributes
   (attrs, record, rules) and allow multiple kdamond instances
 - Implement kernel API interface

Changes from v1
(https://lore.kernel.org/linux-mm/20200120162757.32375-1-sjpark@amazon.com/)
 - Rebase on v5.5
 - Add a tracepoint for integration with other tracers (Kirill A. Shutemov)
 - document: Add more description for the user space tool (Brendan Higgins)
 - unittest: Improve readability (Brendan Higgins)
 - unittest: Use consistent name and helpers function (Brendan Higgins)
 - Update PG_Young to avoid reclaim logic interference (Yunjae Lee)

Changes from RFC
(https://lore.kernel.org/linux-mm/20200110131522.29964-1-sjpark@amazon.com/)
 - Specify an ambiguous plan of access pattern based mm optimizations
 - Support loadable module build
 - Cleanup code
