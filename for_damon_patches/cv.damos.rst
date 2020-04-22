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
parsec3/blackscholes    107.065  107.478  (0.39)     106.682  (-0.36)    107.365  (0.28)     111.811  (4.43)
parsec3/bodytrack       79.256   79.450   (0.25)     78.645   (-0.77)    79.314   (0.07)     80.305   (1.32)
parsec3/canneal         139.497  141.181  (1.21)     121.526  (-12.88)   130.074  (-6.75)    154.644  (10.86)
parsec3/dedup           11.879   11.873   (-0.05)    11.693   (-1.56)    11.948   (0.58)     12.694   (6.86)
parsec3/facesim         207.814  208.467  (0.31)     203.743  (-1.96)    206.759  (-0.51)    214.603  (3.27)
parsec3/ferret          190.124  190.955  (0.44)     189.575  (-0.29)    190.852  (0.38)     191.548  (0.75)
parsec3/fluidanimate    211.046  212.282  (0.59)     208.832  (-1.05)    212.143  (0.52)     218.774  (3.66)
parsec3/freqmine        289.259  290.096  (0.29)     288.510  (-0.26)    290.177  (0.32)     296.269  (2.42)
parsec3/raytrace        118.522  119.701  (0.99)     119.469  (0.80)     118.964  (0.37)     130.584  (10.18)
parsec3/streamcluster   323.619  327.830  (1.30)     283.374  (-12.44)   287.837  (-11.06)   330.216  (2.04)
parsec3/swaptions       154.007  155.714  (1.11)     154.767  (0.49)     154.955  (0.62)     155.256  (0.81)
parsec3/vips            58.822   58.750   (-0.12)    58.564   (-0.44)    58.807   (-0.02)    60.320   (2.55)
parsec3/x264            67.335   72.516   (7.69)     64.680   (-3.94)    70.096   (4.10)     72.465   (7.62)
splash2x/barnes         80.335   80.979   (0.80)     73.758   (-8.19)    78.874   (-1.82)    99.226   (23.52)
splash2x/fft            33.441   33.312   (-0.38)    22.909   (-31.49)   31.561   (-5.62)    41.496   (24.09)
splash2x/lu_cb          85.691   85.706   (0.02)     84.352   (-1.56)    85.943   (0.29)     88.914   (3.76)
splash2x/lu_ncb         92.338   92.749   (0.45)     89.773   (-2.78)    92.888   (0.60)     94.104   (1.91)
splash2x/ocean_cp       44.542   44.795   (0.57)     42.958   (-3.56)    44.061   (-1.08)    49.091   (10.21)
splash2x/ocean_ncp      82.101   82.006   (-0.12)    51.418   (-37.37)   64.496   (-21.44)   105.998  (29.11)
splash2x/radiosity      91.296   91.353   (0.06)     90.668   (-0.69)    91.379   (0.09)     103.265  (13.11)
splash2x/radix          31.243   31.417   (0.56)     25.176   (-19.42)   30.297   (-3.03)    38.474   (23.14)
splash2x/raytrace       84.405   84.863   (0.54)     83.498   (-1.08)    83.637   (-0.91)    85.166   (0.90)
splash2x/volrend        87.516   88.156   (0.73)     86.311   (-1.38)    87.016   (-0.57)    88.318   (0.92)
splash2x/water_nsquared 233.515  233.826  (0.13)     221.169  (-5.29)    224.430  (-3.89)    236.929  (1.46)
splash2x/water_spatial  89.207   89.448   (0.27)     89.396   (0.21)     89.826   (0.69)     97.700   (9.52)
total                   2993.890 3014.920 (0.70)     2851.460 (-4.76)    2923.710 (-2.34)    3158.180 (5.49)


memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    1819997.200  1832626.000  (0.69)     1821707.000  (0.09)     1830010.400  (0.55)     1651016.200  (-9.28)
parsec3/bodytrack       1416437.600  1430462.200  (0.99)     1420736.400  (0.30)     1428355.600  (0.84)     1430327.000  (0.98)
parsec3/canneal         1040414.400  1050736.800  (0.99)     1041515.600  (0.11)     1048562.200  (0.78)     1049049.400  (0.83)
parsec3/dedup           2414431.800  2454260.400  (1.65)     2423175.400  (0.36)     2396560.200  (-0.74)    2379898.200  (-1.43)
parsec3/facesim         540432.200   551410.200   (2.03)     545978.200   (1.03)     558558.400   (3.35)     483755.400   (-10.49)
parsec3/ferret          318728.600   333971.800   (4.78)     322158.200   (1.08)     332889.200   (4.44)     327896.400   (2.88)
parsec3/fluidanimate    576917.800   585126.600   (1.42)     575123.200   (-0.31)    585429.200   (1.48)     484810.600   (-15.97)
parsec3/freqmine        987882.200   997030.600   (0.93)     990429.200   (0.26)     998484.000   (1.07)     770740.200   (-21.98)
parsec3/raytrace        1747059.800  1752904.000  (0.33)     1738853.600  (-0.47)    1753948.600  (0.39)     1578118.000  (-9.67)
parsec3/streamcluster   121857.600   133934.400   (9.91)     121777.800   (-0.07)    133145.800   (9.26)     131512.800   (7.92)
parsec3/swaptions       14123.000    29254.400    (107.14)   14017.200    (-0.75)    26470.600    (87.43)    28429.800    (101.30)
parsec3/vips            2957631.800  2972884.400  (0.52)     2938855.400  (-0.63)    2960746.000  (0.11)     2946850.800  (-0.36)
parsec3/x264            3184777.200  3214527.400  (0.93)     3177061.000  (-0.24)    3192446.600  (0.24)     3185851.800  (0.03)
splash2x/barnes         1209737.200  1214763.200  (0.42)     1242138.400  (2.68)     1215857.600  (0.51)     994280.800   (-17.81)
splash2x/fft            9362799.400  9178844.600  (-1.96)    9264052.600  (-1.05)    9164996.600  (-2.11)    9452048.200  (0.95)
splash2x/lu_cb          515716.000   524071.600   (1.62)     521226.200   (1.07)     524261.400   (1.66)     372910.200   (-27.69)
splash2x/lu_ncb         512898.200   523057.600   (1.98)     520630.800   (1.51)     523779.000   (2.12)     446282.400   (-12.99)
splash2x/ocean_cp       3346038.000  3288703.600  (-1.71)    3386906.600  (1.22)     3330937.200  (-0.45)    3266442.400  (-2.38)
splash2x/ocean_ncp      3886945.600  3871894.000  (-0.39)    7066192.000  (81.79)    5065229.800  (30.31)    3652078.200  (-6.04)
splash2x/radiosity      1467107.200  1468850.800  (0.12)     1481292.600  (0.97)     1470335.800  (0.22)     530923.400   (-63.81)
splash2x/radix          1708330.800  1699792.200  (-0.50)    1352708.600  (-20.82)   1601339.200  (-6.26)    2043947.800  (19.65)
splash2x/raytrace       44817.200    59047.800    (31.75)    52010.200    (16.05)    60407.200    (34.79)    53916.400    (20.30)
splash2x/volrend        151534.200   167791.400   (10.73)    151759.000   (0.15)     165012.400   (8.89)     160864.600   (6.16)
splash2x/water_nsquared 46549.400    61846.800    (32.86)    51741.200    (11.15)    59214.400    (27.21)    91869.400    (97.36)
splash2x/water_spatial  669085.200   675929.200   (1.02)     665924.600   (-0.47)    676218.200   (1.07)     538430.200   (-19.53)
total                   40062200.000 40073700.000 (0.03)     42888000.000 (7.05)     41103100.000 (2.60)     38052200.000 (-5.02)


DAMON Overheads
~~~~~~~~~~~~~~~

In total, DAMON recording feature incurs 0.70% runtime overhead and 0.03%
memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  Nonetheless, the overheads
are not from DAMON, but from the wrapper, and thus should be ignored.  This
fake memory overhead continues in 'ethp' and 'prcl', as those configurations
are also using the Python wrapper.


Efficient THP
~~~~~~~~~~~~~

