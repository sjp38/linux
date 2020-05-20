.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

Setup
=====

On a QEMU/KVM based virtual machine hosted by an Intel i7 host machine running
Ubuntu 18.04, I measure runtime and consumed system memory while running
various realistic workloads with several configurations.  I use 13 and 12
workloads in PARSEC3 [3]_ and SPLASH-2X [4]_ benchmark suites, respectively.  I
use another wrapper scripts [5]_ for convenient setup and run of the workloads.

Measurement
-----------

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.  To make results more stable, I repeat the runs 5 times
and average results.

Configurations
--------------

The configurations I use are as below.

- orig: Linux v5.5 with 'madvise' THP policy
- rec: 'orig' plus DAMON running with record feature
- thp: same with 'orig', but use 'always' THP policy
- ethp: 'orig' plus a DAMON operation scheme, 'efficient THP'
- prcl: 'orig' plus a DAMON operation scheme, 'proactive reclaim [6]_'

I use 'rec' for measurement of DAMON overheads to target workloads and system
memory.  The remaining configs including 'thp', 'ethp', and 'prcl' are for
measurement of DAMON monitoring accuracy.

'ethp' and 'prcl' are simple DAMON-based operation schemes developed for
proof of concepts of DAMON.  'ethp' reduces memory space waste of THP by using
DAMON for decision of promotions and demotion for huge pages, while 'prcl' is
as similar as the original work.  Those are implemented as below::

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
those should be more tuned.

.. [1] "Redis latency problems troubleshooting", https://redis.io/topics/latency
.. [2] "Disable Transparent Huge Pages (THP)",
    https://docs.mongodb.com/manual/tutorial/transparent-huge-pages/
.. [3] "The PARSEC Becnhmark Suite", https://parsec.cs.princeton.edu/index.htm
.. [4] "SPLASH-2x", https://parsec.cs.princeton.edu/parsec3-doc.htm#splash2x
.. [5] "parsec3_on_ubuntu", https://github.com/sjp38/parsec3_on_ubuntu
.. [6] "Proactively reclaiming idle memory", https://lwn.net/Articles/787611/

Results
=======

DAMON is lightweight.  It increases system memory usage by only 0.03% and
consumes less than 1% CPU time.  It slows target worloads down by only 0.7%.

DAMON is accurate and useful for memory management optimizations.  The
experimental DAMON-based operation scheme for THP, 'ethp', removes 63.12% of
THP memory overheads while preserving 49.15% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 85.85% of residentail sets and 21.98% of system memory footprint while
incurring only 2.42% runtime overhead in best case (parsec3/freqmine).

Below two tables show the measurement results.  The runtimes are in seconds
while the memory usages are in KiB.  Each configurations except 'orig' shows
its overhead relative to 'orig' in percent within parenthesises.::

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
---------------

In total, DAMON recording feature incurs 0.70% runtime overhead and 0.03%
memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  Nonetheless, the overheads
are not from DAMON, but from the wrapper, and thus should be ignored.  This
fake memory overhead continues in 'ethp' and 'prcl', as those configurations
are also using the Python wrapper.


Efficient THP
-------------

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
---------------------

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
8.41% runtime overhead in total while achieving 5.83% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

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
