Appendix A: Related Works
=========================

There are a number of researches[1,2,3,4,5,6] optimizing memory management
mechanisms based on the actual memory access patterns that shows impressive
results.  However, most of those has no deep consideration about the monitoring
of the accesses itself.  Some of those focused on the overhead of the
monitoring, but does not consider the accuracy scalability[6] or has additional
dependencies[7].  Indeed, one recent research[5] about the proactive
reclamation has also proposed[8] to the kernel community but the monitoring
overhead was considered a main problem.

[1] Subramanya R Dulloor, Amitabha Roy, Zheguang Zhao, Narayanan Sundaram,
    Nadathur Satish, Rajesh Sankaran, Jeff Jackson, and Karsten Schwan. 2016.
    Data tiering in heterogeneous memory systems. In Proceedings of the 11th
    European Conference on Computer Systems (EuroSys). ACM, 15.
[2] Youngjin Kwon, Hangchen Yu, Simon Peter, Christopher J Rossbach, and Emmett
    Witchel. 2016. Coordinated and efficient huge page management with ingens.
    In 12th USENIX Symposium on Operating Systems Design and Implementation
    (OSDI).  705–721.
[3] Harald Servat, Antonio J Peña, Germán Llort, Estanislao Mercadal,
    HansChristian Hoppe, and Jesús Labarta. 2017. Automating the application
    data placement in hybrid memory systems. In 2017 IEEE International
    Conference on Cluster Computing (CLUSTER). IEEE, 126–136.
[4] Vlad Nitu, Boris Teabe, Alain Tchana, Canturk Isci, and Daniel Hagimont.
    2018. Welcome to zombieland: practical and energy-efficient memory
    disaggregation in a datacenter. In Proceedings of the 13th European
    Conference on Computer Systems (EuroSys). ACM, 16.
[5] Andres Lagar-Cavilla, Junwhan Ahn, Suleiman Souhlal, Neha Agarwal, Radoslaw
    Burny, Shakeel Butt, Jichuan Chang, Ashwin Chaugule, Nan Deng, Junaid
    Shahid, Greg Thelen, Kamil Adam Yurtsever, Yu Zhao, and Parthasarathy
    Ranganathan.  2019. Software-Defined Far Memory in Warehouse-Scale
    Computers.  In Proceedings of the 24th International Conference on
    Architectural Support for Programming Languages and Operating Systems
    (ASPLOS).  ACM, New York, NY, USA, 317–330.
    DOI:https://doi.org/10.1145/3297858.3304053
[6] Carl Waldspurger, Trausti Saemundsson, Irfan Ahmad, and Nohhyun Park.
    2017. Cache Modeling and Optimization using Miniature Simulations. In 2017
    USENIX Annual Technical Conference (ATC). USENIX Association, Santa
    Clara, CA, 487–498.
    https://www.usenix.org/conference/atc17/technical-sessions/
[7] Haojie Wang, Jidong Zhai, Xiongchao Tang, Bowen Yu, Xiaosong Ma, and
    Wenguang Chen. 2018. Spindle: Informed Memory Access Monitoring. In 2018
    USENIX Annual Technical Conference (ATC). USENIX Association, Boston, MA,
    561–574.  https://www.usenix.org/conference/atc18/presentation/wang-haojie
[8] Jonathan Corbet. 2019. Proactively reclaiming idle memory. (2019).
    https://lwn.net/Articles/787611/.


Appendix B: Limitations of Other Access Monitoring Techniques
=============================================================

The memory access instrumentation techniques which are applied to
many tools such as Intel PIN is essential for correctness required cases such
as memory access bug detections or cache level optimizations.  However, those
usually incur exceptionally high overhead which is unacceptable.

Periodic access checks based on access counting features (e.g., PTE Accessed
bits or PG_Idle flags) can reduce the overhead.  It sacrifies some of the
quality but it's still ok to many of this domain.  However, the overhead
arbitrarily increase as the size of the target workload grows.  Miniature-like
static region based sampling can set the upperbound of the overhead, but it
will now decrease the quality of the output as the size of the workload grows.

DAMON is another solution that overcomes the limitations.  It is 1) accurate
enough for this domain, 2) light-weight so that it can be applied online, and
3) allow users to set the upper-bound of the overhead, regardless of the size
of target workloads.  It is implemented as a simple and small kernel module to
support various users in both of the user space and the kernel space.  Refer to
'Evaluations' section below for detailed performance of DAMON.

