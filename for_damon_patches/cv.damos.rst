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
operation schemes.  With this change, users can do the data access awared
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
parsec3/blackscholes    107.097  106.955  (-0.13)    106.352  (-0.70)    107.357  (0.24)     108.284  (1.11)
parsec3/bodytrack       79.135   79.062   (-0.09)    78.996   (-0.18)    79.261   (0.16)     79.824   (0.87)
parsec3/canneal         139.036  139.694  (0.47)     125.947  (-9.41)    131.071  (-5.73)    148.648  (6.91)
parsec3/dedup           11.914   11.905   (-0.07)    11.729   (-1.55)    11.916   (0.02)     12.613   (5.87)
parsec3/facesim         208.761  209.476  (0.34)     204.778  (-1.91)    206.157  (-1.25)    214.016  (2.52)
parsec3/ferret          190.854  191.309  (0.24)     190.223  (-0.33)    190.821  (-0.02)    191.847  (0.52)
parsec3/fluidanimate    211.317  213.798  (1.17)     208.883  (-1.15)    211.319  (0.00)     214.566  (1.54)
parsec3/freqmine        288.672  290.547  (0.65)     288.310  (-0.13)    288.727  (0.02)     292.294  (1.25)
parsec3/raytrace        118.692  119.443  (0.63)     118.625  (-0.06)    118.986  (0.25)     129.942  (9.48)
parsec3/streamcluster   323.387  327.244  (1.19)     284.931  (-11.89)   290.604  (-10.14)   330.111  (2.08)
parsec3/swaptions       154.304  154.891  (0.38)     154.373  (0.04)     155.226  (0.60)     155.338  (0.67)
parsec3/vips            58.879   59.254   (0.64)     58.459   (-0.71)    59.029   (0.25)     59.761   (1.50)
parsec3/x264            71.805   68.718   (-4.30)    67.262   (-6.33)    69.494   (-3.22)    71.291   (-0.72)
splash2x/barnes         80.624   80.680   (0.07)     74.538   (-7.55)    78.363   (-2.80)    86.373   (7.13)
splash2x/fft            33.462   33.285   (-0.53)    23.146   (-30.83)   33.306   (-0.47)    35.311   (5.53)
splash2x/lu_cb          85.474   85.681   (0.24)     84.516   (-1.12)    85.525   (0.06)     87.267   (2.10)
splash2x/lu_ncb         93.227   93.211   (-0.02)    90.939   (-2.45)    93.526   (0.32)     94.409   (1.27)
splash2x/ocean_cp       44.348   44.668   (0.72)     42.920   (-3.22)    44.128   (-0.50)    45.785   (3.24)
splash2x/ocean_ncp      81.234   81.275   (0.05)     51.441   (-36.67)   64.974   (-20.02)   94.207   (15.97)
splash2x/radiosity      90.976   91.131   (0.17)     90.325   (-0.72)    91.395   (0.46)     97.867   (7.57)
splash2x/radix          31.269   31.185   (-0.27)    25.103   (-19.72)   29.289   (-6.33)    37.713   (20.61)
splash2x/raytrace       83.945   84.242   (0.35)     82.314   (-1.94)    83.334   (-0.73)    84.655   (0.85)
splash2x/volrend        86.703   87.545   (0.97)     86.324   (-0.44)    86.717   (0.02)     87.925   (1.41)
splash2x/water_nsquared 230.426  232.979  (1.11)     219.950  (-4.55)    224.474  (-2.58)    235.770  (2.32)
splash2x/water_spatial  88.982   89.748   (0.86)     89.086   (0.12)     89.431   (0.50)     95.849   (7.72)
total                   2994.520 3007.910 (0.45)     2859.470 (-4.51)    2924.420 (-2.34)    3091.670 (3.24)


memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    1821479.200  1836018.600  (0.80)     1822020.600  (0.03)     1834214.200  (0.70)     1721607.800  (-5.48)
parsec3/bodytrack       1418698.400  1434689.800  (1.13)     1419134.400  (0.03)     1430609.800  (0.84)     1433137.600  (1.02)
parsec3/canneal         1045065.400  1052992.400  (0.76)     1042607.400  (-0.24)    1048730.400  (0.35)     1049446.000  (0.42)
parsec3/dedup           2387073.200  2425093.600  (1.59)     2398469.600  (0.48)     2416738.400  (1.24)     2433976.800  (1.96)
parsec3/facesim         540075.800   554130.000   (2.60)     544759.400   (0.87)     553325.800   (2.45)     489255.600   (-9.41)
parsec3/ferret          316932.800   331383.600   (4.56)     320355.800   (1.08)     331042.000   (4.45)     328275.600   (3.58)
parsec3/fluidanimate    576466.400   587466.600   (1.91)     582737.000   (1.09)     582560.600   (1.06)     499228.800   (-13.40)
parsec3/freqmine        985864.000   996351.800   (1.06)     990195.000   (0.44)     997435.400   (1.17)     809333.800   (-17.91)
parsec3/raytrace        1749485.600  1753601.400  (0.24)     1744385.000  (-0.29)    1755230.400  (0.33)     1597574.400  (-8.68)
parsec3/streamcluster   120976.200   133270.000   (10.16)    118688.200   (-1.89)    132846.800   (9.81)     133412.400   (10.28)
parsec3/swaptions       14953.600    28689.400    (91.86)    15826.000    (5.83)     26803.000    (79.24)    27754.400    (85.60)
parsec3/vips            2940086.400  2965866.800  (0.88)     2943217.200  (0.11)     2960823.600  (0.71)     2968121.000  (0.95)
parsec3/x264            3179843.200  3186839.600  (0.22)     3175893.600  (-0.12)    3182023.400  (0.07)     3202598.000  (0.72)
splash2x/barnes         1210899.200  1211648.600  (0.06)     1219328.800  (0.70)     1217686.000  (0.56)     1126669.000  (-6.96)
splash2x/fft            9322834.800  9142039.200  (-1.94)    9183937.800  (-1.49)    9159042.800  (-1.76)    9321729.200  (-0.01)
splash2x/lu_cb          515411.200   523698.400   (1.61)     521019.800   (1.09)     523047.400   (1.48)     461828.400   (-10.40)
splash2x/lu_ncb         514869.000   525223.000   (2.01)     521820.600   (1.35)     522588.800   (1.50)     480118.400   (-6.75)
splash2x/ocean_cp       3345433.400  3298946.800  (-1.39)    3377377.000  (0.95)     3289771.600  (-1.66)    3273329.800  (-2.16)
splash2x/ocean_ncp      3902999.600  3873302.600  (-0.76)    7069853.000  (81.14)    4962220.800  (27.14)    3772835.600  (-3.33)
splash2x/radiosity      1471551.000  1470698.600  (-0.06)    1481433.200  (0.67)     1466283.400  (-0.36)    838138.400   (-43.04)
splash2x/radix          1700185.000  1674226.400  (-1.53)    1386397.600  (-18.46)   1544387.800  (-9.16)    1957567.600  (15.14)
splash2x/raytrace       45493.800    57050.800    (25.40)    50134.000    (10.20)    60166.400    (32.25)    57634.000    (26.69)
splash2x/volrend        150549.200   165190.600   (9.73)     151509.600   (0.64)     162845.000   (8.17)     161346.000   (7.17)
splash2x/water_nsquared 46275.200    58483.600    (26.38)    71529.200    (54.57)    56770.200    (22.68)    59995.800    (29.65)
splash2x/water_spatial  666577.200   672511.800   (0.89)     667422.200   (0.13)     674555.000   (1.20)     608374.000   (-8.73)
total                   39990000.000 39959400.000 (-0.08)    42819900.000 (7.08)     40891655.000 (2.25)     38813174.000 (-2.94)


DAMON Overheads
~~~~~~~~~~~~~~~

In total, DAMON recording feature incurs 0.41% runtime overhead (up to 1.19% in
worst case with 'parsec3/streamcluster') and -0.08% memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  In detail, 10%, 91%, 25%,
9%, and 26% overheads shown for parsec3/streamcluster (125 MiB),
parsec3/swaptions (15 MiB), splash2x/raytrace (45 MiB), splash2x/volrend (151
MiB), and splash2x/water_nsquared (46 MiB)).  Nonetheless, the overheads are
not from DAMON, but from the wrapper, and thus should be ignored.  This fake
memory overhead continues in 'ethp' and 'prcl', as those configurations are
also using the Python wrapper.


