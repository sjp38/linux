Implement Data Access Monitoring-based Memory Operation Schemes

DAMON[1] can be used as a primitive for data access awared memory management
optimizations.  That said, users who want such optimizations should run DAMON,
read the monitoring results, analyze it, plan a new memory management scheme,
and apply the new scheme by themselves.  Such efforts will be inevitable for
some complicated optimizations.

However, in many other cases, the users would simply want the system to apply a
memory management action to a memory region of a specific size having a
specific access frequency for a specific time.  For example, "page out a memory
region larger than 100 MiB keeping only rare accesses more than 2 minutes", or
"Do not use THP for a memory region larger than 2 MiB rarely accessed for more
than 1 seconds".

This RFC patchset makes DAMON to handle such data access monitoring-based
operation schemes.  With this change, users can do the data access aware
optimizations by simply specifying their schemes to DAMON.

[1] https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/


Evaluations
===========

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

orig: Linux v5.6 with 'madvise' THP policy
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
if the target workload has small memory footprint.  In detail,
parsec3/swaptions (13 MiB), splash2x/raytrace (47 MiB), splash2x/volrend (150
MiB), and splash2x/water_nsquared (46 MiB)) show 103.44%, 23%, 9%, and 32%
overheads, respectively.  Nonetheless, the overheads are not from DAMON, but
from the wrapper, and thus should be ignored.  This fake memory overhead
continues in 'ethp' and 'prcl', as those configurations are also using the
Python wrapper.


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
system memory footprint while incurring only 2.91% runtime overhead.


Baseline and Complete Git Tree
==============================


The patches are based on the v5.6 plus v9 DAMON patchset[1] and Minchan's
``do_madvise()`` patch[2].  Minchan's patch was necessary for reuse of
``madvise()`` code in DAMON.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v7

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v7

The latest DAMON development tree is also available at:
https://github.com/sjp38/linux/tree/damon/master


[1] https://lore.kernel.org/linux-mm/20200406130938.14066-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200302193630.68771-2-minchan@kernel.org/


Sequence Of Patches
===================

The first patch allows DAMON to reuse ``madvise()`` code for the actions.  The
second patch accounts age of each region.  The third patch implements the
handling of the schemes in DAMON and exports a kernel space programming
interface for it.  The fourth patch implements a debugfs interface for the
privileged people and programs.  The fifth and sixth patches each adds kunit
tests and selftests for these changes, and finally the seventhe patch adds
human friendly schemes support to the user space tool for DAMON.


Patch History
=============