For the goals, DAMON utilizes its two core mechanisms, which allows lightweight
overhead and high quality of output, repectively.  To show how DAMON promises
those, refer to 'Mechanisms of DAMON' section below.


Appendix C: Mechanisms of DAMON
===============================


Basic Access Check
------------------

DAMON basically reports what pages are how frequently accessed.  The report is
passed to users in binary format via a ``result file`` which users can set it's
path.  Note that the frequency is not an absolute number of accesses, but a
relative frequency among the pages of the target workloads.

Users can also control the resolution of the reports by setting two time
intervals, ``sampling interval`` and ``aggregation interval``.  In detail,
DAMON checks access to each page per ``sampling interval``, aggregates the
results (counts the number of the accesses to each page), and reports the
aggregated results per ``aggregation interval``.  For the access check of each
page, DAMON uses the Accessed bits of PTEs.

This is thus similar to the previously mentioned periodic access checks based
mechanisms, which overhead is increasing as the size of the target process
grows.


Region Based Sampling
---------------------

To avoid the unbounded increase of the overhead, DAMON groups a number of
adjacent pages that assumed to have same access frequencies into a region.  As
long as the assumption (pages in a region have same access frequencies) is
kept, only one page in the region is required to be checked.  Thus, for each
``sampling interval``, DAMON randomly picks one page in each region and clears
its Accessed bit.  After one more ``sampling interval``, DAMON reads the
Accessed bit of the page and increases the access frequency of the region if
the bit has set meanwhile.  Therefore, the monitoring overhead is controllable
by setting the number of regions.  DAMON allows users to set the minimal and
maximum number of regions for the trade-off.

Except the assumption, this is almost same with the above-mentioned
miniature-like static region based sampling.  In other words, this scheme
cannot preserve the quality of the output if the assumption is not guaranteed.


Adaptive Regions Adjustment
---------------------------

At the beginning of the monitoring, DAMON constructs the initial regions by
evenly splitting the memory mapped address space of the process into the
user-specified minimal number of regions.  In this initial state, the
assumption is normally not kept and thus the quality could be low.  To keep the
assumption as much as possible, DAMON adaptively merges and splits each region.
For each ``aggregation interval``, it compares the access frequencies of
adjacent regions and merges those if the frequency difference is small.  Then,
after it reports and clears the aggregated access frequency of each region, it
splits each region into two regions if the total number of regions is smaller
than the half of the user-specified maximum number of regions.

In this way, DAMON provides its best-effort quality and minimal overhead while
keeping the bounds users set for their trade-off.


Applying Dynamic Memory Mappings
--------------------------------

Only a number of small parts in the super-huge virtual address space of the
processes is mapped to physical memory and accessed.  Thus, tracking the
unmapped address regions is just wasteful.  However, tracking every memory
mapping change might incur an overhead.  For the reason, DAMON applies the
dynamic memory mapping changes to the tracking regions only for each of an
user-specified time interval (``regions update interval``).


Appendix D: Expected Use-cases
==============================

A straightforward usecase of DAMON would be the program behavior analysis.
With the DAMON output, users can confirm whether the program is running as
intended or not.  This will be useful for debuggings and tests of design
points.

The monitored results can also be useful for counting the dynamic working set
size of workloads.  For the administration of memory overcommitted systems or
selection of the environments (e.g., containers providing different amount of
memory) for your workloads, this will be useful.

If you are a programmer, you can optimize your program by managing the memory
based on the actual data access pattern.  For example, you can identify the
dynamic hotness of your data using DAMON and call ``mlock()`` to keep your hot
data in DRAM, or call ``madvise()`` with ``MADV_PAGEOUT`` to proactively
reclaim cold data.  Even though your program is guaranteed to not encounter
memory pressure, you can still improve the performance by applying the DAMON
outputs for call of ``MADV_HUGEPAGE`` and ``MADV_NOHUGEPAGE``.  More creative
optimizations would be possible.  Our evaluations of DAMON includes a
straightforward optimization using the ``mlock()``.  Please refer to the below
Evaluation section for more detail.