Efficient THP
~~~~~~~~~~~~~

THP 'always' enabled policy achieves 4.51% speedup but incurs 7.08% memory
overhead.  It achieves 36.67% speedup in best case, but 81.14% memory overhead
in worst case.  Interestingly, both the best and worst case are with
'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.34% speedup and 2.25% memory overhead.  In other words, 'ethp' removes
68.22% of THP memory waste while preserving 51.88% of THP speedup in total.  In
case of the 'splash2x/ocean_ncp', 'ethp' removes 66.55% of THP memory waste
while preserving 74% of THP speedup.


Proactive Reclamation
~~~~~~~~~~~~~~~~~~~~

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
3.24% runtime overhead in total while achieving 2.94% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads:

rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    589877.400   591587.600   (0.29)     593797.000   (0.66)     591090.800   (0.21)     424841.800   (-27.98)
parsec3/bodytrack       32326.600    32289.800    (-0.11)    32284.000    (-0.13)    32249.600    (-0.24)    28931.800    (-10.50)
parsec3/canneal         839469.400   840116.600   (0.08)     838083.800   (-0.17)    837870.400   (-0.19)    833193.800   (-0.75)
parsec3/dedup           1194881.800  1207486.800  (1.05)     1217461.000  (1.89)     1225107.000  (2.53)     995459.400   (-16.69)
parsec3/facesim         311416.600   311812.800   (0.13)     314923.000   (1.13)     312525.200   (0.36)     195057.600   (-37.36)
parsec3/ferret          99787.800    99655.400    (-0.13)    101332.800   (1.55)     99820.400    (0.03)     93295.000    (-6.51)
parsec3/fluidanimate    531801.600   531784.800   (-0.00)    531775.400   (-0.00)    531928.600   (0.02)     432113.400   (-18.75)
parsec3/freqmine        552404.600   553054.400   (0.12)     555716.400   (0.60)     554045.600   (0.30)     157776.200   (-71.44)
parsec3/raytrace        894502.400   892753.600   (-0.20)    888306.200   (-0.69)    892790.600   (-0.19)    374962.600   (-58.08)
parsec3/streamcluster   110877.200   110846.400   (-0.03)    111255.400   (0.34)     111467.600   (0.53)     110063.400   (-0.73)
parsec3/swaptions       5637.600     5611.600     (-0.46)    5621.400     (-0.29)    5630.200     (-0.13)    4594.800     (-18.50)
parsec3/vips            31897.600    31803.800    (-0.29)    32336.400    (1.38)     32168.000    (0.85)     30496.800    (-4.39)
parsec3/x264            82068.400    81975.600    (-0.11)    83066.400    (1.22)     82656.400    (0.72)     80752.400    (-1.60)
splash2x/barnes         1210976.600  1215669.400  (0.39)     1224071.200  (1.08)     1219203.200  (0.68)     1047794.600  (-13.48)
splash2x/fft            9714139.000  9623503.600  (-0.93)    9523996.200  (-1.96)    9555242.400  (-1.64)    9050047.000  (-6.84)
splash2x/lu_cb          510368.800   510468.800   (0.02)     514496.800   (0.81)     510299.200   (-0.01)    445912.000   (-12.63)
splash2x/lu_ncb         510149.600   510325.600   (0.03)     513899.000   (0.73)     510331.200   (0.04)     465811.200   (-8.69)
splash2x/ocean_cp       3407224.400  3405827.200  (-0.04)    3437758.400  (0.90)     3394473.000  (-0.37)    3334869.600  (-2.12)
splash2x/ocean_ncp      3919511.200  3934023.000  (0.37)     7181317.200  (83.22)    5074390.600  (29.46)    3560788.200  (-9.15)
splash2x/radiosity      1474982.000  1476292.400  (0.09)     1485884.000  (0.74)     1474162.800  (-0.06)    695592.400   (-52.84)
splash2x/radix          1765313.200  1752605.000  (-0.72)    1440052.200  (-18.43)   1662186.600  (-5.84)    1888954.800  (7.00)
splash2x/raytrace       23277.600    23289.600    (0.05)     29185.600    (25.38)    26960.600    (15.82)    21139.400    (-9.19)
splash2x/volrend        44110.600    44069.200    (-0.09)    44321.600    (0.48)     44436.000    (0.74)     28610.400    (-35.14)
splash2x/water_nsquared 29412.800    29443.200    (0.10)     29470.000    (0.19)     29894.600    (1.64)     27927.800    (-5.05)
splash2x/water_spatial  655785.200   656694.400   (0.14)     655665.200   (-0.02)    656572.000   (0.12)     558691.000   (-14.81)
total                   28542100.000 28472900.000 (-0.24)    31386000.000 (9.96)     29467572.000 (3.24)     24887691.000 (-12.80)