THP 'always' enabled policy achieves 4.76% speedup but incurs 7.05% memory
overhead.  It achieves 37.37% speedup in best case, but 81.79% memory overhead
in worst case.  Interestingly, both the best and worst case are with
'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.34% speedup and 2.60% memory overhead.  In other words, 'ethp' removes
63.12% of THP memory waste while preserving 49.15% of THP speedup in total.  In
case of the 'splash2x/ocean_ncp', 'ethp' removes 62.94% of THP memory waste
while preserving 57.37% of THP speedup.


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
parsec3/blackscholes    591452.000   591466.400   (0.00)     593145.200   (0.29)     590609.400   (-0.14)    324379.000   (-45.16)
parsec3/bodytrack       32458.600    32352.200    (-0.33)    32218.400    (-0.74)    32376.400    (-0.25)    27186.000    (-16.24)
parsec3/canneal         841311.600   839888.400   (-0.17)    837008.400   (-0.51)    837811.000   (-0.42)    823276.200   (-2.14)
parsec3/dedup           1219096.600  1228038.800  (0.73)     1235610.800  (1.35)     1214267.000  (-0.40)    992031.000   (-18.63)
parsec3/facesim         311322.200   311574.400   (0.08)     316277.000   (1.59)     312593.800   (0.41)     188789.400   (-39.36)
parsec3/ferret          99536.600    99556.800    (0.02)     102366.000   (2.84)     99799.000    (0.26)     88392.000    (-11.20)
parsec3/fluidanimate    531893.600   531856.000   (-0.01)    532143.400   (0.05)     532190.200   (0.06)     421798.800   (-20.70)
parsec3/freqmine        553533.200   552730.400   (-0.15)    555642.600   (0.38)     553895.400   (0.07)     78335.000    (-85.85)
parsec3/raytrace        894094.200   894849.000   (0.08)     889964.000   (-0.46)    892865.000   (-0.14)    332911.800   (-62.77)
parsec3/streamcluster   110938.000   110968.200   (0.03)     111673.400   (0.66)     111312.200   (0.34)     109911.200   (-0.93)
parsec3/swaptions       5630.000     5634.800     (0.09)     5656.600     (0.47)     5692.000     (1.10)     4028.400     (-28.45)
parsec3/vips            32107.000    32045.200    (-0.19)    32207.800    (0.31)     32293.800    (0.58)     29093.600    (-9.39)
parsec3/x264            81926.000    82143.000    (0.26)     83258.400    (1.63)     82570.600    (0.79)     80651.800    (-1.56)
splash2x/barnes         1215468.800  1217889.800  (0.20)     1222006.800  (0.54)     1217425.600  (0.16)     752405.200   (-38.10)
splash2x/fft            9584734.800  9568872.800  (-0.17)    9660321.400  (0.79)     9646012.000  (0.64)     8367492.800  (-12.70)
splash2x/lu_cb          510555.400   510807.400   (0.05)     514448.600   (0.76)     509281.800   (-0.25)    349272.200   (-31.59)
splash2x/lu_ncb         510310.000   508915.600   (-0.27)    513886.000   (0.70)     510288.400   (-0.00)    431521.800   (-15.44)
splash2x/ocean_cp       3408724.400  3408424.600  (-0.01)    3446054.400  (1.10)     3419536.200  (0.32)     3173818.600  (-6.89)
splash2x/ocean_ncp      3923539.600  3922605.400  (-0.02)    7175526.600  (82.88)    5152558.800  (31.32)    3475756.000  (-11.41)
splash2x/radiosity      1476050.000  1475470.400  (-0.04)    1485747.000  (0.66)     1476232.600  (0.01)     269512.200   (-81.74)
splash2x/radix          1756385.400  1752676.000  (-0.21)    1431621.600  (-18.49)   1711460.800  (-2.56)    1923448.200  (9.51)
splash2x/raytrace       23286.400    23311.200    (0.11)     28440.800    (22.13)    26977.200    (15.85)    15685.200    (-32.64)
splash2x/volrend        44089.400    44125.600    (0.08)     44436.600    (0.79)     44250.400    (0.37)     27616.800    (-37.36)
splash2x/water_nsquared 29437.600    29403.200    (-0.12)    29817.400    (1.29)     30040.000    (2.05)     25369.600    (-13.82)
splash2x/water_spatial  656264.400   656566.400   (0.05)     656016.400   (-0.04)    656420.200   (0.02)     474480.400   (-27.70)
total                   28444100.000 28432200.000 (-0.04)    31535300.000 (10.87)    29698900.000 (4.41)     22787200.000 (-19.89)

In total, 19.89% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 85.85% of residential sets and 21.98% of
system memory usage while incurring only 2.42% runtime overhead.


Baseline and Complete Git Tree
==============================


The patches are based on the v5.6 plus v11 DAMON patchset[1] and Minchan's
``do_madvise()`` patch[2].  Minchan's patch was necessary for reuse of
``madvise()`` code in DAMON.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v8

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v8

The latest DAMON development tree is also available at:
https://github.com/sjp38/linux/tree/damon/master


[1] https://lore.kernel.org/linux-mm/20200511123302.12520-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200302193630.68771-2-minchan@kernel.org/


Sequence Of Patches
===================

The first patch allows DAMON to reuse ``madvise()`` code for the actions.  The
second patch accounts age of each region.  The third patch implements the
handling of the schemes in DAMON and exports a kernel space programming
interface for it.  The fourth patch implements a debugfs interface for the
privileged people and programs.  The fifth and sixth patches each adds kunit
tests and selftests for these changes, and the seventhe patch adds human
friendly schemes support to the user space tool for DAMON.  Finally, the eighth
patch documents this new feature in the document.


Patch History
=============