As DAMON incurs very low overhead, such optimizations can be applied not only
offline, but also online.  Also, there is no reason to limit such optimizations
to the user space.  Several parts of the kernel's memory management mechanisms
could be also optimized using DAMON. The reclamation, the THP (de)promotion
decisions, and the compaction would be such a candidates.  DAMON will continue
its development to be highly optimized for the online/in-kernel uses.


A Future Plan: Data Access Monitoring-based Operation Schemes
-------------------------------------------------------------

As described in the above section, DAMON could be helpful for actual access
based memory management optimizations.  Nevertheless, users who want to do such
optimizations should run DAMON, read the traced data (either online or
offline), analyze it, plan a new memory management scheme, and apply the new
scheme by themselves.  It must be easier than the past, but could still require
some level of efforts.  In its next development stage, DAMON will reduce some
of such efforts by allowing users to specify some access based memory
management rules for their specific processes.

Because this is just a plan, the specific interface is not fixed yet, but for
example, users will be allowed to write their desired memory management rules
to a special file in a DAMON specific format.  The rules will be something like
'if a memory region of size in a range is keeping a range of hotness for more
than a duration, apply specific memory management rule using madvise() or
mlock() to the region'.  For example, we can imagine rules like below:

    # format is: <min/max size> <min/max frequency (0-99)> <duration> <action>

    # if a region of a size keeps a very high access frequency for more than
    # 100ms, lock the region in the main memory (call mlock()). But, if the
    # region is larger than 500 MiB, skip it. The exception might be helpful
    # if the system has only, say, 600 MiB of DRAM, a region of size larger
    # than 600 MiB cannot be locked in the DRAM at all.
    na 500M 90 99 100ms mlock

    # if a region keeps a high access frequency for more than 100ms, put the
    # region on the head of the LRU list (call madvise() with MADV_WILLNEED).
    na na 80 90 100ms madv_willneed

    # if a region keeps a low access frequency for more than 100ms, put the
    # region on the tail of the LRU list (call madvise() with MADV_COLD).
    na na 10 20 100ms madv_cold

    # if a region keeps a very low access frequency for more than 100ms, swap
    # out the region immediately (call madvise() with MADV_PAGEOUT).
    na na 0 10 100ms madv_pageout

    # if a region of a size bigger than 2MB keeps a very high access frequency
    # for more than 100ms, let the region to use huge pages (call madvise()
    # with MADV_HUGEPAGE).
    2M na 90 99 100ms madv_hugepage

    # If a regions of a size bigger than > 2MB keeps no high access frequency
    # for more than 100ms, avoid the region from using huge pages (call
    # madvise() with MADV_NOHUGEPAGE).
    2M na 0 25 100ms madv_nohugepage

An RFC patchset for this is available:
https://lore.kernel.org/linux-mm/20200218085309.18346-1-sjpark@amazon.com/


Appendix E: Evaluations
=======================

Setup
-----

On my personal QEMU/KVM based virtual machine on an Intel i7 host machine
running Ubuntu 18.04, I measure runtime and consumed system memory while
running various realistic workloads with several configurations.  I use 13 and
12 workloads in PARSEC3[3] and SPLASH-2X[4] benchmark suites, respectively.  I
personally use another wrapper scripts[5] for setup and run of the workloads.
On top of this patchset, we also applied the DAMON-based operation schemes
patchset[6] for this evaluation.

Measurement
~~~~~~~~~~~

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.  To make results more stable, I repeat the runs 5 times
and average results.  You can get stdev, min, and max of the numbers among the
repeated runs in appendix below.

Configurations
~~~~~~~~~~~~~~

The configurations I use are as below.

orig: Linux v5.5 with 'madvise' THP policy
rec: 'orig' plus DAMON running with record feature
thp: same with 'orig', but use 'always' THP policy
ethp: 'orig' plus a DAMON operation scheme[6], 'efficient THP'
prcl: 'orig' plus a DAMON operation scheme, 'proactive reclaim[7]'

I use 'rec' for measurement of DAMON overheads to target workloads and system
memory.  The remaining configs including 'thp', 'ethp', and 'prcl' are for
measurement of DAMON monitoring accuracy.

'ethp' and 'prcl' is simple DAMON-based operation schemes developed for
proof of concepts of DAMON.  'ethp' reduces memory space waste of THP by using
DAMON for decision of promotions and demotion for huge pages, while 'prcl' is
as similar as the original work.  Those are implemented as below:

# format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>
# ethp: Use huge pages if a region >2MB shows >5% access rate, use regular
# pages if a region >2MB shows <5% access rate for >1 second
2M null    5 null    null null    hugepage
2M null    null 5    1s null      nohugepage

# prcl: If a region >4KB shows <5% access rate for >5 seconds, page out.
4K null    null 5    5s null      pageout

Note that both 'ethp' and 'prcl' are designed with my only straightforward
intuition, because those are for only proof of concepts and monitoring accuracy
of DAMON.  In other words, those are not for production.  For production use,
those should be tuned more.


[1] "Redis latency problems troubleshooting", https://redis.io/topics/latency
[2] "Disable Transparent Huge Pages (THP)",
    https://docs.mongodb.com/manual/tutorial/transparent-huge-pages/
[3] "The PARSEC Becnhmark Suite", https://parsec.cs.princeton.edu/index.htm
[4] "SPLASH-2x", https://parsec.cs.princeton.edu/parsec3-doc.htm#splash2x
[5] "parsec3_on_ubuntu", https://github.com/sjp38/parsec3_on_ubuntu
[6] "[RFC v4 0/7] Implement Data Access Monitoring-based Memory Operation
    Schemes",
    https://lore.kernel.org/linux-mm/20200303121406.20954-1-sjpark@amazon.com/
[7] "Proactively reclaiming idle memory", https://lwn.net/Articles/787611/


Results
-------

Below two tables show the measurement results.  The runtimes are in seconds
while the memory usages are in KiB.  Each configurations except 'orig' shows
its overhead relative to 'orig' in percent within parenthesises.

