Implement Data Access Monitoring-based Memory Operation Schemes

Changes from Previous Version (RFC[1])
======================================

- Rebase on latest -mm tree

[1] https://lore.kernel.org/linux-mm/20201216084404.23183-1-sjpark@amazon.com/

Introduction
============

DAMON[1] can be used as a primitive for data access aware memory management
optimizations.  For that, users who want such optimizations should run DAMON,
read the monitoring results, analyze it, plan a new memory management scheme,
and apply the new scheme by themselves.  Such efforts will be inevitable for
some complicated optimizations.

However, in many other cases, the users would simply want the system to apply a
memory management action to a memory region of a specific size having a
specific access frequency for a specific time.  For example, "page out a memory
region larger than 100 MiB keeping only rare accesses more than 2 minutes", or
"Do not use THP for a memory region larger than 2 MiB rarely accessed for more
than 1 seconds".

To make the works easier and non-redundant, this patchset implements a new
feature of DAMON, which is called Data Access Monitoring-based Operation
Schemes (DAMOS).  Using the feature, users can describe the normal schemes in a
simple way and ask DAMON to execute those on its own.

[1] https://damonitor.github.io

Evaluations
===========

DAMOS is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 76.15% of
THP memory overheads while preserving 51.25% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 93.38% of residential sets and 23.63% of system memory footprint while
incurring only 1.22% runtime overhead in the best case (parsec3/freqmine).

NOTE that the experimental THP optimization and proactive reclamation are not
for production but only for proof of concepts.

Please refer to the showcase web site's evaluation document[1] for detailed
evaluation setup and results.

[1] https://damonitor.github.io/doc/html/v34/vm/damon/eval.html

Baseline and Complete Git Tree
==============================

The patches are based on the latest -mm tree
(v5.15-rc3-mmots-2021-09-30-19-38)[1].  You can also clone the complete git
tree:

    $ git clone git://git.kernel.org/pub/scm/linux/kernel/git/sj/linux.git -b damos/patches/v1

The web is also available:
https://git.kernel.org/pub/scm/linux/kernel/git/sj/linux.git/tag/?h=damos/patches/v1

[1] https://github.com/hnaz/linux-mm/tree/v5.15-rc3-mmots-2021-09-30-19-38

Development Trees
-----------------

There are a couple of trees for entire DAMON patchset series.

- For latest release: https://git.kernel.org/sj/h/damon/master
- For next release: https://git.kernel.org/sj/h/damon/next

Long-term Support Trees
-----------------------

For people who want to test DAMON but using LTS kernels, there are another
couple of trees based on two latest LTS kernels respectively and containing the
'damon/master' backports.

- For v5.4.y: https://git.kernel.org/sj/h/damon/for-v5.4.y
- For v5.10.y: https://git.kernel.org/sj/h/damon/for-v5.10.y

Sequence Of Patches
===================

The 1st patch accounts age of each region.  The 2nd patch implements the core
of the DAMON-based operation schemes feature.  The 3rd patch makes the default
monitoring primitives for virtual address spaces to support the schemes.  From
this point, the kernel space users can use DAMOS.  The 4th patch exports the
feature to the user space via the debugfs interface.  The 5th patch implements
schemes statistics feature for easier tuning of the schemes and runtime access
pattern analysis, and the 6th patch adds selftests for these changes.  Finally,
the 7th patch documents this new feature.

Patch History
=============

Changes from RFC v15.1
(https://lore.kernel.org/linux-mm/20201216084404.23183-1-sjpark@amazon.com/)
- Rebase on latest -mm tree

Please refer to RFC v15.1 for previous history.