Changes from RFC v8
(https://lore.kernel.org/linux-mm/20200512115343.27699-1-sjpark@amazon.com/)
 - Rewrite the document (Stefan Nuernberger)
 - Make 'damon_for_each_*' argument order consistent (Leonard Foerster)
 - Implement schemes application stat
 - Avoid races between debugfs readers and writers

Changes from RFC v7
(https://lore.kernel.org/linux-mm/20200429124540.32232-1-sjpark@amazon.com/)
 - Rebase on DAMON v11 patchset
 - Add documentation

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

runtime_avg             orig    rec     thp     ethp    prcl
parsec3/blackscholes    107.065 107.478 106.682 107.365 111.811
parsec3/bodytrack       79.256  79.450  78.645  79.314  80.305
parsec3/canneal         139.497 141.181 121.526 130.074 154.644
parsec3/dedup           11.879  11.873  11.693  11.948  12.694
parsec3/facesim         207.814 208.467 203.743 206.759 214.603
parsec3/ferret          190.124 190.955 189.575 190.852 191.548
parsec3/fluidanimate    211.046 212.282 208.832 212.143 218.774
parsec3/freqmine        289.259 290.096 288.510 290.177 296.269
parsec3/raytrace        118.522 119.701 119.469 118.964 130.584
parsec3/streamcluster   323.619 327.830 283.374 287.837 330.216
parsec3/swaptions       154.007 155.714 154.767 154.955 155.256
parsec3/vips            58.822  58.750  58.564  58.807  60.320
parsec3/x264            67.335  72.516  64.680  70.096  72.465
splash2x/barnes         80.335  80.979  73.758  78.874  99.226
splash2x/fft            33.441  33.312  22.909  31.561  41.496
splash2x/lu_cb          85.691  85.706  84.352  85.943  88.914
splash2x/lu_ncb         92.338  92.749  89.773  92.888  94.104
splash2x/ocean_cp       44.542  44.795  42.958  44.061  49.091
splash2x/ocean_ncp      82.101  82.006  51.418  64.496  105.998
splash2x/radiosity      91.296  91.353  90.668  91.379  103.265
splash2x/radix          31.243  31.417  25.176  30.297  38.474
splash2x/raytrace       84.405  84.863  83.498  83.637  85.166
splash2x/volrend        87.516  88.156  86.311  87.016  88.318
splash2x/water_nsquared 233.515 233.826 221.169 224.430 236.929
splash2x/water_spatial  89.207  89.448  89.396  89.826  97.700


memused.avg_avg         orig        rec         thp         ethp        prcl
parsec3/blackscholes    1819997.200 1832626.000 1821707.000 1830010.400 1651016.200
parsec3/bodytrack       1416437.600 1430462.200 1420736.400 1428355.600 1430327.000
parsec3/canneal         1040414.400 1050736.800 1041515.600 1048562.200 1049049.400
parsec3/dedup           2414431.800 2454260.400 2423175.400 2396560.200 2379898.200
parsec3/facesim         540432.200  551410.200  545978.200  558558.400  483755.400
parsec3/ferret          318728.600  333971.800  322158.200  332889.200  327896.400
parsec3/fluidanimate    576917.800  585126.600  575123.200  585429.200  484810.600
parsec3/freqmine        987882.200  997030.600  990429.200  998484.000  770740.200
parsec3/raytrace        1747059.800 1752904.000 1738853.600 1753948.600 1578118.000
parsec3/streamcluster   121857.600  133934.400  121777.800  133145.800  131512.800
parsec3/swaptions       14123.000   29254.400   14017.200   26470.600   28429.800
parsec3/vips            2957631.800 2972884.400 2938855.400 2960746.000 2946850.800
parsec3/x264            3184777.200 3214527.400 3177061.000 3192446.600 3185851.800
splash2x/barnes         1209737.200 1214763.200 1242138.400 1215857.600 994280.800
splash2x/fft            9362799.400 9178844.600 9264052.600 9164996.600 9452048.200
splash2x/lu_cb          515716.000  524071.600  521226.200  524261.400  372910.200
splash2x/lu_ncb         512898.200  523057.600  520630.800  523779.000  446282.400
splash2x/ocean_cp       3346038.000 3288703.600 3386906.600 3330937.200 3266442.400
splash2x/ocean_ncp      3886945.600 3871894.000 7066192.000 5065229.800 3652078.200
splash2x/radiosity      1467107.200 1468850.800 1481292.600 1470335.800 530923.400
splash2x/radix          1708330.800 1699792.200 1352708.600 1601339.200 2043947.800
splash2x/raytrace       44817.200   59047.800   52010.200   60407.200   53916.400
splash2x/volrend        151534.200  167791.400  151759.000  165012.400  160864.600
splash2x/water_nsquared 46549.400   61846.800   51741.200   59214.400   91869.400
splash2x/water_spatial  669085.200  675929.200  665924.600  676218.200  538430.200


rss.avg_avg             orig        rec         thp         ethp        prcl
parsec3/blackscholes    591452.000  591466.400  593145.200  590609.400  324379.000
parsec3/bodytrack       32458.600   32352.200   32218.400   32376.400   27186.000
parsec3/canneal         841311.600  839888.400  837008.400  837811.000  823276.200
parsec3/dedup           1219096.600 1228038.800 1235610.800 1214267.000 992031.000
parsec3/facesim         311322.200  311574.400  316277.000  312593.800  188789.400
parsec3/ferret          99536.600   99556.800   102366.000  99799.000   88392.000
parsec3/fluidanimate    531893.600  531856.000  532143.400  532190.200  421798.800
parsec3/freqmine        553533.200  552730.400  555642.600  553895.400  78335.000
parsec3/raytrace        894094.200  894849.000  889964.000  892865.000  332911.800
parsec3/streamcluster   110938.000  110968.200  111673.400  111312.200  109911.200
parsec3/swaptions       5630.000    5634.800    5656.600    5692.000    4028.400
parsec3/vips            32107.000   32045.200   32207.800   32293.800   29093.600
parsec3/x264            81926.000   82143.000   83258.400   82570.600   80651.800
splash2x/barnes         1215468.800 1217889.800 1222006.800 1217425.600 752405.200
splash2x/fft            9584734.800 9568872.800 9660321.400 9646012.000 8367492.800
splash2x/lu_cb          510555.400  510807.400  514448.600  509281.800  349272.200
splash2x/lu_ncb         510310.000  508915.600  513886.000  510288.400  431521.800
splash2x/ocean_cp       3408724.400 3408424.600 3446054.400 3419536.200 3173818.600
splash2x/ocean_ncp      3923539.600 3922605.400 7175526.600 5152558.800 3475756.000
splash2x/radiosity      1476050.000 1475470.400 1485747.000 1476232.600 269512.200
splash2x/radix          1756385.400 1752676.000 1431621.600 1711460.800 1923448.200
splash2x/raytrace       23286.400   23311.200   28440.800   26977.200   15685.200
splash2x/volrend        44089.400   44125.600   44436.600   44250.400   27616.800
splash2x/water_nsquared 29437.600   29403.200   29817.400   30040.000   25369.600
splash2x/water_spatial  656264.400  656566.400  656016.400  656420.200  474480.400


runtime_stdev           orig  rec   thp   ethp   prcl
parsec3/blackscholes    1.002 0.968 1.072 0.699  1.353
parsec3/bodytrack       0.772 0.691 0.546 0.544  0.999
parsec3/canneal         5.381 3.346 1.187 1.693  6.379
parsec3/dedup           0.098 0.063 0.047 0.119  0.835
parsec3/facesim         0.842 0.407 0.699 0.825  0.745
parsec3/ferret          0.343 0.809 0.214 1.009  0.453
parsec3/fluidanimate    0.732 0.864 1.506 1.420  1.539
parsec3/freqmine        3.491 2.325 2.746 1.057  2.781
parsec3/raytrace        0.946 0.919 1.315 0.746  0.812
parsec3/streamcluster   1.294 2.267 2.133 0.837  2.858
parsec3/swaptions       0.918 1.154 1.433 0.789  0.983
parsec3/vips            0.157 0.130 0.150 0.313  0.663
parsec3/x264            5.485 3.596 3.120 3.251  4.454
splash2x/barnes         0.401 0.827 0.213 0.734  5.511
splash2x/fft            0.245 0.372 0.067 2.244  4.638
splash2x/lu_cb          1.439 0.306 0.082 0.928  1.463
splash2x/lu_ncb         0.339 0.165 0.208 0.266  1.054
splash2x/ocean_cp       0.175 0.308 0.290 0.379  6.556
splash2x/ocean_ncp      1.485 0.939 0.477 14.037 15.558
splash2x/radiosity      0.666 0.448 0.444 0.466  1.553
splash2x/radix          0.301 0.402 0.137 1.554  6.122
splash2x/raytrace       1.053 0.853 0.786 0.535  0.275
splash2x/volrend        0.911 0.309 0.783 0.828  1.168
splash2x/water_nsquared 2.791 1.930 2.511 4.602  2.299
splash2x/water_spatial  0.795 0.696 0.794 0.548  1.663


memused.avg_stdev       orig      rec       thp       ethp       prcl
parsec3/blackscholes    5734.731  4311.105  7196.073  6250.088   38591.335
parsec3/bodytrack       5604.283  8053.736  5653.392  7457.510   6963.186
parsec3/canneal         4234.325  4698.502  1277.093  3276.701   5093.649
parsec3/dedup           57376.221 12160.066 7145.142  60455.246  83253.830
parsec3/facesim         1512.291  2418.956  3818.778  11707.979  5285.020
parsec3/ferret          2636.174  1932.627  2836.998  2171.699   3600.209
parsec3/fluidanimate    1598.216  3489.956  1799.876  2744.956   16040.475
parsec3/freqmine        4737.378  2585.209  1516.651  833.023    2835.236
parsec3/raytrace        6029.193  5301.939  3917.335  2996.082   9466.302
parsec3/streamcluster   5896.735  2787.781  1670.123  1757.718   3018.166
parsec3/swaptions       2643.941  269.967   1914.219  2180.731   544.245
parsec3/vips            1783.138  5078.111  24973.322 31076.452  38391.263
parsec3/x264            15667.795 15632.156 11233.959 25678.288  36209.397
splash2x/barnes         6571.840  8658.759  42823.767 7075.071   68590.867
splash2x/fft            85635.738 47518.810 17455.819 49252.727  363372.295
splash2x/lu_cb          1421.258  1647.129  2372.638  2105.939   17907.733
splash2x/lu_ncb         3998.792  1236.203  1718.105  3234.752   19646.973
splash2x/ocean_cp       4887.504  2266.515  2997.870  40046.934  25117.676
splash2x/ocean_ncp      25633.509 3240.085  8002.012  980010.789 122762.809
splash2x/radiosity      6798.900  2183.071  2027.795  2417.779   30261.866
splash2x/radix          39814.934 34503.616 57493.970 87549.945  235275.193
splash2x/raytrace       2875.630  1353.957  1065.653  1918.838   1356.082
splash2x/volrend        1222.512  3380.618  2242.415  1507.868   2955.215
splash2x/water_nsquared 3198.770  3823.809  16179.890 3511.015   62231.581
splash2x/water_spatial  714.574   2989.515  4637.876  2556.774   26980.198


rss.avg_stdev           orig       rec        thp       ethp        prcl
parsec3/blackscholes    2539.004   2464.031   2156.462  2413.093    53977.533
parsec3/bodytrack       94.396     226.072    121.582   149.827     1071.906
parsec3/canneal         1140.831   1332.975   1659.666  1579.872    7374.389
parsec3/dedup           20103.782  14597.251  9030.847  20039.333   314199.052
parsec3/facesim         363.581    494.161    906.467   473.521     6371.760
parsec3/ferret          199.416    138.839    697.471   78.519      1163.491
parsec3/fluidanimate    33.903     47.666     394.817   368.483     16330.277
parsec3/freqmine        410.158    981.816    637.904   1114.078    8881.688
parsec3/raytrace        1724.461   1463.681   746.438   1706.933    15221.381
parsec3/streamcluster   24.698     37.080     688.713   521.613     73.251
parsec3/swaptions       32.521     65.965     48.861    23.195      74.535
parsec3/vips            186.227    113.344    147.699   426.642     497.310
parsec3/x264            230.339    84.763     739.852   189.659     1015.909
splash2x/barnes         2068.964   1788.076   2402.419  1463.312    114524.086
splash2x/fft            175328.325 179979.020 17840.083 148550.842  818559.897
splash2x/lu_cb          256.770    397.374    23.500    2601.638    20636.424
splash2x/lu_ncb         210.223    2042.361   42.133    238.893     19596.759
splash2x/ocean_cp       4103.035   2449.065   1812.305  12470.919   299893.309
splash2x/ocean_ncp      22401.137  18053.550  5341.733  1003372.423 366901.046
splash2x/radiosity      1221.591   1271.056   349.319   710.017     40872.559
splash2x/radix          37002.584  26159.493  7497.310  81687.572   133361.762
splash2x/raytrace       30.943     36.783     456.262   545.508     1046.860
splash2x/volrend        114.991    68.611     208.251   249.526     4425.980
splash2x/water_nsquared 34.926     27.294     463.759   413.561     534.799
splash2x/water_spatial  602.384    1035.828   544.494   861.522     25796.221


runtime_min             orig    rec     thp     ethp    prcl
parsec3/blackscholes    106.122 106.677 105.919 106.619 110.383
parsec3/bodytrack       78.583  78.662  77.963  78.873  79.467
parsec3/canneal         128.976 134.898 120.434 127.203 143.840
parsec3/dedup           11.752  11.767  11.627  11.827  11.899
parsec3/facesim         206.796 207.966 202.737 205.231 213.218
parsec3/ferret          189.745 190.214 189.266 189.905 190.969
parsec3/fluidanimate    210.378 211.312 207.586 210.468 216.577
parsec3/freqmine        286.317 288.065 286.234 289.249 293.411
parsec3/raytrace        117.755 118.384 118.072 118.358 129.605
parsec3/streamcluster   321.690 325.011 279.751 286.955 326.447
parsec3/swaptions       153.185 154.154 153.310 153.993 154.328
parsec3/vips            58.688  58.537  58.430  58.451  59.520
parsec3/x264            62.342  65.581  62.383  64.317  66.676
splash2x/barnes         79.756  80.335  73.387  78.191  93.555
splash2x/fft            33.066  32.855  22.804  28.031  33.648
splash2x/lu_cb          84.857  85.454  84.211  85.408  86.695
splash2x/lu_ncb         91.931  92.534  89.463  92.670  93.163
splash2x/ocean_cp       44.342  44.283  42.531  43.634  44.678
splash2x/ocean_ncp      79.808  80.530  50.690  52.658  82.238
splash2x/radiosity      90.522  91.085  90.146  91.076  101.043
splash2x/radix          30.793  30.748  25.059  27.514  31.649
splash2x/raytrace       83.115  83.872  82.632  83.073  84.777
splash2x/volrend        86.573  87.847  85.367  86.157  87.235
splash2x/water_nsquared 230.687 231.397 216.456 218.151 234.711
splash2x/water_spatial  88.231  88.761  88.657  89.263  94.696


memused.avg_min         orig        rec         thp         ethp        prcl
parsec3/blackscholes    1811544.000 1826536.000 1808494.000 1821773.000 1614734.000
parsec3/bodytrack       1408397.000 1417771.000 1409682.000 1418166.000 1421597.000
parsec3/canneal         1033746.000 1044350.000 1039137.000 1042459.000 1039997.000
parsec3/dedup           2301102.000 2439442.000 2415716.000 2312844.000 2271479.000
parsec3/facesim         538342.000  548319.000  538855.000  548533.000  476439.000
parsec3/ferret          314307.000  330592.000  318816.000  328550.000  322669.000
parsec3/fluidanimate    575266.000  580145.000  572053.000  580210.000  471031.000
parsec3/freqmine        983385.000  992568.000  989103.000  996967.000  765382.000
parsec3/raytrace        1739135.000 1746208.000 1732673.000 1750043.000 1562113.000
parsec3/streamcluster   117830.000  129708.000  119065.000  130544.000  127311.000
parsec3/swaptions       9019.000    28779.000   10747.000   22525.000   27794.000
parsec3/vips            2955461.000 2966185.000 2889282.000 2899751.000 2899574.000
parsec3/x264            3167265.000 3184099.000 3165670.000 3147043.000 3127201.000
splash2x/barnes         1197072.000 1202375.000 1216718.000 1208018.000 927735.000
splash2x/fft            9252959.000 9127817.000 9241331.000 9070892.000 9171009.000
splash2x/lu_cb          513078.000  521249.000  516804.000  522107.000  352197.000
splash2x/lu_ncb         507538.000  520640.000  518193.000  520217.000  414060.000
splash2x/ocean_cp       3338417.000 3285969.000 3381462.000 3284112.000 3220039.000
splash2x/ocean_ncp      3850694.000 3866285.000 7054914.000 3863994.000 3533390.000
splash2x/radiosity      1458533.000 1466378.000 1478591.000 1466341.000 500186.000
splash2x/radix          1632067.000 1660555.000 1237961.000 1433203.000 1742575.000
splash2x/raytrace       41129.000   57383.000   50345.000   56853.000   51558.000
splash2x/volrend        149557.000  161324.000  149036.000  162226.000  156885.000
splash2x/water_nsquared 42148.000   54926.000   37080.000   53261.000   59453.000
splash2x/water_spatial  667829.000  672042.000  657036.000  674431.000  499066.000


rss.avg_min             orig        rec         thp         ethp        prcl
parsec3/blackscholes    588296.000  588492.000  590821.000  588424.000  275254.000
parsec3/bodytrack       32314.000   31950.000   32051.000   32229.000   25606.000
parsec3/canneal         839821.000  837971.000  835293.000  835454.000  814989.000
parsec3/dedup           1190628.000 1198910.000 1217746.000 1191978.000 387128.000
parsec3/facesim         311025.000  311017.000  315101.000  312000.000  178780.000
parsec3/ferret          99241.000   99307.000   101493.000  99715.000   87322.000
parsec3/fluidanimate    531852.000  531808.000  531716.000  531812.000  407062.000
parsec3/freqmine        553154.000  551378.000  554723.000  552409.000  60624.000
parsec3/raytrace        891883.000  892921.000  889021.000  890269.000  320336.000
parsec3/streamcluster   110908.000  110901.000  110959.000  110905.000  109822.000
parsec3/swaptions       5595.000    5569.000    5598.000    5669.000    3932.000
parsec3/vips            31813.000   31872.000   31999.000   31862.000   28309.000
parsec3/x264            81595.000   81995.000   82492.000   82257.000   79007.000
splash2x/barnes         1213152.000 1215177.000 1219337.000 1215911.000 633247.000
splash2x/fft            9352024.000 9344708.000 9635110.000 9350033.000 7315602.000
splash2x/lu_cb          510081.000  510173.000  514423.000  504167.000  328613.000
splash2x/lu_ncb         510113.000  504887.000  513811.000  509917.000  400025.000
splash2x/ocean_cp       3403622.000 3406128.000 3444901.000 3402319.000 2645269.000
splash2x/ocean_ncp      3879080.000 3886923.000 7166490.000 3922086.000 2992831.000
splash2x/radiosity      1474071.000 1473050.000 1485076.000 1475643.000 227437.000
splash2x/radix          1695640.000 1705200.000 1419128.000 1554897.000 1749998.000
splash2x/raytrace       23268.000   23256.000   28084.000   26045.000   14677.000
splash2x/volrend        44000.000   44028.000   44150.000   44027.000   22121.000
splash2x/water_nsquared 29380.000   29380.000   29284.000   29436.000   24453.000
splash2x/water_spatial  655590.000  654947.000  655388.000  655542.000  432854.000


runtime_max             orig    rec     thp     ethp    prcl
parsec3/blackscholes    108.370 108.877 108.776 108.445 113.864
parsec3/bodytrack       80.276  80.366  79.630  80.126  81.659
parsec3/canneal         143.367 144.197 123.455 131.975 162.892
parsec3/dedup           12.054  11.965  11.772  12.142  14.055
parsec3/facesim         209.196 209.195 204.742 207.583 215.245
parsec3/ferret          190.661 192.285 189.877 192.743 192.066
parsec3/fluidanimate    212.472 213.463 211.522 214.281 220.408
parsec3/freqmine        295.246 293.927 292.255 292.148 300.932
parsec3/raytrace        120.358 120.792 121.136 120.404 131.839
parsec3/streamcluster   325.071 331.539 286.436 289.228 334.317
parsec3/swaptions       155.697 157.038 156.825 156.309 156.606
parsec3/vips            59.062  58.877  58.842  59.265  61.472
parsec3/x264            75.345  75.685  70.758  73.542  78.697
splash2x/barnes         80.831  82.605  74.027  79.812  106.207
splash2x/fft            33.718  33.881  23.008  33.554  47.834
splash2x/lu_cb          88.563  86.292  84.463  87.789  91.005
splash2x/lu_ncb         92.726  92.950  90.083  93.311  96.009
splash2x/ocean_cp       44.868  45.243  43.415  44.749  61.729
splash2x/ocean_ncp      84.442  83.289  51.998  82.229  124.633
splash2x/radiosity      92.378  92.244  91.275  92.292  105.059
splash2x/radix          31.681  31.874  25.371  32.192  45.455
splash2x/raytrace       85.864  85.906  84.819  84.530  85.539
splash2x/volrend        88.814  88.744  87.670  88.399  90.504
splash2x/water_nsquared 237.596 237.199 223.741 231.241 241.168
splash2x/water_spatial  90.466  90.773  90.521  90.550  99.268


memused.avg_max         orig        rec         thp         ethp        prcl
parsec3/blackscholes    1827340.000 1836897.000 1828187.000 1837378.000 1707479.000
parsec3/bodytrack       1421976.000 1437658.000 1424985.000 1437609.000 1436792.000
parsec3/canneal         1046601.000 1056260.000 1042789.000 1051592.000 1055545.000
parsec3/dedup           2456740.000 2467145.000 2435467.000 2467154.000 2466283.000
parsec3/facesim         542381.000  554228.000  550226.000  581529.000  492295.000
parsec3/ferret          321498.000  336341.000  325716.000  334070.000  331389.000
parsec3/fluidanimate    579520.000  588703.000  577242.000  588291.000  516079.000
parsec3/freqmine        996987.000  999207.000  992443.000  999281.000  773858.000
parsec3/raytrace        1754751.000 1760398.000 1743557.000 1756996.000 1586220.000
parsec3/streamcluster   133449.000  137064.000  124066.000  134933.000  134636.000
parsec3/swaptions       16397.000   29621.000   15944.000   28895.000   29207.000
parsec3/vips            2960001.000 2981427.000 2955163.000 2986429.000 2983387.000
parsec3/x264            3205259.000 3228002.000 3197838.000 3219088.000 3223033.000
splash2x/barnes         1214951.000 1228755.000 1327664.000 1228861.000 1114904.000
splash2x/fft            9480354.000 9264341.000 9282594.000 9213597.000 10164992.000
splash2x/lu_cb          517263.000  526113.000  523255.000  528095.000  402158.000
splash2x/lu_ncb         517764.000  524148.000  522610.000  528755.000  471551.000
splash2x/ocean_cp       3352774.000 3292570.000 3389379.000 3391900.000 3287861.000
splash2x/ocean_ncp      3908896.000 3875773.000 7077948.000 5935372.000 3867246.000
splash2x/radiosity      1475164.000 1472553.000 1484177.000 1473081.000 581921.000
splash2x/radix          1747493.000 1755210.000 1386306.000 1670956.000 2257898.000
splash2x/raytrace       47791.000   60947.000   53194.000   62454.000   55115.000
splash2x/volrend        153317.000  170996.000  154377.000  166662.000  164911.000
splash2x/water_nsquared 50000.000   66673.000   82988.000   62664.000   216319.000
splash2x/water_spatial  669928.000  681200.000  669739.000  681264.000  576776.000


rss.avg_max             orig        rec         thp         ethp        prcl
parsec3/blackscholes    593578.000  593873.000  596546.000  593668.000  404235.000
parsec3/bodytrack       32583.000   32617.000   32400.000   32664.000   28517.000
parsec3/canneal         842867.000  841952.000  839916.000  840304.000  835563.000
parsec3/dedup           1236591.000 1237214.000 1242257.000 1238979.000 1235900.000
parsec3/facesim         311936.000  312443.000  317898.000  313322.000  197379.000
parsec3/ferret          99760.000   99669.000   103618.000  99923.000   90262.000
parsec3/fluidanimate    531952.000  531940.000  532718.000  532706.000  453297.000
parsec3/freqmine        554327.000  553656.000  556388.000  555822.000  83652.000
parsec3/raytrace        896387.000  897364.000  891293.000  894360.000  362231.000
parsec3/streamcluster   110965.000  111003.000  112888.000  112335.000  110019.000
parsec3/swaptions       5675.000    5761.000    5721.000    5736.000    4110.000
parsec3/vips            32314.000   32166.000   32368.000   33057.000   29649.000
parsec3/x264            82128.000   82256.000   84304.000   82773.000   81839.000
splash2x/barnes         1219196.000 1219746.000 1225193.000 1219322.000 944438.000
splash2x/fft            9756750.000 9744458.000 9680119.000 9739032.000 9774117.000
splash2x/lu_cb          510829.000  511270.000  514484.000  510940.000  384608.000
splash2x/lu_ncb         510607.000  510501.000  513941.000  510667.000  455172.000
splash2x/ocean_cp       3414341.000 3412957.000 3449666.000 3434074.000 3401949.000
splash2x/ocean_ncp      3938869.000 3935467.000 7181326.000 6013719.000 3929055.000
splash2x/radiosity      1477633.000 1476719.000 1486072.000 1477598.000 343386.000
splash2x/radix          1796005.000 1783915.000 1440644.000 1776567.000 2039690.000
splash2x/raytrace       23348.000   23356.000   29332.000   27568.000   17323.000
splash2x/volrend        44314.000   44213.000   44736.000   44729.000   32296.000
splash2x/water_nsquared 29488.000   29444.000   30541.000   30583.000   26084.000
splash2x/water_spatial  657107.000  657889.000  656721.000  657519.000  510062.000