runtime                 orig     rec      (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
parsec3/blackscholes    107.594  107.956  (0.34)     106.750  (-0.78)    107.672  (0.07)     111.916  (4.02)    
parsec3/bodytrack       79.230   79.368   (0.17)     78.908   (-0.41)    79.705   (0.60)     80.423   (1.50)    
parsec3/canneal         142.831  143.810  (0.69)     123.530  (-13.51)   133.778  (-6.34)    144.998  (1.52)    
parsec3/dedup           11.986   11.959   (-0.23)    11.762   (-1.87)    12.028   (0.35)     13.313   (11.07)   
parsec3/facesim         210.125  209.007  (-0.53)    205.226  (-2.33)    207.766  (-1.12)    209.815  (-0.15)   
parsec3/ferret          191.601  191.177  (-0.22)    190.420  (-0.62)    191.775  (0.09)     192.638  (0.54)    
parsec3/fluidanimate    212.735  212.970  (0.11)     209.151  (-1.68)    211.904  (-0.39)    218.573  (2.74)    
parsec3/freqmine        291.225  290.873  (-0.12)    289.258  (-0.68)    289.884  (-0.46)    298.373  (2.45)    
parsec3/raytrace        118.289  119.586  (1.10)     119.045  (0.64)     119.064  (0.66)     137.919  (16.60)   
parsec3/streamcluster   323.565  328.168  (1.42)     279.565  (-13.60)   287.452  (-11.16)   333.244  (2.99)    
parsec3/swaptions       155.140  155.473  (0.21)     153.816  (-0.85)    156.423  (0.83)     156.237  (0.71)    
parsec3/vips            58.979   59.311   (0.56)     58.733   (-0.42)    59.005   (0.04)     61.062   (3.53)    
parsec3/x264            70.539   68.413   (-3.01)    64.760   (-8.19)    67.180   (-4.76)    68.103   (-3.45)   
splash2x/barnes         80.414   81.751   (1.66)     73.585   (-8.49)    80.232   (-0.23)    115.753  (43.95)   
splash2x/fft            33.902   34.111   (0.62)     24.228   (-28.53)   29.926   (-11.73)   44.438   (31.08)   
splash2x/lu_cb          85.556   86.001   (0.52)     84.538   (-1.19)    86.000   (0.52)     91.447   (6.89)    
splash2x/lu_ncb         93.399   93.652   (0.27)     90.463   (-3.14)    94.008   (0.65)     93.901   (0.54)    
splash2x/ocean_cp       45.253   45.191   (-0.14)    43.049   (-4.87)    44.022   (-2.72)    46.588   (2.95)    
splash2x/ocean_ncp      86.927   87.065   (0.16)     50.747   (-41.62)   86.855   (-0.08)    199.553  (129.57)  
splash2x/radiosity      91.433   91.511   (0.09)     90.626   (-0.88)    91.865   (0.47)     104.524  (14.32)   
splash2x/radix          31.923   32.023   (0.31)     25.194   (-21.08)   32.035   (0.35)     39.231   (22.89)   
splash2x/raytrace       84.367   84.677   (0.37)     82.417   (-2.31)    83.505   (-1.02)    84.857   (0.58)    
splash2x/volrend        87.499   87.495   (-0.00)    86.775   (-0.83)    87.311   (-0.21)    87.511   (0.01)    
splash2x/water_nsquared 236.397  236.759  (0.15)     219.902  (-6.98)    224.228  (-5.15)    238.562  (0.92)    
splash2x/water_spatial  89.646   89.767   (0.14)     89.735   (0.10)     90.347   (0.78)     103.585  (15.55)   
total                   3020.570 3028.080 (0.25)     2852.190 (-5.57)    2953.960 (-2.21)    3276.550 (8.47)    


memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    1785916.600  1834201.400  (2.70)     1826249.200  (2.26)     1828079.200  (2.36)     1712210.600  (-4.13)   
parsec3/bodytrack       1415049.400  1434317.600  (1.36)     1423715.000  (0.61)     1430392.600  (1.08)     1435136.000  (1.42)    
parsec3/canneal         1043489.800  1058617.600  (1.45)     1040484.600  (-0.29)    1048664.800  (0.50)     1050280.000  (0.65)    
parsec3/dedup           2414453.200  2458493.200  (1.82)     2411379.400  (-0.13)    2400516.000  (-0.58)    2461120.800  (1.93)    
parsec3/facesim         541597.200   550097.400   (1.57)     544364.600   (0.51)     553240.000   (2.15)     552316.400   (1.98)    
parsec3/ferret          317986.600   332346.000   (4.52)     320218.000   (0.70)     331085.000   (4.12)     330895.200   (4.06)    
parsec3/fluidanimate    576183.400   585442.000   (1.61)     577780.200   (0.28)     587703.400   (2.00)     506501.000   (-12.09)  
parsec3/freqmine        990869.200   997817.000   (0.70)     990350.400   (-0.05)    997669.000   (0.69)     763325.800   (-22.96)  
parsec3/raytrace        1748370.800  1757109.200  (0.50)     1746153.800  (-0.13)    1757830.400  (0.54)     1581455.800  (-9.55)   
parsec3/streamcluster   121521.800   140452.400   (15.58)    129725.400   (6.75)     132266.000   (8.84)     130558.200   (7.44)    
parsec3/swaptions       15592.400    29018.800    (86.11)    14765.800    (-5.30)    27260.200    (74.83)    26631.600    (70.80)   
parsec3/vips            2957567.600  2967993.800  (0.35)     2956623.200  (-0.03)    2973062.600  (0.52)     2951402.000  (-0.21)   
parsec3/x264            3169012.400  3175048.800  (0.19)     3190345.400  (0.67)     3189353.000  (0.64)     3172924.200  (0.12)    
splash2x/barnes         1209066.000  1213125.400  (0.34)     1217261.400  (0.68)     1209661.600  (0.05)     921041.800   (-23.82)  
splash2x/fft            9359313.200  9195213.000  (-1.75)    9377562.400  (0.19)     9050957.600  (-3.29)    9517977.000  (1.70)    
splash2x/lu_cb          514966.200   522939.400   (1.55)     520870.400   (1.15)     522635.000   (1.49)     329933.600   (-35.93)  
splash2x/lu_ncb         514180.400   525974.800   (2.29)     521420.200   (1.41)     521063.600   (1.34)     523557.000   (1.82)    
splash2x/ocean_cp       3346493.400  3288078.000  (-1.75)    3382253.800  (1.07)     3289477.600  (-1.70)    3260810.400  (-2.56)   
splash2x/ocean_ncp      3909966.400  3882968.800  (-0.69)    7037196.000  (79.98)    4046363.400  (3.49)     3471452.400  (-11.22)  
splash2x/radiosity      1471119.400  1470626.800  (-0.03)    1482604.200  (0.78)     1472718.400  (0.11)     546893.600   (-62.82)  
splash2x/radix          1748360.800  1729163.400  (-1.10)    1371463.200  (-21.56)   1701993.600  (-2.65)    1817519.600  (3.96)    
splash2x/raytrace       46670.000    60172.200    (28.93)    51901.600    (11.21)    60782.600    (30.24)    52644.800    (12.80)   
splash2x/volrend        150666.600   167444.200   (11.14)    151335.200   (0.44)     163345.000   (8.41)     162760.000   (8.03)    
splash2x/water_nsquared 45720.200    59422.400    (29.97)    46031.000    (0.68)     61801.400    (35.17)    62627.000    (36.98)   
splash2x/water_spatial  663052.200   672855.800   (1.48)     665787.600   (0.41)     674696.200   (1.76)     471052.600   (-28.96)  
total                   40077300.000 40108900.000 (0.08)     42997900.000 (7.29)     40032700.000 (-0.11)    37813000.000 (-5.65)   


DAMON Overheads
~~~~~~~~~~~~~~~

In total, DAMON recording feature incurs 0.25% runtime overhead (up to 1.66% in
worst case with 'splash2x/barnes') and 0.08% memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  In detail, 16%, 86%, 29%,
11%, and 30% overheads shown for parsec3/streamcluster (125 MiB),
parsec3/swaptions (15 MiB), splash2x/raytrace (45 MiB), splash2x/volrend (151
MiB), and splash2x/water_nsquared (46 MiB)).  Nonetheless, the overheads are
not from DAMON, but from the wrapper, and thus should be ignored.  This fake
memory overhead continues in 'ethp' and 'prcl', as those configurations are
also using the Python wrapper.


