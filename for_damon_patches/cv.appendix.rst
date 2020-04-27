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
its development to be highly optimized for the online/in-kernel uses.  We will
further automate the optimization for many usecases.


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
4K null    null 5    500ms null      pageout

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
parsec3/blackscholes    107.755  106.693  (-0.99)    106.408  (-1.25)    107.848  (0.09)     112.142  (4.07)
parsec3/bodytrack       79.603   79.110   (-0.62)    78.862   (-0.93)    79.577   (-0.03)    80.579   (1.23)
parsec3/canneal         139.588  139.148  (-0.31)    125.747  (-9.92)    130.833  (-6.27)    157.601  (12.90)
parsec3/dedup           11.923   11.860   (-0.53)    11.739   (-1.55)    11.931   (0.06)     13.090   (9.78)
parsec3/facesim         208.270  208.401  (0.06)     205.557  (-1.30)    206.114  (-1.04)    216.352  (3.88)
parsec3/ferret          190.247  190.540  (0.15)     191.056  (0.43)     190.492  (0.13)     193.026  (1.46)
parsec3/fluidanimate    210.495  212.142  (0.78)     210.075  (-0.20)    211.365  (0.41)     220.724  (4.86)
parsec3/freqmine        287.887  292.770  (1.70)     287.576  (-0.11)    289.190  (0.45)     296.266  (2.91)
parsec3/raytrace        117.887  119.385  (1.27)     118.781  (0.76)     118.572  (0.58)     129.831  (10.13)
parsec3/streamcluster   321.637  327.692  (1.88)     283.875  (-11.74)   291.699  (-9.31)    329.212  (2.36)
parsec3/swaptions       154.148  155.623  (0.96)     155.070  (0.60)     154.952  (0.52)     155.241  (0.71)
parsec3/vips            58.851   58.527   (-0.55)    58.396   (-0.77)    58.979   (0.22)     59.970   (1.90)
parsec3/x264            70.559   68.624   (-2.74)    66.662   (-5.52)    67.817   (-3.89)    71.065   (0.72)
splash2x/barnes         80.678   80.491   (-0.23)    74.135   (-8.11)    79.493   (-1.47)    98.688   (22.32)
splash2x/fft            33.565   33.434   (-0.39)    23.153   (-31.02)   31.181   (-7.10)    45.662   (36.04)
splash2x/lu_cb          85.536   85.391   (-0.17)    84.396   (-1.33)    86.323   (0.92)     89.000   (4.05)
splash2x/lu_ncb         92.899   92.830   (-0.07)    90.075   (-3.04)    93.566   (0.72)     95.603   (2.91)
splash2x/ocean_cp       44.529   44.741   (0.47)     43.049   (-3.32)    44.117   (-0.93)    57.652   (29.47)
splash2x/ocean_ncp      81.271   81.538   (0.33)     51.337   (-36.83)   62.990   (-22.49)   137.621  (69.34)
splash2x/radiosity      91.411   91.329   (-0.09)    90.889   (-0.57)    91.944   (0.58)     102.682  (12.33)
splash2x/radix          31.194   31.202   (0.03)     25.258   (-19.03)   28.667   (-8.10)    43.684   (40.04)
splash2x/raytrace       83.930   84.754   (0.98)     83.734   (-0.23)    83.394   (-0.64)    84.932   (1.19)
splash2x/volrend        86.163   87.052   (1.03)     86.918   (0.88)     86.621   (0.53)     87.520   (1.57)
splash2x/water_nsquared 231.335  234.050  (1.17)     222.722  (-3.72)    224.502  (-2.95)    236.589  (2.27)
splash2x/water_spatial  88.753   89.167   (0.47)     89.542   (0.89)     89.510   (0.85)     97.960   (10.37)
total                   2990.130 3006.480 (0.55)     2865.010 (-4.18)    2921.670 (-2.29)    3212.680 (7.44)


memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    1816303.000  1835404.800  (1.05)     1825285.800  (0.49)     1827203.000  (0.60)     1641411.600  (-9.63)
parsec3/bodytrack       1413888.000  1435353.800  (1.52)     1418535.200  (0.33)     1423560.600  (0.68)     1449993.600  (2.55)
parsec3/canneal         1042149.000  1053590.600  (1.10)     1038469.400  (-0.35)    1051556.600  (0.90)     1044271.200  (0.20)
parsec3/dedup           2364713.400  2448044.200  (3.52)     2397824.600  (1.40)     2427849.200  (2.67)     2402863.000  (1.61)
parsec3/facesim         540004.800   554035.000   (2.60)     543449.800   (0.64)     553955.400   (2.58)     483559.400   (-10.45)
parsec3/ferret          319349.600   331756.400   (3.89)     319751.600   (0.13)     333884.000   (4.55)     329600.400   (3.21)
parsec3/fluidanimate    576741.400   587662.400   (1.89)     576208.000   (-0.09)    586089.800   (1.62)     489655.000   (-15.10)
parsec3/freqmine        986222.400   999265.800   (1.32)     987716.200   (0.15)     1001756.400  (1.58)     766269.800   (-22.30)
parsec3/raytrace        1748338.200  1750036.000  (0.10)     1742218.400  (-0.35)    1755005.000  (0.38)     1584009.400  (-9.40)
parsec3/streamcluster   134980.800   136257.600   (0.95)     119580.000   (-11.41)   135188.600   (0.15)     132589.600   (-1.77)
parsec3/swaptions       13893.800    28265.000    (103.44)   16206.000    (16.64)    27826.800    (100.28)   26332.800    (89.53)
parsec3/vips            2954105.600  2972710.000  (0.63)     2955940.200  (0.06)     2971989.600  (0.61)     2968768.600  (0.50)
parsec3/x264            3169214.400  3206571.400  (1.18)     3185179.200  (0.50)     3170560.000  (0.04)     3209772.400  (1.28)
splash2x/barnes         1213585.000  1211837.400  (-0.14)    1220890.600  (0.60)     1215453.600  (0.15)     974635.600   (-19.69)
splash2x/fft            9371991.000  9201587.200  (-1.82)    9292089.200  (-0.85)    9108707.400  (-2.81)    9625476.600  (2.70)
splash2x/lu_cb          515113.800   523791.000   (1.68)     520880.200   (1.12)     523066.800   (1.54)     362113.400   (-29.70)
splash2x/lu_ncb         514847.800   524934.000   (1.96)     521362.400   (1.27)     521515.600   (1.30)     445374.200   (-13.49)
splash2x/ocean_cp       3341933.600  3322040.400  (-0.60)    3381251.000  (1.18)     3292229.400  (-1.49)    3181383.000  (-4.80)
splash2x/ocean_ncp      3899426.800  3870830.800  (-0.73)    7065641.200  (81.20)    5099403.200  (30.77)    3557460.000  (-8.77)
splash2x/radiosity      1465960.800  1470778.600  (0.33)     1482777.600  (1.15)     1500133.400  (2.33)     498807.200   (-65.97)
splash2x/radix          1711100.800  1672141.400  (-2.28)    1387826.200  (-18.89)   1516728.600  (-11.36)   2043053.600  (19.40)
splash2x/raytrace       47586.400    58698.000    (23.35)    51308.400    (7.82)     61274.800    (28.77)    54446.200    (14.42)
splash2x/volrend        150480.400   164633.800   (9.41)     150819.600   (0.23)     163517.400   (8.66)     161828.200   (7.54)
splash2x/water_nsquared 47147.600    62403.400    (32.36)    47689.600    (1.15)     60030.800    (27.33)    59736.600    (26.70)
splash2x/water_spatial  666544.600   674447.800   (1.19)     665904.600   (-0.10)    673677.600   (1.07)     559765.200   (-16.02)
total                   40025500.000 40096900.000 (0.18)     42914900.000 (7.22)     41002100.000 (2.44)     38053200.000 (-4.93)


DAMON Overheads
~~~~~~~~~~~~~~~

In total, DAMON recording feature incurs 0.55% runtime overhead (up to 1.88% in
worst case with 'parsec3/streamcluster') and 0.18% memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  In detail, 103%, 23%, 9%,
and 32% overheads shown for parsec3/swaptions (15 MiB), splash2x/raytrace (45
MiB), splash2x/volrend (151 MiB), and splash2x/water_nsquared (46 MiB)).
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
~~~~~~~~~~~~~

THP 'always' enabled policy achieves 4.18% speedup but incurs 7.22% memory
overhead.  It achieves 36.83% speedup in best case, but 81.20% memory overhead
in worst case.  Interestingly, both the best and worst case are with
'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.29% speedup and 2.44% memory overhead.  In other words, 'ethp' removes
66.2% of THP memory waste while preserving 54.78% of THP speedup in total.  In
case of the 'splash2x/ocean_ncp', 'ethp' removes 62.10% of THP memory waste
while preserving 61% of THP speedup.