In total, 12.80% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 17.91% of system memory usage and 71.44%
of residential sets while incurring only 1.25% runtime overhead.


Baseline and Complete Git Tree
==============================


The patches are based on the v5.6 plus v8 DAMON patchset[1] and Minchan's
``do_madvise()`` patch[2].  Minchan's patch was necessary for reuse of
``madvise()`` code in DAMON.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v6

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v6


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
parsec3/blackscholes    0.968 0.344 0.942 0.686  0.998
parsec3/bodytrack       0.774 0.480 0.703 0.554  0.811
parsec3/canneal         4.788 2.858 5.790 5.472  2.740
parsec3/dedup           0.039 0.081 0.069 0.034  0.805
parsec3/facesim         2.443 1.540 1.440 1.104  1.456
parsec3/ferret          1.290 1.120 1.215 0.740  0.425
parsec3/fluidanimate    1.684 2.888 1.106 1.372  0.927
parsec3/freqmine        1.871 1.858 1.625 1.287  2.066
parsec3/raytrace        0.858 0.906 0.294 0.994  1.362
parsec3/streamcluster   3.142 1.918 1.667 2.388  2.288
parsec3/swaptions       1.168 1.097 1.340 0.678  0.820
parsec3/vips            0.243 0.689 0.306 0.372  0.799
parsec3/x264            4.312 4.370 5.466 4.908  6.697
splash2x/barnes         0.653 0.831 0.527 0.643  6.632
splash2x/fft            0.162 0.349 0.545 0.130  2.109
splash2x/lu_cb          0.868 0.632 0.342 0.544  1.081
splash2x/lu_ncb         1.267 0.853 1.939 1.020  1.031
splash2x/ocean_cp       0.191 0.264 0.361 0.330  1.497
splash2x/ocean_ncp      0.700 0.778 0.625 10.773 8.483
splash2x/radiosity      0.735 0.168 0.522 0.542  1.524
splash2x/radix          0.087 0.170 0.071 2.221  6.093
splash2x/raytrace       0.670 0.435 0.490 0.641  0.789
splash2x/volrend        0.510 0.917 0.941 0.467  1.265
splash2x/water_nsquared 1.030 1.523 3.348 2.123  3.088
splash2x/water_spatial  0.608 0.918 0.906 0.445  3.970


memused.avg_stdev       orig       rec       thp        ethp       prcl      
parsec3/blackscholes    7177.041   457.804   4383.605   4348.211   90410.681 
parsec3/bodytrack       5242.897   6320.575  5683.372   6394.144   5840.731  
parsec3/canneal         3184.229   1720.154  2376.988   5711.706   2700.059  
parsec3/dedup           75174.152  60494.403 53569.224  68923.064  47926.768 
parsec3/facesim         1541.295   540.054   975.971    2177.974   6268.291  
parsec3/ferret          2548.279   3327.212  3115.638   2743.695   2488.756  
parsec3/fluidanimate    2285.138   454.034   11107.196  4070.637   32511.547 
parsec3/freqmine        1755.124   3088.623  778.824    1678.288   16653.887 
parsec3/raytrace        6131.038   4045.303  4256.371   1799.231   24729.930 
parsec3/streamcluster   1343.654   4528.505  3951.744   1858.964   1130.358  
parsec3/swaptions       1068.925   904.881   1160.615   1238.473   1084.037  
parsec3/vips            23036.966  25271.459 25392.252  23114.475  23138.259 
parsec3/x264            25651.583  20632.548 38064.938  35408.944  16795.800 
splash2x/barnes         4225.837   3197.037  3310.488   6113.420   105424.423
splash2x/fft            128834.428 75798.685 183232.230 43288.238  224031.825
splash2x/lu_cb          1938.978   454.602   2015.104   3397.805   31045.866 
splash2x/lu_ncb         3060.666   869.136   1879.629   2685.733   35044.952 
splash2x/ocean_cp       4597.261   32439.088 7665.899   8222.192   33961.730 
splash2x/ocean_ncp      4847.339   3113.802  5683.014   778490.022 90581.566 
splash2x/radiosity      3893.081   1810.792  2043.399   2626.202   177520.049
splash2x/radix          19154.048  7288.461  9847.327   124484.757 258540.188
splash2x/raytrace       2169.427   2412.817  1787.911   1361.230   1678.738  
splash2x/volrend        2564.985   2683.642  1635.776   1848.565   1977.785  
splash2x/water_nsquared 3428.873   4165.502  50631.216  3947.463   1756.141  
splash2x/water_spatial  2112.921   2028.010  1121.394   1016.742   53388.242 


