.. SPDX-License-Identifier: GPL-2.0

==========================
DAMON: Data Access MONitor
==========================

Optimal placement of each data object under accurate future data access pattern
predictions is one of the biggest goal of the memory management.  However, most
of the predictions rely on only coarse information such as LRU to keep the
instrumentation overhead minimal.  As a consequence, a number of memory
management optimization works consistently say the management is far from the
optimal.  However, a number of such optimization works have failed being
adopted into the mainline Linux kernel mainly due to the limitations of their
instrumentation mechanisms.

The limitations can be translated into below three requirements that essential
for the instrumentation mechanisms to be adopted in a wide range of production
environments.

- *accuracy*.  The instrumented information should be useful for the DRAM level
  memory management.  Cache-level accuracy is not required, though.
- *light-weight overhead*.  The instrumentation overhead should be low enough
  to be applied online while making no impact to the performance of the main
  workloads.
- *scalability*.  Envrionmental changes such as size of instrumentation target
  workloads should not be able to make the instrumentation overhead arbitrarily
  increase.

DAMON is a data access monitoring framework subsystem for the Linux kernel.
The core mechanisms of DAMON called 'region based sampling' and adaptive
regions adjustment' (refer to :doc:`mechanisms` for the detail), which are
designed for the three requirements, make it fulfill the requirements.  Using
this framework, therefore, the kernel's core memory management mechanisms
including reclmation and THP can be optimized for better memory management.
The above-mentioned memory management optimization works that have not merged
into mainline will be able to have another try.  In user space, meanwhile,
users who have some special workloads will be able to write personalized tools
or applications for more understanding and specialized optimizations using the
DAMON framework.

.. toctree::
   :maxdepth: 2

   start
   guide
   usage
   api
   faq
   mechanisms
   plans
