================================== >8 =========================================
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
parsec3/blackscholes    106.654  107.111  (0.43)     106.622  (-0.03)    107.143  (0.46)     112.081  (5.09)
parsec3/bodytrack       78.866   79.204   (0.43)     78.922   (0.07)     79.332   (0.59)     80.734   (2.37)
parsec3/canneal         138.029  145.336  (5.29)     123.243  (-10.71)   132.810  (-3.78)    186.616  (35.20)
parsec3/dedup           11.884   11.853   (-0.26)    11.659   (-1.89)    11.810   (-0.62)    12.782   (7.56)
parsec3/facesim         207.510  208.426  (0.44)     204.354  (-1.52)    205.985  (-0.73)    216.421  (4.29)
parsec3/ferret          190.833  191.105  (0.14)     190.544  (-0.15)    191.256  (0.22)     192.457  (0.85)
parsec3/fluidanimate    212.443  211.528  (-0.43)    209.050  (-1.60)    211.874  (-0.27)    221.745  (4.38)
parsec3/freqmine        290.628  291.023  (0.14)     289.766  (-0.30)    290.372  (-0.09)    296.525  (2.03)
parsec3/raytrace        118.753  119.071  (0.27)     119.469  (0.60)     119.164  (0.35)     129.302  (8.88)
parsec3/streamcluster   323.478  333.131  (2.98)     283.777  (-12.27)   292.393  (-9.61)    334.684  (3.46)
parsec3/swaptions       153.842  155.696  (1.21)     154.910  (0.69)     155.416  (1.02)     154.564  (0.47)
parsec3/vips            58.720   59.134   (0.71)     58.578   (-0.24)    58.704   (-0.03)    60.198   (2.52)
parsec3/x264            67.249   69.709   (3.66)     72.050   (7.14)     66.221   (-1.53)    73.243   (8.91)
splash2x/barnes         80.294   80.925   (0.79)     73.913   (-7.95)    79.315   (-1.22)    104.004  (29.53)
splash2x/fft            33.382   33.455   (0.22)     22.975   (-31.18)   32.540   (-2.52)    41.816   (25.26)
splash2x/lu_cb          85.227   86.203   (1.14)     84.441   (-0.92)    85.754   (0.62)     89.219   (4.68)
splash2x/lu_ncb         92.584   93.778   (1.29)     90.515   (-2.23)    94.927   (2.53)     100.555  (8.61)
splash2x/ocean_cp       44.616   44.908   (0.66)     43.252   (-3.06)    44.247   (-0.83)    47.246   (5.90)
splash2x/ocean_ncp      81.833   81.842   (0.01)     51.142   (-37.50)   73.403   (-10.30)   135.436  (65.50)
splash2x/radiosity      91.400   91.677   (0.30)     90.272   (-1.23)    91.934   (0.58)     101.606  (11.17)
splash2x/radix          31.300   31.513   (0.68)     25.124   (-19.73)   31.029   (-0.87)    43.364   (38.54)
splash2x/raytrace       84.216   84.812   (0.71)     82.784   (-1.70)    83.842   (-0.44)    85.805   (1.89)
splash2x/volrend        87.021   88.504   (1.70)     86.420   (-0.69)    87.585   (0.65)     89.024   (2.30)
splash2x/water_nsquared 232.134  233.488  (0.58)     221.293  (-4.67)    228.383  (-1.62)    236.080  (1.70)
splash2x/water_spatial  89.560   89.473   (-0.10)    89.051   (-0.57)    90.450   (0.99)     98.599   (10.09)
total                   2992.450 3022.890 (1.02)     2864.120 (-4.29)    2945.880 (-1.56)    3244.110 (8.41)


memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    1821608.200  1835268.800  (0.75)     1832919.000  (0.62)     1833142.200  (0.63)     1628095.400  (-10.62)
parsec3/bodytrack       1418964.600  1430433.200  (0.81)     1416871.200  (-0.15)    1433869.600  (1.05)     1427908.000  (0.63)
parsec3/canneal         1045208.200  1058310.200  (1.25)     1037149.400  (-0.77)    1050233.400  (0.48)     1043619.200  (-0.15)
parsec3/dedup           2436947.000  2457476.800  (0.84)     2439557.800  (0.11)     2433319.800  (-0.15)    2441437.000  (0.18)
parsec3/facesim         540414.600   552894.400   (2.31)     540916.800   (0.09)     553387.000   (2.40)     485772.200   (-10.11)
parsec3/ferret          319888.200   332860.800   (4.06)     317343.400   (-0.80)    330101.000   (3.19)     329613.200   (3.04)
parsec3/fluidanimate    571528.400   585670.200   (2.47)     581304.600   (1.71)     588979.000   (3.05)     464640.400   (-18.70)
parsec3/freqmine        985397.400   998531.400   (1.33)     994316.000   (0.91)     996987.000   (1.18)     768600.400   (-22.00)
parsec3/raytrace        1744482.200  1754726.400  (0.59)     1743206.600  (-0.07)    1751374.600  (0.40)     1582949.400  (-9.26)
parsec3/streamcluster   121025.400   140085.400   (15.75)    118762.200   (-1.87)    134685.400   (11.29)    132044.800   (9.11)
parsec3/swaptions       13818.000    29163.000    (111.05)   13360.200    (-3.31)    28003.400    (102.66)   25973.000    (87.96)
parsec3/vips            2958208.000  2979560.200  (0.72)     2954836.800  (-0.11)    2962623.400  (0.15)     2966417.000  (0.28)
parsec3/x264            3183295.400  3203048.400  (0.62)     3196805.200  (0.42)     3191084.600  (0.24)     3215451.800  (1.01)
splash2x/barnes         1214667.000  1213394.400  (-0.10)    1217076.200  (0.20)     1211348.400  (-0.27)    955056.000   (-21.37)
splash2x/fft            9431970.200  9233116.800  (-2.11)    9281561.000  (-1.59)    9177215.600  (-2.70)    9459883.200  (0.30)
splash2x/lu_cb          515966.400   525217.800   (1.79)     520740.200   (0.93)     521936.800   (1.16)     356931.000   (-30.82)
splash2x/lu_ncb         514713.400   525678.000   (2.13)     517579.200   (0.56)     522634.800   (1.54)     368449.800   (-28.42)
splash2x/ocean_cp       3339236.400  3282117.600  (-1.71)    3385554.400  (1.39)     3292656.800  (-1.39)    3274836.400  (-1.93)
splash2x/ocean_ncp      3903327.200  3916898.800  (0.35)     7066337.800  (81.03)    4398342.000  (12.68)    3533107.600  (-9.48)
splash2x/radiosity      1465314.800  1471012.200  (0.39)     1478023.600  (0.87)     1470864.400  (0.38)     567732.000   (-61.26)
splash2x/radix          1715460.000  1675025.600  (-2.36)    1385497.000  (-19.23)   1654754.200  (-3.54)    2013401.600  (17.37)
splash2x/raytrace       46682.400    59543.400    (27.55)    50391.400    (7.95)     61513.200    (31.77)    52070.800    (11.54)
splash2x/volrend        152453.800   166313.000   (9.09)     152284.000   (-0.11)    164212.400   (7.71)     158362.800   (3.88)
splash2x/water_nsquared 48045.400    61679.800    (28.38)    48871.200    (1.72)     59778.000    (24.42)    59444.200    (23.73)
splash2x/water_spatial  667431.000   674130.400   (1.00)     665027.800   (-0.36)    674607.000   (1.08)     520460.800   (-22.02)
total                   40175831.000 40162000.000 (-0.03)    42956400.000 (6.92)     40497585.000 (0.80)     37832100.000 (-5.83)


DAMON Overheads
~~~~~~~~~~~~~~~

In total, DAMON recording feature incurs 1.02% runtime overhead (up to 5.29% in
worst case with 'parsec3/canneal') and -0.03% memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  In detail, for
parsec3/swaptions (13 MiB), splash2x/raytrace (46 MiB), splash2x/volrend (151
MiB), and splash2x/water_nsquared (48 MiB)), it shows 111%, 27%, 9%, and 28%
overheads, respectively.  Nonetheless, the overheads are not from DAMON, but
from the wrapper, and thus should be ignored.  This fake memory overhead
continues in 'ethp' and 'prcl', as those configurations are also using the
Python wrapper.


Efficient THP
~~~~~~~~~~~~~

THP 'always' enabled policy achieves 4.29% speedup but incurs 6.92% memory
overhead.  It achieves 37.50% speedup in best case, but 81.03% memory overhead
in worst case.  Interestingly, both the best and worst case are with
'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 1.56% speedup and 0.80% memory overhead.  In other words, 'ethp' removes
88.43% of THP memory waste while preserving 36.36% of THP speedup in total.  In
case of the 'splash2x/ocean_ncp', 'ethp' removes 84.35% of THP memory waste
while preserving 27.46% of THP speedup.


