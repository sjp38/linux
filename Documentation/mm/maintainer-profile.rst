.. SPDX-License-Identifier: GPL-2.0

Memory Management Maintainer Entry Profile
==========================================

The memory management (mm) subsystem covers the files that are listed in
'MEMORY MANAGEMENT' and related section of 'MAINTAINERS' file.

The mailing lists for the subsystem is linux-mm@kvack.org.
Patches should be made against the `mm-unstable tree
<https://git.kernel.org/akpm/mm/h/mm-unstable>` or `mm-new tree
<https://git.kernel.org/akpm/mm/h/mm-new>`_ whenever possible and posted to the
mailing lists.

SCM Trees
---------

- mm-new: for review
- mm-unstable: for test
- mm-stable: for PR
- mm-hotfixes-unstable: hotfixes for test and review
- mm-hotfixes-stable: for PR

Submit checklist addendum
-------------------------

- New thread for new revision.  Don't reply to previous version
- Simple fixups as reply.
- Two tabs on second and later parameter lines

Key cycle dates
---------------

Assume some subsystems may ignore patches posted during the merge window.

Review cadence
--------------

The DAMON maintainer does the work on the usual work hour (09:00 to 17:00,
Mon-Fri) in PT (Pacific Time).  The response to patches will occasionally be
slow.  Do not hesitate to send a ping if you have not heard back within a week
of sending a patch.

Mailing tool
------------

Like many other Linux kernel subsystems, DAMON uses the mailing lists
(damon@lists.linux.dev and linux-mm@kvack.org) as the major communication
channel.  There is a simple tool called `HacKerMaiL
<https://github.com/damonitor/hackermail>`_ (``hkml``), which is for people who
are not very familiar with the mailing lists based communication.  The tool
could be particularly helpful for DAMON community members since it is developed
and maintained by DAMON maintainer.  The tool is also officially announced to
support DAMON and general Linux kernel development workflow.

In other words, `hkml <https://github.com/damonitor/hackermail>`_ is a mailing
tool for DAMON community, which DAMON maintainer is committed to support.
Please feel free to try and report issues or feature requests for the tool to
the maintainer.