Changes from RFC v6
(https://lore.kernel.org/linux-mm/20200407100007.3894-1-sjpark@amazon.com/)
 - Rebase on DAMON v9 patchset
 - Cleanup code and fix typos (Stefan Nuernberger)

Changes from RFC v5
(https://lore.kernel.org/linux-mm/20200330115042.17431-1-sjpark@amazon.com/)
 - Rebase on DAMON v8 patchset
 - Update test results
 - Fix DAMON userspace tool crash on signal handling
 - Fix checkpatch warnings

Changes from RFC v4
(https://lore.kernel.org/linux-mm/20200303121406.20954-1-sjpark@amazon.com/)
 - Handle CONFIG_ADVISE_SYSCALL
 - Clean up code (Jonathan Cameron)
 - Update test results
 - Rebase on v5.6 + DAMON v7

Changes from RFC v3
(https://lore.kernel.org/linux-mm/20200225102300.23895-1-sjpark@amazon.com/)
 - Add Reviewed-by from Brendan Higgins
 - Code cleanup: Modularize madvise() call
 - Fix a trivial bug in the wrapper python script
 - Add more stable and detailed evaluation results with updated ETHP scheme

Changes from RFC v2
(https://lore.kernel.org/linux-mm/20200218085309.18346-1-sjpark@amazon.com/)
 - Fix aging mechanism for more better 'old region' selection
 - Add more kunittests and kselftests for this patchset
 - Support more human friedly description and application of 'schemes'

Changes from RFC v1
(https://lore.kernel.org/linux-mm/20200210150921.32482-1-sjpark@amazon.com/)
 - Properly adjust age accounting related properties after splitting, merging,
   and action applying

==================================== >8 =======================================

Appendix: Stdev / min / max numbers among the repeated runs
===========================================================

Below are stdev/min/max of each number in the 5 repeated runs.

runtime_stdev           orig  rec   thp   ethp   prcl
parsec3/blackscholes    0.927 0.093 0.479 0.809  1.879
parsec3/bodytrack       0.716 0.299 0.545 0.744  0.759
parsec3/canneal         5.413 2.320 4.270 3.734  4.561
parsec3/dedup           0.087 0.045 0.051 0.103  1.027
parsec3/facesim         1.286 0.243 1.832 1.070  1.478
parsec3/ferret          1.224 0.355 1.410 0.280  1.042
parsec3/fluidanimate    0.771 1.015 1.825 0.560  3.507
parsec3/freqmine        1.583 2.597 0.788 1.133  2.163
parsec3/raytrace        0.221 0.694 0.158 0.398  0.540
parsec3/streamcluster   0.999 1.660 1.426 3.448  1.246
parsec3/swaptions       1.014 0.949 2.997 0.902  1.032
parsec3/vips            0.322 0.127 0.280 0.349  0.365
parsec3/x264            4.271 5.999 5.492 3.739  5.560
splash2x/barnes         0.972 0.698 0.558 1.094  3.955
splash2x/fft            0.447 0.143 0.352 2.985  7.956
splash2x/lu_cb          0.796 0.105 0.088 0.688  1.200
splash2x/lu_ncb         1.024 0.277 0.199 1.103  2.872
splash2x/ocean_cp       0.280 0.126 0.201 0.673  3.281
splash2x/ocean_ncp      1.057 0.508 0.335 12.102 14.090
splash2x/radiosity      0.785 0.173 0.260 0.870  0.893
splash2x/radix          0.316 0.150 0.201 2.437  7.187
splash2x/raytrace       0.424 0.321 0.882 0.830  0.626
splash2x/volrend        0.089 0.381 0.972 0.242  0.223
splash2x/water_nsquared 2.306 3.673 2.910 2.275  0.274
splash2x/water_spatial  0.107 0.230 0.709 0.800  2.819


memused.avg_stdev       orig       rec       thp       ethp       prcl
parsec3/blackscholes    9697.635   1494.697  2387.690  8093.143   43663.214
parsec3/bodytrack       7250.643   1473.509  2397.013  7603.035   39980.147
parsec3/canneal         4210.342   1424.186  7433.352  5297.711   4658.795
parsec3/dedup           68212.037  20891.405 45047.056 53208.941  80774.420
parsec3/facesim         2032.111   1231.900  2189.628  1509.638   3163.506
parsec3/ferret          3086.358   3654.773  2260.834  479.277    1326.218
parsec3/fluidanimate    4134.608   2544.518  1207.893  3600.042   19132.237
parsec3/freqmine        1289.178   1158.483  2775.569  11680.579  1778.696
parsec3/raytrace        2998.385   4523.060  4970.297  2610.227   10123.220
parsec3/streamcluster   29359.793  778.918   2727.880  8358.612   1162.631
parsec3/swaptions       1333.761   1725.212  921.479   1256.970   1450.368
parsec3/vips            3396.246   3850.901  3947.109  4234.346   4890.742
parsec3/x264            37565.788  16116.006 23477.849 36372.921  19756.335
splash2x/barnes         2187.324   2570.255  4164.816  6122.991   23278.418
splash2x/fft            114780.227 55225.274 38989.522 120264.044 320549.636
splash2x/lu_cb          1068.762   1221.553  2979.611  1226.053   9996.730
splash2x/lu_ncb         3170.041   1571.103  1376.271  2051.800   24940.971
splash2x/ocean_cp       7068.865   35021.454 6299.243  5732.506   39711.553
splash2x/ocean_ncp      7937.572   4710.785  6351.772  884113.443 65600.214
splash2x/radiosity      6180.740   1808.464  1987.039  62425.903  49386.961
splash2x/radix          19986.863  5055.739  7747.845  133910.722 250182.861
splash2x/raytrace       824.991    805.535   1398.821  1882.344   657.657
splash2x/volrend        1536.609   2907.749  1343.772  1551.393   1794.672
splash2x/water_nsquared 2302.140   589.065   2673.043  2603.017   1178.533
splash2x/water_spatial  4190.273   1206.995  4811.902  1670.600   54112.547


rss.avg_stdev           orig       rec       thp       ethp       prcl
parsec3/blackscholes    1974.955   2499.204  2250.405  1922.173   63345.041
parsec3/bodytrack       220.466    268.157   131.569   74.021     761.960
parsec3/canneal         1471.797   514.756   1434.294  2098.894   5416.763
parsec3/dedup           32354.306  476.043   44399.324 11765.220  447871.173
parsec3/facesim         180.793    347.747   1332.481  699.827    4196.029
parsec3/ferret          56.986     197.517   1604.932  445.317    1218.997
parsec3/fluidanimate    901.496    35.091    118.211   364.814    20828.449
parsec3/freqmine        598.492    921.266   1049.698  716.771    3775.504
parsec3/raytrace        1438.348   1493.995  933.785   1600.661   40270.688
parsec3/streamcluster   84.156     35.942    540.225   552.519    76.022
parsec3/swaptions       72.212     67.857    68.884    92.417     184.822
parsec3/vips            210.871    119.778   429.014   385.145    492.484
parsec3/x264            391.476    275.537   657.096   211.499    524.207
splash2x/barnes         3042.395   7105.471  984.499   2359.814   51528.340
splash2x/fft            177542.817 25508.833 65650.997 46205.634  986121.141
splash2x/lu_cb          482.980    2549.395  22.414    692.206    10484.456
splash2x/lu_ncb         752.005    318.677   42.691    601.876    25431.036
splash2x/ocean_cp       9541.635   5736.370  4909.930  7999.780   199531.665
splash2x/ocean_ncp      8671.685   16560.130 3528.334  945156.130 205065.499
splash2x/radiosity      4009.875   1272.857  347.112   2746.042   84332.709
splash2x/radix          31387.749  49955.889 4666.096  140269.485 162155.771
splash2x/raytrace       57.722     74.085    1291.440  489.024    2241.461
splash2x/volrend        54.169     72.182    89.641    208.225    2268.328
splash2x/water_nsquared 23.379     29.890    435.101   490.508    631.352
splash2x/water_spatial  611.088    652.141   885.563   554.320    71409.571


runtime_min             orig    rec     thp     ethp    prcl
parsec3/blackscholes    106.457 106.550 105.994 106.795 109.004
parsec3/bodytrack       78.367  78.800  78.222  78.645  79.723
parsec3/canneal         129.744 135.271 121.695 126.207 149.199
parsec3/dedup           11.822  11.785  11.693  11.818  11.963
parsec3/facesim         206.997 208.071 203.557 205.254 215.114
parsec3/ferret          189.292 190.111 189.528 190.210 192.073
parsec3/fluidanimate    209.892 211.267 207.865 210.921 216.901
parsec3/freqmine        286.118 288.196 286.343 287.564 294.548
parsec3/raytrace        117.501 118.562 118.566 118.213 129.207
parsec3/streamcluster   320.227 325.232 281.686 287.583 327.193
parsec3/swaptions       153.229 154.133 153.392 154.194 154.358
parsec3/vips            58.563  58.352  57.859  58.604  59.446
parsec3/x264            64.915  62.497  59.804  64.030  63.511
splash2x/barnes         79.605  79.729  73.315  78.168  94.994
splash2x/fft            32.830  33.302  22.901  26.244  34.666
splash2x/lu_cb          84.837  85.198  84.320  85.354  87.937
splash2x/lu_ncb         91.839  92.540  89.880  92.368  93.502
splash2x/ocean_cp       44.189  44.592  42.787  43.538  53.972
splash2x/ocean_ncp      79.264  81.014  50.772  52.880  119.121
splash2x/radiosity      90.665  91.160  90.471  91.020  101.365
splash2x/radix          30.702  31.060  25.087  25.822  33.994
splash2x/raytrace       83.267  84.228  82.642  82.295  83.801
splash2x/volrend        86.087  86.621  85.258  86.344  87.316
splash2x/water_nsquared 229.264 231.365 217.897 222.087 236.275
splash2x/water_spatial  88.576  88.934  88.633  88.829  93.093


memused.avg_min         orig        rec         thp         ethp        prcl
parsec3/blackscholes    1806450.000 1832800.000 1821208.000 1815059.000 1597823.000
parsec3/bodytrack       1406716.000 1433260.000 1415546.000 1414766.000 1422325.000
parsec3/canneal         1034762.000 1050949.000 1029342.000 1045707.000 1039362.000
parsec3/dedup           2276407.000 2416275.000 2332186.000 2326706.000 2293520.000
parsec3/facesim         537730.000  552392.000  541119.000  551696.000  479697.000
parsec3/ferret          314753.000  325707.000  315704.000  333059.000  327897.000
parsec3/fluidanimate    569205.000  582740.000  574036.000  579170.000  465726.000
parsec3/freqmine        984189.000  997617.000  983088.000  990709.000  763236.000
parsec3/raytrace        1743861.000 1745491.000 1737423.000 1750450.000 1574814.000
parsec3/streamcluster   119184.000  135411.000  115804.000  127617.000  130339.000
parsec3/swaptions       11455.000   24872.000   15156.000   25501.000   24410.000
parsec3/vips            2950013.000 2968672.000 2952220.000 2966535.000 2961988.000
parsec3/x264            3105486.000 3187448.000 3152008.000 3124959.000 3186481.000
splash2x/barnes         1210156.000 1207677.000 1213432.000 1207101.000 942444.000
splash2x/fft            9169120.000 9153626.000 9248274.000 8931352.000 9298670.000
splash2x/lu_cb          513286.000  521543.000  516460.000  520848.000  343202.000
splash2x/lu_ncb         509384.000  522742.000  519016.000  518494.000  414389.000
splash2x/ocean_cp       3332320.000 3283348.000 3369876.000 3283512.000 3111864.000
splash2x/ocean_ncp      3887754.000 3865529.000 7060592.000 3875220.000 3491442.000
splash2x/radiosity      1456077.000 1467334.000 1478828.000 1463326.000 426797.000
splash2x/radix          1671807.000 1665862.000 1380882.000 1343347.000 1720222.000
splash2x/raytrace       46261.000   57711.000   50246.000   57849.000   53681.000
splash2x/volrend        147829.000  161237.000  148246.000  160764.000  158943.000
splash2x/water_nsquared 42598.000   61731.000   42600.000   54974.000   57599.000
splash2x/water_spatial  660084.000  672456.000  656933.000  670901.000  476582.000


rss.avg_min             orig        rec         thp         ethp        prcl
parsec3/blackscholes    588530.000  588342.000  590573.000  588953.000  251104.000
parsec3/bodytrack       31780.000   31948.000   32128.000   32212.000   26108.000
parsec3/canneal         839418.000  839190.000  835078.000  835363.000  816479.000
parsec3/dedup           1165305.000 1234371.000 1143193.000 1206303.000 194759.000
parsec3/facesim         311415.000  310889.000  312549.000  311587.000  178906.000
parsec3/ferret          99636.000   99188.000   99631.000   99183.000   87446.000
parsec3/fluidanimate    529628.000  531824.000  531584.000  531880.000  402604.000
parsec3/freqmine        551880.000  551304.000  555413.000  552349.000  59796.000
parsec3/raytrace        892361.000  892703.000  888396.000  890062.000  317630.000
parsec3/streamcluster   110762.000  110887.000  110975.000  111028.000  109785.000
parsec3/swaptions       5552.000    5565.000    5567.000    5533.000    4028.000
parsec3/vips            31569.000   31792.000   31569.000   31770.000   28081.000
parsec3/x264            81172.000   81427.000   82115.000   82098.000   80171.000
splash2x/barnes         1211709.000 1198036.000 1221765.000 1214537.000 612264.000
splash2x/fft            9325088.000 9702371.000 9643955.000 9609539.000 6873966.000
splash2x/lu_cb          509124.000  504333.000  514440.000  509140.000  326514.000
splash2x/lu_ncb         508924.000  509949.000  513828.000  508981.000  398087.000
splash2x/ocean_cp       3390197.000 3396522.000 3435755.000 3406635.000 2496312.000
splash2x/ocean_ncp      3911994.000 3888754.000 7174881.000 3931613.000 2510919.000
splash2x/radiosity      1466955.000 1473002.000 1485104.000 1469533.000 133616.000
splash2x/radix          1718566.000 1651402.000 1427018.000 1431687.000 1421661.000
splash2x/raytrace       23192.000   23184.000   28360.000   26609.000   14007.000
splash2x/volrend        43966.000   43984.000   44036.000   43916.000   24641.000
splash2x/water_nsquared 29384.000   29372.000   29461.000   29368.000   24322.000
splash2x/water_spatial  655822.000  655553.000  655257.000  655564.000  385536.000


runtime_max             orig    rec     thp     ethp    prcl
parsec3/blackscholes    108.639 106.823 107.089 108.523 114.100
parsec3/bodytrack       80.344  79.682  79.610  80.455  81.570
parsec3/canneal         144.660 141.343 133.450 136.502 162.011
parsec3/dedup           12.053  11.914  11.815  12.125  14.375
parsec3/facesim         210.501 208.796 208.025 208.131 219.250
parsec3/ferret          192.667 191.103 193.199 190.842 194.873
parsec3/fluidanimate    212.016 214.100 212.441 212.302 226.134
parsec3/freqmine        290.759 296.147 288.329 290.879 300.302
parsec3/raytrace        118.187 120.142 119.051 119.264 130.698
parsec3/streamcluster   322.889 329.792 286.140 296.215 330.679
parsec3/swaptions       155.672 156.698 161.053 156.156 157.261
parsec3/vips            59.441  58.744  58.660  59.471  60.506
parsec3/x264            75.093  76.112  71.583  73.333  77.155
splash2x/barnes         82.041  81.774  75.048  80.939  106.030
splash2x/fft            34.055  33.708  23.851  33.607  55.145
splash2x/lu_cb          86.523  85.472  84.560  87.341  90.481
splash2x/lu_ncb         94.408  93.267  90.395  95.377  100.905
splash2x/ocean_cp       44.899  44.971  43.330  45.350  62.554
splash2x/ocean_ncp      82.065  82.463  51.817  80.988  159.273
splash2x/radiosity      92.750  91.653  91.252  93.436  104.075
splash2x/radix          31.621  31.422  25.630  31.439  53.405
splash2x/raytrace       84.439  85.095  85.075  84.137  85.696
splash2x/volrend        86.330  87.694  87.981  87.032  87.949
splash2x/water_nsquared 235.682 241.083 225.483 228.505 237.043
splash2x/water_spatial  88.888  89.473  90.619  91.044  101.648


memused.avg_max         orig        rec         thp         ethp        prcl
parsec3/blackscholes    1828721.000 1836936.000 1828313.000 1836904.000 1725072.000
parsec3/bodytrack       1423906.000 1437442.000 1421857.000 1436967.000 1529455.000
parsec3/canneal         1047894.000 1055037.000 1044682.000 1060394.000 1050224.000
parsec3/dedup           2422783.000 2470880.000 2440561.000 2469625.000 2482833.000
parsec3/facesim         543497.000  555890.000  547311.000  555693.000  488209.000
parsec3/ferret          322168.000  335269.000  321546.000  334310.000  331252.000
parsec3/fluidanimate    581849.000  589377.000  577381.000  588655.000  519412.000
parsec3/freqmine        987399.000  1001129.000 991383.000  1024350.000 768684.000
parsec3/raytrace        1752775.000 1756513.000 1748414.000 1757901.000 1602179.000
parsec3/streamcluster   193685.000  137433.000  123499.000  151169.000  133488.000
parsec3/swaptions       15394.000   29542.000   17551.000   29108.000   28190.000
parsec3/vips            2959475.000 2980019.000 2963026.000 2978095.000 2973578.000
parsec3/x264            3208695.000 3231006.000 3216669.000 3202809.000 3238422.000
splash2x/barnes         1215967.000 1214849.000 1225941.000 1225748.000 1002529.000
splash2x/fft            9498187.000 9306674.000 9348936.000 9248373.000 10130342.000
splash2x/lu_cb          516522.000  524947.000  523693.000  524516.000  371119.000
splash2x/lu_ncb         518686.000  527209.000  522813.000  523686.000  481892.000
splash2x/ocean_cp       3351940.000 3363005.000 3387628.000 3301121.000 3225897.000
splash2x/ocean_ncp      3911769.000 3879024.000 7076827.000 5828184.000 3663069.000
splash2x/radiosity      1473754.000 1472333.000 1484191.000 1624759.000 551833.000
splash2x/radix          1727012.000 1680485.000 1402384.000 1670110.000 2391320.000
splash2x/raytrace       48817.000   60002.000   54006.000   63010.000   55226.000
splash2x/volrend        152398.000  169388.000  152201.000  165242.000  164335.000
splash2x/water_nsquared 48865.000   63465.000   50230.000   62111.000   61226.000
splash2x/water_spatial  670394.000  675796.000  670321.000  675621.000  630537.000


rss.avg_max             orig        rec         thp         ethp        prcl
parsec3/blackscholes    593431.000  593881.000  596538.000  594269.000  431382.000
parsec3/bodytrack       32421.000   32705.000   32516.000   32411.000   28188.000
parsec3/canneal         843084.000  840577.000  839187.000  841070.000  831519.000
parsec3/dedup           1236606.000 1235718.000 1242719.000 1236695.000 1235588.000
parsec3/facesim         311916.000  311790.000  316261.000  313469.000  190366.000
parsec3/ferret          99770.000   99753.000   103510.000  100556.000  90763.000
parsec3/fluidanimate    531936.000  531912.000  531948.000  532809.000  460491.000
parsec3/freqmine        553491.000  553511.000  558414.000  554173.000  70911.000
parsec3/raytrace        896681.000  896342.000  891012.000  894728.000  425544.000
parsec3/streamcluster   110997.000  110999.000  112287.000  112349.000  110011.000
parsec3/swaptions       5763.000    5723.000    5722.000    5799.000    4553.000
parsec3/vips            32099.000   32135.000   32879.000   32930.000   29459.000
parsec3/x264            82209.000   82190.000   83818.000   82679.000   81584.000
splash2x/barnes         1220175.000 1216655.000 1224298.000 1221938.000 759477.000
splash2x/fft            9748999.000 9768966.000 9824334.000 9726585.000 9749104.000
splash2x/lu_cb          510299.000  510868.000  514505.000  510922.000  355965.000
splash2x/lu_ncb         510679.000  510788.000  513962.000  510663.000  466846.000
splash2x/ocean_cp       3416130.000 3414420.000 3449904.000 3427720.000 2998629.000
splash2x/ocean_ncp      3936906.000 3932580.000 7183097.000 6017945.000 3040572.000
splash2x/radiosity      1477020.000 1476736.000 1486152.000 1477564.000 344068.000
splash2x/radix          1788375.000 1781055.000 1440575.000 1774484.000 1892298.000
splash2x/raytrace       23356.000   23360.000   31800.000   27846.000   20436.000
splash2x/volrend        44119.000   44157.000   44260.000   44517.000   30636.000
splash2x/water_nsquared 29448.000   29452.000   30625.000   30629.000   25995.000
splash2x/water_spatial  657379.000  657433.000  657217.000  657169.000  602051.000
