.. SPDX-License-Identifier: GPL-2.0

==========================
DAMON: Data Access MONitor
==========================

DAMON is a data access monitoring framework subsystem for the Linux kernel.
The core mechanisms of DAMON called 'region based sampling' and 'adaptive
regions adjustment' (refer to :doc:`mechanisms` for the detail) make it

 - *accurate* (The monitored information is useful for DRAM level memory
   management; It might not appropriate for Cache-level accuracy, though),
 - *ligfht-weight* (The monitoring overhead is low enough to be applied online
   while making no impact on the performance of the target workloads), and
 - *scalable* (The upper-bound of the instrumentation overhead is controllable
   regardless of the size of target workloads).

Using this framework, therefore, the kernel's core memory management mechanisms
including reclamation and THP can be optimized for better memory management.
The experimental memory management optimization works that incurring high
instrumentation overhead will be able to have another try.  In user space,
meanwhile, users who have some special workloads will be able to write
personalized tools or applications for deeper understanding and specialized
optimizations of their systems.

.. toctree::
   :maxdepth: 2

   start
   guide
   usage
   api
   faq
   mechanisms
   eval
   plans