Efficient THP
~~~~~~~~~~~~~

THP 'always' enabled policy achieves 5.57% speedup but incurs 7.29% memory
overhead.  It achieves 41.62% speedup in best case, but 79.98% memory overhead
in worst case.  Interestingly, both the best and worst case are with
'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.21% speedup and -0.11% memory overhead.  In other words, 'ethp' removes
100% of THP memory waste while preserving 39.67% of THP speedup in total.


Proactive Reclamation
~~~~~~~~~~~~~~~~~~~~

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
8.47% runtime overhead in total while achieving 5.65% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads:

rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    592502.000   589764.400   (-0.46)    592132.600   (-0.06)    593702.000   (0.20)     406639.400   (-31.37)  
parsec3/bodytrack       32365.400    32195.000    (-0.53)    32210.800    (-0.48)    32114.600    (-0.77)    21537.600    (-33.45)  
parsec3/canneal         839904.200   840292.200   (0.05)     836866.400   (-0.36)    838263.200   (-0.20)    837895.800   (-0.24)   
parsec3/dedup           1208337.200  1218465.600  (0.84)     1233278.600  (2.06)     1200490.200  (-0.65)    882911.400   (-26.93)  
parsec3/facesim         311380.800   311363.600   (-0.01)    315642.600   (1.37)     312573.400   (0.38)     310257.400   (-0.36)   
parsec3/ferret          99514.800    99542.000    (0.03)     100454.200   (0.94)     99879.800    (0.37)     89679.200    (-9.88)   
parsec3/fluidanimate    531760.800   531735.200   (-0.00)    531865.400   (0.02)     531940.800   (0.03)     440781.000   (-17.11)  
parsec3/freqmine        552455.400   552882.600   (0.08)     555793.600   (0.60)     553019.800   (0.10)     58067.000    (-89.49)  
parsec3/raytrace        894798.400   894953.400   (0.02)     892223.400   (-0.29)    893012.400   (-0.20)    315259.800   (-64.77)  
parsec3/streamcluster   110780.400   110856.800   (0.07)     110954.000   (0.16)     111310.800   (0.48)     108066.800   (-2.45)   
parsec3/swaptions       5614.600     5645.600     (0.55)     5553.200     (-1.09)    5552.600     (-1.10)    3251.800     (-42.08)  
parsec3/vips            31942.200    31752.800    (-0.59)    32042.600    (0.31)     32226.600    (0.89)     29012.200    (-9.17)   
parsec3/x264            81770.800    81609.200    (-0.20)    82800.800    (1.26)     82612.200    (1.03)     81805.800    (0.04)    
splash2x/barnes         1216515.600  1217113.800  (0.05)     1225605.600  (0.75)     1217325.000  (0.07)     540108.400   (-55.60)  
splash2x/fft            9668660.600  9751350.800  (0.86)     9773806.400  (1.09)     9613555.400  (-0.57)    7951241.800  (-17.76)  
splash2x/lu_cb          510368.800   510095.800   (-0.05)    514350.600   (0.78)     510276.000   (-0.02)    311584.800   (-38.95)  
splash2x/lu_ncb         509904.800   510001.600   (0.02)     513847.000   (0.77)     510073.400   (0.03)     509905.600   (0.00)    
splash2x/ocean_cp       3389550.600  3404466.000  (0.44)     3443363.600  (1.59)     3410388.000  (0.61)     3330608.600  (-1.74)   
splash2x/ocean_ncp      3923723.200  3911148.200  (-0.32)    7175800.400  (82.88)    4104482.400  (4.61)     2030525.000  (-48.25)  
splash2x/radiosity      1472994.600  1475946.400  (0.20)     1485636.800  (0.86)     1476193.000  (0.22)     262161.400   (-82.20)  
splash2x/radix          1750329.800  1765697.000  (0.88)     1413304.000  (-19.25)   1754154.400  (0.22)     1516142.600  (-13.38)  
splash2x/raytrace       23149.600    23208.000    (0.25)     28574.400    (23.43)    26694.600    (15.31)    16257.800    (-29.77)  
splash2x/volrend        43968.800    43919.000    (-0.11)    44087.600    (0.27)     44224.000    (0.58)     32484.400    (-26.12)  
splash2x/water_nsquared 29348.000    29338.400    (-0.03)    29604.600    (0.87)     29779.400    (1.47)     23644.800    (-19.43)  
splash2x/water_spatial  655263.600   655097.800   (-0.03)    655199.200   (-0.01)    656282.400   (0.16)     379816.800   (-42.04)  
total                   28486900.000 28598400.000 (0.39)     31625000.000 (11.02)    28640100.000 (0.54)     20489600.000 (-28.07)  