rss.avg_stdev           orig      rec        thp        ethp       prcl      
parsec3/blackscholes    1948.347  2433.569   2378.384   2546.064   141780.371
parsec3/bodytrack       100.009   108.831    202.572    54.224     1675.922  
parsec3/canneal         2843.308  870.076    1212.655   1663.087   3129.842  
parsec3/dedup           38480.204 37967.975  15786.446  23358.440  284791.123
parsec3/facesim         424.304   261.595    1150.830   662.450    9413.968  
parsec3/ferret          42.593    242.034    793.083    192.745    994.699   
parsec3/fluidanimate    21.257    28.778     310.882    303.923    19284.083 
parsec3/freqmine        529.478   774.065    968.572    1323.927   29768.495 
parsec3/raytrace        1482.470  1229.837   2779.796   1418.954   50787.532 
parsec3/streamcluster   22.319    38.014     491.406    576.730    63.361    
parsec3/swaptions       25.843    40.859     55.464     33.796     304.075   
parsec3/vips            290.065   108.796    325.525    250.876    694.694   
parsec3/x264            335.630   239.158    689.243    404.686    833.712   
splash2x/barnes         8046.904  1798.924   2887.496   2314.673   209795.080
splash2x/fft            30165.250 150262.589 166604.977 162264.945 719396.468
splash2x/lu_cb          527.638   604.646    41.388     381.418    32601.509 
splash2x/lu_ncb         346.281   475.799    44.815     372.379    35382.669 
splash2x/ocean_cp       5392.266  6318.058   5840.898   31620.831  143325.540
splash2x/ocean_ncp      19098.959 4778.973   4479.653   810126.522 301918.519
splash2x/radiosity      2843.715  2147.503   201.863    1735.611   224067.639
splash2x/radix          18088.585 61704.051  8055.302   125733.584 144404.870
splash2x/raytrace       35.831    44.585     383.980    508.757    1280.050  
splash2x/volrend        94.167    53.868     276.262    293.683    4758.048  
splash2x/water_nsquared 25.849    31.638     212.514    405.700    845.312   
splash2x/water_spatial  1029.682  932.309    761.867    740.982    72889.161 


runtime_min             orig    rec     thp     ethp    prcl   
parsec3/blackscholes    106.213 106.719 105.610 106.572 106.305
parsec3/bodytrack       78.427  78.711  78.362  78.724  79.217 
parsec3/canneal         129.649 134.706 119.055 122.529 144.037
parsec3/dedup           11.851  11.784  11.615  11.879  11.902 
parsec3/facesim         206.797 207.940 203.154 205.152 211.611
parsec3/ferret          189.671 190.004 189.038 190.212 191.398
parsec3/fluidanimate    210.159 211.238 208.214 209.906 213.499
parsec3/freqmine        286.665 287.817 285.679 287.323 289.858
parsec3/raytrace        117.768 118.418 118.218 118.024 127.414
parsec3/streamcluster   318.597 324.726 282.782 287.918 327.838
parsec3/swaptions       152.893 153.795 153.263 154.382 154.420
parsec3/vips            58.626  58.587  57.937  58.605  58.739 
parsec3/x264            63.521  62.478  61.051  62.186  63.234 
splash2x/barnes         79.714  79.898  73.805  77.650  80.935 
splash2x/fft            33.244  32.718  22.796  33.156  33.335 
splash2x/lu_cb          84.805  85.147  84.230  85.163  86.571 
splash2x/lu_ncb         91.566  92.375  89.347  92.681  93.086 
splash2x/ocean_cp       44.232  44.353  42.633  43.694  44.871 
splash2x/ocean_ncp      80.368  80.533  50.574  52.553  82.318 
splash2x/radiosity      90.339  90.913  89.892  90.920  96.185 
splash2x/radix          31.162  30.934  25.018  25.910  31.431 
splash2x/raytrace       83.350  83.738  81.611  82.822  83.847 
splash2x/volrend        86.300  86.856  85.510  86.249  86.766 
splash2x/water_nsquared 229.303 231.021 215.812 220.298 232.670
splash2x/water_spatial  88.469  88.853  88.331  88.929  90.077 


