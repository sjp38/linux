L1D Flushing for the paranoid
=============================

With an increasing number of vulnerabilities being reported around data
leaks from the Level 1 Data cache (L1D) the kernel provides an opt-in
mechanism to flush the L1D cache on context switch.

This mechanism can be used to address e.g. CVE-2020-0550. For paranoid
applications the mechanism keeps them safe from any yet to be discovered
vulnerabilities, related to leaks from the L1D cache.


Related CVEs
------------
At the present moment, the following CVEs can be addressed by this
mechanism

    =============       ========================     ==================
    CVE-2020-0550       Improper Data Forwarding     OS related aspects
    =============       ========================     ==================

Usage Guidelines
----------------

Please see document: :ref:`Documentation/userspace-api/spec_ctrl.rst` for
details.

**NOTE**: The feature is disabled by default, applications need to
specifically opt into the feature to enable it.

Mitigation
----------

When PR_SET_L1D_FLUSH is enabled for a task a flush of the L1D cache is
performed when the task is scheduled out and the incoming task belongs to a
different process and therefore to a different address space.

If the underlying CPU supports L1D flushing in hardware, the hardware
mechanism is used, otherwise a software fallback, similar to the L1TF
mitigation, is invoked.

Limitations
-----------

The mechanism does not mitigate L1D data leaks between tasks belonging to
different processes which are concurrently executing on sibling threads of
a physical CPU core when SMT is enabled on the system.

This can be addressed by controlled placement of processes on physical CPU
cores or by disabling SMT. See the relevant chapter in the L1TF mitigation
document: :ref:`Documentation/admin-guide/hw-vuln/l1tf.rst <smt_control>`.
