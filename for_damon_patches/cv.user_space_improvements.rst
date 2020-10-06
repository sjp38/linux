Subject: DAMON: Improve User Space Interface

NOTE: This is only an RFC for future features of DAMON patchset[1], which is
not merged in the mainline yet.  The aim of this RFC is to show how DAMON would
be evolved once it is merged in.  So, if you have some interest in this RFC,
please consider reviewing the DAMON patchset, either.

After posting DAMON patchset[1], we received a number of comments.  Based on
those, we listed and shared future works for DAMON in the kernel summit
2020[2] and had a poll for the priorities of the works.  As a result, the user
space interface improvement received a second highest priority[3].  For the
reason, this patchset is came out.

The 1st patch puts more information in the monitoring thread name so that user
space could charge the DAMON's CPU usage on them by themselves, in fine
granularity.  The 2nd patch makes multiple monitoring contexts available using
the debugfs interface.

[1] https://lore.kernel.org/linux-mm/20200817105137.19296-1-sjpark@amazon.com/
[2] https://linuxplumbersconf.org/event/7/contributions/659/
[3] https://lore.kernel.org/linux-mm/20200831112235.2675-1-sjpark@amazon.com/

Baseline and Complete Git Trees
===============================

The patches are based on the v5.8 plus DAMON v20 patchset[1], RFC v14 of DAMOS
patchset, RFC v8 of physical address space support patchset, and some more
trivial fixes (s/snprintf/scnprintf).  You can also clone the complete git
tree:

    $ git clone git://github.com/sjp38/linux -b damon-usi/rfc/v1

The web is also available:
https://github.com/sjp38/linux/releases/tag/damon-usi/rfc/v1


[1] https://lore.kernel.org/linux-mm/20200817105137.19296-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200804142430.15384-1-sjpark@amazon.com/
[3] https://lore.kernel.org/linux-mm/20200831104730.28970-1-sjpark@amazon.com/