memused.avg_min         orig        rec         thp         ethp        prcl       
parsec3/blackscholes    1809364.000 1835314.000 1813451.000 1826121.000 1630498.000
parsec3/bodytrack       1408819.000 1422249.000 1408973.000 1419504.000 1421510.000
parsec3/canneal         1039707.000 1049621.000 1039820.000 1037775.000 1046253.000
parsec3/dedup           2272302.000 2305775.000 2324269.000 2284678.000 2340384.000
parsec3/facesim         538642.000  553479.000  543879.000  549316.000  482729.000 
parsec3/ferret          314537.000  326866.000  316134.000  326033.000  324641.000 
parsec3/fluidanimate    573472.000  587097.000  576695.000  577384.000  472345.000 
parsec3/freqmine        983999.000  990841.000  988803.000  994674.000  790472.000 
parsec3/raytrace        1740317.000 1745978.000 1736373.000 1751865.000 1571822.000
parsec3/streamcluster   119245.000  129308.000  111429.000  130599.000  131169.000 
parsec3/swaptions       13520.000   27454.000   14709.000   24348.000   26375.000  
parsec3/vips            2895249.000 2916070.000 2892703.000 2915398.000 2922162.000
parsec3/x264            3148004.000 3158819.000 3108839.000 3132153.000 3184442.000
splash2x/barnes         1204329.000 1207582.000 1213471.000 1209854.000 972893.000 
splash2x/fft            9103855.000 9030923.000 8821853.000 9076768.000 9093310.000
splash2x/lu_cb          512799.000  523165.000  518433.000  517195.000  434908.000 
splash2x/lu_ncb         511022.000  524206.000  518261.000  518286.000  429588.000 
splash2x/ocean_cp       3339946.000 3275718.000 3364925.000 3273474.000 3205815.000
splash2x/ocean_ncp      3897002.000 3867698.000 7062400.000 3896748.000 3656014.000
splash2x/radiosity      1466117.000 1468188.000 1478459.000 1463064.000 501738.000 
splash2x/radix          1662706.000 1660223.000 1373499.000 1343854.000 1676546.000
splash2x/raytrace       42092.000   53347.000   47940.000   58506.000   54551.000  
splash2x/volrend        147965.000  160264.000  149453.000  159835.000  158427.000 
splash2x/water_nsquared 41183.000   52633.000   38589.000   51521.000   56762.000  
splash2x/water_spatial  663324.000  669966.000  665693.000  673253.000  524625.000 