Proactive Reclamation
~~~~~~~~~~~~~~~~~~~~

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
8.41% runtime overhead in total while achieving 5.83% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads:

rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    591726.600   589827.600   (-0.32)    594031.000   (0.39)     590779.800   (-0.16)    289370.800   (-51.10)
parsec3/bodytrack       32191.800    32147.600    (-0.14)    32203.800    (0.04)     32190.800    (-0.00)    28509.600    (-11.44)
parsec3/canneal         840227.000   840745.400   (0.06)     836600.000   (-0.43)    837091.200   (-0.37)    800416.200   (-4.74)
parsec3/dedup           1209377.600  1235142.800  (2.13)     1236087.200  (2.21)     1235881.400  (2.19)     1040422.200  (-13.97)
parsec3/facesim         311068.200   311408.800   (0.11)     315000.200   (1.26)     312630.200   (0.50)     189374.800   (-39.12)
parsec3/ferret          99423.800    99612.800    (0.19)     100580.800   (1.16)     99643.800    (0.22)     89602.000    (-9.88)
parsec3/fluidanimate    531795.200   531800.000   (0.00)     531768.200   (-0.01)    532213.800   (0.08)     404920.000   (-23.86)
parsec3/freqmine        552705.200   553281.200   (0.10)     524774.600   (-5.05)    553188.000   (0.09)     75346.000    (-86.37)
parsec3/raytrace        893993.000   894836.200   (0.09)     890009.400   (-0.45)    894002.800   (0.00)     370720.400   (-58.53)
parsec3/streamcluster   110863.200   110800.800   (-0.06)    111202.800   (0.31)     111747.400   (0.80)     109678.400   (-1.07)
parsec3/swaptions       5597.200     5565.600     (-0.56)    5579.000     (-0.33)    5581.000     (-0.29)    3895.000     (-30.41)
parsec3/vips            31838.200    31800.600    (-0.12)    32053.800    (0.68)     32094.800    (0.81)     28568.400    (-10.27)
parsec3/x264            81848.400    81968.200    (0.15)     82704.800    (1.05)     82349.600    (0.61)     80256.400    (-1.95)
splash2x/barnes         1216330.000  1216704.800  (0.03)     1226790.600  (0.86)     1217759.800  (0.12)     652778.400   (-46.33)
splash2x/fft            9604191.400  9478105.400  (-1.31)    9676396.200  (0.75)     9611581.200  (0.08)     8191631.600  (-14.71)
splash2x/lu_cb          510556.200   510598.200   (0.01)     514475.400   (0.77)     510784.400   (0.04)     343677.000   (-32.69)
splash2x/lu_ncb         509221.400   510098.000   (0.17)     513902.200   (0.92)     510608.800   (0.27)     348586.800   (-31.55)
splash2x/ocean_cp       3385303.800  3388240.400  (0.09)     3446201.600  (1.80)     3396817.800  (0.34)     3194041.400  (-5.65)
splash2x/ocean_ncp      3930791.200  3905177.000  (-0.65)    7180424.600  (82.67)    4491125.400  (14.26)    2764826.400  (-29.66)
splash2x/radiosity      1474778.800  1475827.000  (0.07)     1485800.000  (0.75)     1473044.800  (-0.12)    337617.400   (-77.11)
splash2x/radix          1766938.600  1753619.200  (-0.75)    1429421.200  (-19.10)   1745239.800  (-1.23)    1684783.200  (-4.65)
splash2x/raytrace       23273.600    23279.200    (0.02)     28877.600    (24.08)    26965.400    (15.86)    14920.000    (-35.89)
splash2x/volrend        44081.200    44069.600    (-0.03)    44206.800    (0.28)     44387.800    (0.70)     20929.600    (-52.52)
splash2x/water_nsquared 29414.400    29396.000    (-0.06)    29877.400    (1.57)     29734.200    (1.09)     25908.800    (-11.92)
splash2x/water_spatial  655966.800   656516.800   (0.08)     656515.200   (0.08)     656664.000   (0.11)     451478.000   (-31.17)
total                   28443600.000 28310500.000 (-0.47)    31525500.000 (10.84)    29033964.000 (2.08)     21542178.000 (-24.26)

In total, 24.26% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 86.37% of residential sets and 22% of
system memory usage while incurring only 2.03% runtime overhead.


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