In total, 28.07% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 22.96% of system memory usage and 89.49%
of residential sets while incurring only 2.45% runtime overhead.


Appendix F: Prototype Evaluations
=================================

A prototype of DAMON has evaluated on an Intel Xeon E7-8837 machine using 20
benchmarks that picked from SPEC CPU 2006, NAS, Tensorflow Benchmark,
SPLASH-2X, and PARSEC 3 benchmark suite.  Nonethless, this section provides
only summary of the results.  For more detail, please refer to the slides used
for the introduction of DAMON at the Linux Plumbers Conference 2019[1] or the
MIDDLEWARE'19 industrial track paper[2].

[1] SeongJae Park, Tracing Data Access Pattern with Bounded Overhead and
    Best-effort Accuracy. In The Linux Kernel Summit, September 2019.
    https://linuxplumbersconf.org/event/4/contributions/548/
[2] SeongJae Park, Yunjae Lee, Heon Y. Yeom, Profiling Dynamic Data Access
    Patterns with Controlled Overhead and Quality. In 20th ACM/IFIP
    International Middleware Conference Industry, December 2019.
    https://dl.acm.org/doi/10.1145/3366626.3368125


Quality
-------

We first traced and visualized the data access pattern of each workload.  We
were able to confirm that the visualized results are reasonably accurate by
manually comparing those with the source code of the workloads.

To see the usefulness of the monitoring, we optimized 9 memory intensive
workloads among them for memory pressure situations using the DAMON outputs.
In detail, we identified frequently accessed memory regions in each workload
based on the DAMON results and protected them with ``mlock()`` system calls by
manually modifying the source code.  The optimized versions consistently show
speedup (2.55x in best case, 1.65x in average) under artificial memory
pressures.  We use cgroups for the pressure.


Overhead
--------

We also measured the overhead of DAMON.  The upperbound we set was kept as
expected.  Besides, it was much lower (0.6 percent of the bound in best case,
13.288 percent of the bound in average).  This reduction of the overhead is
mainly resulted from its core mechanism called adaptive regions adjustment.
Refer to 'Appendix D' for more detail about the mechanism.  We also compared
the overhead of DAMON with that of a straightforward periodic PTE Accessed bit
checking based monitoring.  DAMON's overhead was smaller than it by 94,242.42x
in best case, 3,159.61x in average.

The latest version of DAMON running with its default configuration consumes
only up to 1% of CPU time when applied to realistic workloads in PARSEC3 and
SPLASH-2X and makes no visible slowdown to the target processes.