rss.avg_min             orig        rec         thp         ethp        prcl       
parsec3/blackscholes    588193.000  588546.000  590578.000  588837.000  281664.000 
parsec3/bodytrack       32217.000   32174.000   31990.000   32145.000   26986.000  
parsec3/canneal         834005.000  839363.000  836407.000  836148.000  829406.000 
parsec3/dedup           1139762.000 1139860.000 1203145.000 1178413.000 576406.000 
parsec3/facesim         310806.000  311600.000  313230.000  311588.000  180985.000 
parsec3/ferret          99717.000   99183.000   99762.000   99614.000   91774.000  
parsec3/fluidanimate    531788.000  531756.000  531612.000  531756.000  412085.000 
parsec3/freqmine        551729.000  551528.000  554331.000  552060.000  129924.000 
parsec3/raytrace        893291.000  890536.000  883761.000  890842.000  313465.000 
parsec3/streamcluster   110834.000  110805.000  110841.000  110949.000  109970.000 
parsec3/swaptions       5592.000    5571.000    5552.000    5592.000    4250.000   
parsec3/vips            31440.000   31691.000   31986.000   31859.000   29553.000  
parsec3/x264            81417.000   81643.000   82326.000   82033.000   79253.000  
splash2x/barnes         1195609.000 1212619.000 1220852.000 1215687.000 729684.000 
splash2x/fft            9669070.000 9337348.000 9256159.000 9346205.000 8074101.000
splash2x/lu_cb          509681.000  509389.000  514458.000  509917.000  422503.000 
splash2x/lu_ncb         509519.000  509445.000  513827.000  509706.000  414464.000 
splash2x/ocean_cp       3400068.000 3395874.000 3427799.000 3332119.000 3048358.000
splash2x/ocean_ncp      3882042.000 3924954.000 7174037.000 3958785.000 3185248.000
splash2x/radiosity      1469611.000 1472058.000 1485612.000 1472430.000 273727.000 
splash2x/radix          1729868.000 1629239.000 1429951.000 1433396.000 1782016.000
splash2x/raytrace       23236.000   23240.000   28648.000   26000.000   19658.000  
splash2x/volrend        43949.000   43989.000   43993.000   44031.000   21790.000  
splash2x/water_nsquared 29380.000   29392.000   29228.000   29376.000   26512.000  
splash2x/water_spatial  654377.000  655129.000  654335.000  655277.000  468940.000 


runtime_max             orig    rec     thp     ethp    prcl   
parsec3/blackscholes    108.604 107.629 108.206 108.454 108.968
parsec3/bodytrack       80.519  80.010  80.260  80.198  81.417 
parsec3/canneal         142.951 142.540 132.566 138.925 152.082
parsec3/dedup           11.959  12.039  11.800  11.965  13.637 
parsec3/facesim         213.169 211.975 206.659 208.279 215.987
parsec3/ferret          192.947 193.345 192.037 192.256 192.563
parsec3/fluidanimate    214.657 219.035 211.089 213.763 215.946
parsec3/freqmine        290.961 292.748 290.256 290.867 294.994
parsec3/raytrace        120.291 120.611 119.136 120.754 131.076
parsec3/streamcluster   328.033 330.483 286.858 294.857 334.514
parsec3/swaptions       156.425 156.669 156.413 156.223 156.838
parsec3/vips            59.286  60.539  58.863  59.658  60.912 
parsec3/x264            75.251  73.295  73.890  74.545  79.278 
splash2x/barnes         81.656  82.246  75.137  79.126  98.318 
splash2x/fft            33.703  33.682  24.230  33.540  38.137 
splash2x/lu_cb          87.183  86.863  85.175  86.592  89.392 
splash2x/lu_ncb         94.827  94.785  94.728  95.244  95.473 
splash2x/ocean_cp       44.728  45.108  43.582  44.699  48.746 
splash2x/ocean_ncp      81.929  82.763  52.385  80.177  104.032
splash2x/radiosity      92.328  91.331  91.345  92.441  100.571
splash2x/radix          31.367  31.428  25.230  31.416  46.456 
splash2x/raytrace       85.100  84.986  83.149  84.551  86.136 
splash2x/volrend        87.706  89.353  88.107  87.616  90.051 
splash2x/water_nsquared 232.325 235.516 225.639 226.242 240.900
splash2x/water_spatial  90.178  91.288  90.820  90.247  99.400 