Proactive Reclamation
~~~~~~~~~~~~~~~~~~~~

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
7.44% runtime overhead in total while achieving 4.93% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads:

rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    591461.000   590761.000   (-0.12)    592669.200   (0.20)     592442.600   (0.17)     308627.200   (-47.82)
parsec3/bodytrack       32201.400    32242.800    (0.13)     32299.000    (0.30)     32327.600    (0.39)     27411.000    (-14.88)
parsec3/canneal         841593.600   839721.400   (-0.22)    837427.600   (-0.50)    838363.400   (-0.38)    822220.600   (-2.30)
parsec3/dedup           1210000.600  1235153.600  (2.08)     1205207.200  (-0.40)    1229808.800  (1.64)     827881.400   (-31.58)
parsec3/facesim         311630.400   311273.200   (-0.11)    314747.400   (1.00)     312449.400   (0.26)     184104.600   (-40.92)
parsec3/ferret          99714.800    99558.400    (-0.16)    100996.800   (1.29)     99769.600    (0.05)     88979.200    (-10.77)
parsec3/fluidanimate    531429.600   531855.200   (0.08)     531744.800   (0.06)     532158.600   (0.14)     428154.000   (-19.43)
parsec3/freqmine        553063.600   552561.000   (-0.09)    556588.600   (0.64)     553518.000   (0.08)     65516.800    (-88.15)
parsec3/raytrace        894129.800   894332.400   (0.02)     889421.800   (-0.53)    892801.000   (-0.15)    363634.000   (-59.33)
parsec3/streamcluster   110887.200   110949.400   (0.06)     111508.400   (0.56)     111645.000   (0.68)     109921.200   (-0.87)
parsec3/swaptions       5688.600     5660.800     (-0.49)    5656.400     (-0.57)    5709.200     (0.36)     4201.000     (-26.15)
parsec3/vips            31774.800    31992.000    (0.68)     32134.800    (1.13)     32212.400    (1.38)     29026.000    (-8.65)
parsec3/x264            81897.400    81842.200    (-0.07)    83073.800    (1.44)     82435.200    (0.66)     80929.400    (-1.18)
splash2x/barnes         1216429.200  1212158.000  (-0.35)    1223021.400  (0.54)     1218261.200  (0.15)     710678.800   (-41.58)
splash2x/fft            9582824.800  9732597.400  (1.56)     9695113.400  (1.17)     9665607.200  (0.86)     7959449.000  (-16.94)
splash2x/lu_cb          509782.600   509423.400   (-0.07)    514467.000   (0.92)     510521.000   (0.14)     346267.200   (-32.08)
splash2x/lu_ncb         509735.200   510578.000   (0.17)     513892.200   (0.82)     509864.800   (0.03)     429509.800   (-15.74)
splash2x/ocean_cp       3402516.400  3405858.200  (0.10)     3442579.400  (1.18)     3411920.400  (0.28)     2782917.800  (-18.21)
splash2x/ocean_ncp      3924875.800  3921542.800  (-0.08)    7179644.000  (82.93)    5243201.400  (33.59)    2760506.600  (-29.67)
splash2x/radiosity      1472925.800  1475449.200  (0.17)     1485645.800  (0.86)     1473646.000  (0.05)     248785.000   (-83.11)
splash2x/radix          1748452.000  1750998.000  (0.15)     1434846.600  (-17.94)   1606307.800  (-8.13)    1713493.600  (-2.00)
splash2x/raytrace       23265.600    23278.400    (0.06)     29232.800    (25.65)    27050.400    (16.27)    16464.600    (-29.23)
splash2x/volrend        44020.600    44048.400    (0.06)     44148.400    (0.29)     44125.400    (0.24)     28101.800    (-36.16)
splash2x/water_nsquared 29420.800    29409.600    (-0.04)    29808.400    (1.32)     29984.800    (1.92)     25234.000    (-14.23)
splash2x/water_spatial  656716.000   656514.200   (-0.03)    656023.000   (-0.11)    656411.600   (-0.05)    498736.400   (-24.06)
total                   28416316.000 28589600.000 (0.61)     31541823.000 (11.00)    29712600.000 (4.56)     20860800.000 (-26.59)

In total, 26.59% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 88.15% of residential sets and 22.30% of
system memory usage while incurring only 2.91% runtime overhead.


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
