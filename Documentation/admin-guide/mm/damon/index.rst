.. SPDX-License-Identifier: GPL-2.0

==========================
DAMON: Data Access MONitor
==========================

The memory management is supposed to provide optimal placement of data objects
under accurate predictions of future data accesses.  The predictions should be
made on fine information about current accesses, but most of those are relying
on only coarse ones, to keep the instrumentation overhead minimal.  As a
consequence, a number of finer access information based memory management
optimization works consistently pointed out the predictions are far from
optimal.  That said, most of such optimization works have failed being merged
into the mainline Linux kernel mainly due to the high overhead of their data
access pattern instrumentation.

From here, we can list up below three requirements for the data access
information instrumentation that must be fulfilled to be adopted in a wide
range of production environments.

- *accuracy*.  The instrumented information should be useful for DRAM level
  memory management.  Cache-level accuracy would not highly required, though.
- *light-weight overhead*.  The instrumentation overhead should be low enough
  to be applied online while making no impact on the performance of the main
  workloads.
- *scalability*.  Environmental changes such as the size of instrumentation
  target workloads should not be able to make the instrumentation overhead
  arbitrarily increase.

DAMON is a data access monitoring framework subsystem for the Linux kernel.
The core mechanisms of DAMON called 'region based sampling' and adaptive
regions adjustment' (refer to :doc:`mechanisms` for the detail) make it fulfill
the requirements.  Using this framework, therefore, the kernel's core memory
management mechanisms including reclamation and THP can be optimized for better
memory management.  The above-mentioned memory management optimization works
that have not merged into the mainline will be able to have another try.  In
user space, meanwhile, users who have some special workloads will be able to
write personalized tools or applications for more understanding and specialized
optimizations of their systems using the DAMON as a framework.

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