memused.avg_max         orig        rec         thp         ethp        prcl       
parsec3/blackscholes    1830880.000 1836638.000 1825073.000 1838413.000 1834983.000
parsec3/bodytrack       1424251.000 1439700.000 1424370.000 1437457.000 1436895.000
parsec3/canneal         1048704.000 1054234.000 1046373.000 1054487.000 1054289.000
parsec3/dedup           2454882.000 2468178.000 2446912.000 2475403.000 2467548.000
parsec3/facesim         542743.000  555087.000  546519.000  555677.000  498430.000 
parsec3/ferret          320770.000  334671.000  323424.000  333681.000  331047.000 
parsec3/fluidanimate    579432.000  588311.000  604938.000  587856.000  562852.000 
parsec3/freqmine        988347.000  999098.000  991131.000  999570.000  840085.000 
parsec3/raytrace        1759165.000 1757903.000 1748236.000 1756988.000 1643533.000
parsec3/streamcluster   122665.000  142103.000  122903.000  135396.000  134189.000 
parsec3/swaptions       16468.000   29888.000   18057.000   27659.000   29626.000  
parsec3/vips            2961506.000 2985347.000 2959342.000 2978053.000 2983825.000
parsec3/x264            3213579.000 3213069.000 3213048.000 3226493.000 3226443.000
splash2x/barnes         1216327.000 1216342.000 1222147.000 1228269.000 1213209.000
splash2x/fft            9447877.000 9242495.000 9320845.000 9202797.000 9618066.000
splash2x/lu_cb          517854.000  524223.000  523710.000  527836.000  522041.000 
splash2x/lu_ncb         518300.000  526492.000  523630.000  526485.000  518812.000 
splash2x/ocean_cp       3352591.000 3363253.000 3388442.000 3295938.000 3297358.000
splash2x/ocean_ncp      3911762.000 3876720.000 7075796.000 5910359.000 3874687.000
splash2x/radiosity      1475186.000 1473229.000 1484861.000 1471090.000 985970.000 
splash2x/radix          1715992.000 1681228.000 1397396.000 1663675.000 2334163.000
splash2x/raytrace       47569.000   59269.000   52479.000   61762.000   59195.000  
splash2x/volrend        155099.000  168450.000  153224.000  165443.000  163687.000 
splash2x/water_nsquared 49437.000   63025.000   172478.000  62050.000   61817.000  
splash2x/water_spatial  669174.000  675915.000  668663.000  676081.000  675431.000 


rss.avg_max             orig        rec         thp         ethp        prcl       
parsec3/blackscholes    593417.000  593646.000  596394.000  594292.000  593634.000 
parsec3/bodytrack       32477.000   32478.000   32515.000   32303.000   31140.000  
parsec3/canneal         842209.000  841734.000  839817.000  841008.000  836965.000 
parsec3/dedup           1237156.000 1236922.000 1242695.000 1237438.000 1235870.000
parsec3/facesim         311877.000  312295.000  316325.000  313520.000  208345.000 
parsec3/ferret          99830.000   99829.000   101852.000  100168.000  94664.000  
parsec3/fluidanimate    531844.000  531828.000  532397.000  532536.000  468698.000 
parsec3/freqmine        553285.000  553677.000  556678.000  556215.000  214825.000 
parsec3/raytrace        897336.000  894063.000  891228.000  894188.000  437099.000 
parsec3/streamcluster   110896.000  110898.000  112198.000  112186.000  110167.000 
parsec3/swaptions       5662.000    5680.000    5689.000    5679.000    4975.000   
parsec3/vips            32174.000   31965.000   32901.000   32450.000   31694.000  
parsec3/x264            82322.000   82258.000   84176.000   83236.000   81624.000  
splash2x/barnes         1218530.000 1218027.000 1228857.000 1222907.000 1216512.000
splash2x/fft            9739818.000 9754926.000 9671148.000 9719001.000 9749458.000
splash2x/lu_cb          511047.000  511136.000  514576.000  511007.000  510434.000 
splash2x/lu_ncb         510527.000  510766.000  513958.000  510825.000  505870.000 
splash2x/ocean_cp       3416062.000 3412547.000 3445290.000 3416314.000 3413927.000
splash2x/ocean_ncp      3934963.000 3938964.000 7186934.000 6003152.000 3934187.000
splash2x/radiosity      1477524.000 1477620.000 1486136.000 1476725.000 899792.000 
splash2x/radix          1780157.000 1786530.000 1448784.000 1775975.000 2161045.000
splash2x/raytrace       23336.000   23348.000   29600.000   27407.000   22914.000  
splash2x/volrend        44244.000   44155.000   44711.000   44789.000   34089.000  
splash2x/water_nsquared 29444.000   29492.000   29825.000   30323.000   28810.000  
splash2x/water_spatial  657265.000  657890.000  656649.000  657176.000  656735.000 
