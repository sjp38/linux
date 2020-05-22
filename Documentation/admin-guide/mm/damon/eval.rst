.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by only -0.26% and
consumes less than 1% CPU time.  It slows target workloads down by only 0.68%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 37.11% of
THP memory overheads while preserving 51.53% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 88.85% of residential sets and 22.38% of system memory footprint while
incurring only 2.63% runtime overhead in the best case (parsec3/freqmine).

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
'/proc/meminfo' file.  To make results more stable, I repeat the runs 4 times
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
DAMON for the decision of promotions and demotion for huge pages, while 'prcl'
is as similar as the original work.  Those are implemented as below::

    # format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>
    # ethp: Use huge pages if a region >2MB shows >5% access rate, use regular
    # pages if a region >2MB shows <5% access rate for >2 seconds
    2M      null    5       null    null    null    hugepage
    2M      null    null    5       2s      null    nohugepage

    # prcl: If a region >4KB shows <5% access rate for >7 seconds, page out.
    4K null    null 5    7s null      pageout

Note that both 'ethp' and 'prcl' are designed with my only straightforward
intuition because those are for only proof of concepts and monitoring accuracy
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

Below two tables show the measurement results.  The runtimes are in seconds
while the memory usages are in KiB.  Each configuration except 'orig' shows
its overhead relative to 'orig' in percent within parenthesizes.::

    runtime                 orig     rec      (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
    parsec3/blackscholes    106.700  106.816  (0.11)     106.235  (-0.44)    107.998  (1.22)     113.877  (6.73)
    parsec3/bodytrack       78.730   78.910   (0.23)     78.493   (-0.30)    79.212   (0.61)     79.970   (1.58)
    parsec3/canneal         137.220  138.381  (0.85)     125.296  (-8.69)    135.508  (-1.25)    141.898  (3.41)
    parsec3/dedup           11.906   11.812   (-0.79)    11.673   (-1.96)    11.922   (0.13)     13.175   (10.66)
    parsec3/facesim         207.709  210.253  (1.22)     204.383  (-1.60)    205.520  (-1.05)    208.090  (0.18)
    parsec3/ferret          190.382  191.019  (0.33)     189.960  (-0.22)    190.700  (0.17)     192.388  (1.05)
    parsec3/fluidanimate    211.938  212.163  (0.11)     209.091  (-1.34)    211.298  (-0.30)    213.115  (0.56)
    parsec3/freqmine        289.991  289.692  (-0.10)    289.709  (-0.10)    290.204  (0.07)     297.609  (2.63)
    parsec3/raytrace        118.435  119.075  (0.54)     119.789  (1.14)     118.901  (0.39)     134.019  (13.16)
    parsec3/streamcluster   323.203  327.456  (1.32)     282.739  (-12.52)   294.553  (-8.86)    328.417  (1.61)
    parsec3/swaptions       154.240  154.747  (0.33)     153.628  (-0.40)    154.691  (0.29)     154.862  (0.40)
    parsec3/vips            58.558   58.909   (0.60)     58.568   (0.02)     58.801   (0.41)     59.768   (2.07)
    parsec3/x264            66.541   68.812   (3.41)     71.863   (8.00)     67.001   (0.69)     67.031   (0.74)
    splash2x/barnes         80.363   80.808   (0.55)     74.531   (-7.26)    78.536   (-2.27)    105.120  (30.81)
    splash2x/fft            33.353   33.481   (0.38)     23.117   (-30.69)   33.274   (-0.24)    42.462   (27.31)
    splash2x/lu_cb          85.203   85.937   (0.86)     85.406   (0.24)     85.741   (0.63)     89.585   (5.14)
    splash2x/lu_ncb         92.554   93.949   (1.51)     90.660   (-2.05)    93.230   (0.73)     93.277   (0.78)
    splash2x/ocean_cp       44.806   44.678   (-0.29)    43.124   (-3.76)    43.930   (-1.96)    44.826   (0.04)
    splash2x/ocean_ncp      81.079   81.400   (0.40)     51.731   (-36.20)   54.989   (-32.18)   103.789  (28.01)
    splash2x/radiosity      91.176   91.976   (0.88)     91.174   (-0.00)    92.064   (0.97)     103.662  (13.69)
    splash2x/radix          31.024   31.341   (1.02)     25.332   (-18.35)   30.196   (-2.67)    41.430   (33.54)
    splash2x/raytrace       83.887   84.533   (0.77)     82.819   (-1.27)    84.072   (0.22)     85.294   (1.68)
    splash2x/volrend        86.848   87.480   (0.73)     86.918   (0.08)     86.957   (0.13)     87.637   (0.91)
    splash2x/water_nsquared 231.361  233.846  (1.07)     224.311  (-3.05)    227.771  (-1.55)    235.040  (1.59)
    splash2x/water_spatial  89.168   89.240   (0.08)     89.441   (0.31)     89.297   (0.14)     97.558   (9.41)
    total                   2986.370 3006.740 (0.68)     2869.990 (-3.90)    2926.360 (-2.01)    3133.910 (4.94)


    memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1820499.600  1831645.750  (0.61)     1824447.250  (0.22)     1832162.750  (0.64)     1613882.250  (-11.35)
    parsec3/bodytrack       1420949.000  1433789.250  (0.90)     1419899.750  (-0.07)    1448026.500  (1.91)     1433325.750  (0.87)
    parsec3/canneal         1040934.400  1053421.000  (1.20)     1041470.500  (0.05)     1076319.250  (3.40)     1052077.000  (1.07)
    parsec3/dedup           2451892.800  2430809.750  (-0.86)    2432056.000  (-0.81)    2461954.750  (0.41)     2464085.250  (0.50)
    parsec3/facesim         540391.400   554140.250   (2.54)     541746.500   (0.25)     554752.000   (2.66)     551292.750   (2.02)
    parsec3/ferret          317320.200   330704.250   (4.22)     321047.500   (1.17)     328083.750   (3.39)     334229.750   (5.33)
    parsec3/fluidanimate    574584.400   586118.500   (2.01)     575439.000   (0.15)     581992.750   (1.29)     572441.250   (-0.37)
    parsec3/freqmine        984871.000   995677.250   (1.10)     996862.250   (1.22)     997982.750   (1.33)     764451.750   (-22.38)
    parsec3/raytrace        1745588.750  1753719.500  (0.47)     1741797.250  (-0.22)    1754282.500  (0.50)     1568502.500  (-10.14)
    parsec3/streamcluster   119804.500   135758.000   (13.32)    120532.250   (0.61)     131375.250   (9.66)     133674.000   (11.58)
    parsec3/swaptions       13452.000    29179.250    (116.91)   13459.250    (0.05)     25678.750    (90.89)    25137.250    (86.87)
    parsec3/vips            2950073.250  2976003.500  (0.88)     2955024.750  (0.17)     2973070.250  (0.78)     2963762.500  (0.46)
    parsec3/x264            3180169.500  3199739.750  (0.62)     3194986.500  (0.47)     3186194.500  (0.19)     3200058.750  (0.63)
    splash2x/barnes         1212580.750  1212441.000  (-0.01)    1220578.250  (0.66)     1215824.000  (0.27)     949311.250   (-21.71)
    splash2x/fft            9407833.000  9191975.000  (-2.29)    9273307.250  (-1.43)    9201129.500  (-2.20)    9604074.000  (2.09)
    splash2x/lu_cb          515566.750   522522.500   (1.35)     520529.500   (0.96)     523235.750   (1.49)     334659.750   (-35.09)
    splash2x/lu_ncb         515890.250   523787.000   (1.53)     520830.000   (0.96)     523640.500   (1.50)     521718.750   (1.13)
    splash2x/ocean_cp       3350000.250  3304180.500  (-1.37)    3378817.500  (0.86)     3309873.750  (-1.20)    3298824.750  (-1.53)
    splash2x/ocean_ncp      3894993.500  3883243.500  (-0.30)    7063257.500  (81.34)    5759086.000  (47.86)    3660051.250  (-6.03)
    splash2x/radiosity      1470886.250  1468405.500  (-0.17)    1482163.750  (0.77)     1469432.250  (-0.10)    511760.250   (-65.21)
    splash2x/radix          1706026.500  1664726.500  (-2.42)    1388218.750  (-18.63)   1618079.500  (-5.16)    1804947.500  (5.80)
    splash2x/raytrace       44302.500    58867.000    (32.88)    49322.000    (11.33)    60357.000    (36.24)    53211.750    (20.11)
    splash2x/volrend        151185.000   165322.750   (9.35)     159558.750   (5.54)     164295.000   (8.67)     162293.500   (7.35)
    splash2x/water_nsquared 47521.250    61957.750    (30.38)    78032.500    (64.21)    59917.750    (26.09)    55789.750    (17.40)
    splash2x/water_spatial  669813.000   673132.250   (0.50)     668719.250   (-0.16)    674324.750   (0.67)     516339.250   (-22.91)
    total                   40147113.000 40041100.000 (-0.26)    42982100.000 (7.06)     41931200.000 (4.44)     38149900.000 (-4.97)


DAMON Overheads
---------------

In total, DAMON recording feature incurs 0.68% runtime overhead and -0.26%
memory space overhead.

For a convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes a high memory
overhead if the target workload has a small memory footprint.  Nonetheless, the
overheads are not from DAMON, but from the wrapper, and thus should be ignored.
This fake memory overhead continues in 'ethp' and 'prcl', as those
configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 3.90% speedup but incurs 7.06% memory
overhead.  It achieves 36.20% speedup in the best case, but 81.34% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.01% speedup and 4.44% memory overhead.  In other words, 'ethp' removes
37.11% of THP memory waste while preserving 51.53% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 41.16% of THP memory waste
while preserving 88.89% of THP speedup.


Proactive Reclamation
---------------------

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
4.94% runtime overhead in total while achieving 4.97% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    590676.600   591138.250   (0.08)     593013.500   (0.40)     592165.500   (0.25)     265108.750   (-55.12)
    parsec3/bodytrack       32210.600    32168.000    (-0.13)    32153.000    (-0.18)    32213.500    (0.01)     25193.000    (-21.79)
    parsec3/canneal         839745.000   839307.250   (-0.05)    836146.750   (-0.43)    839000.000   (-0.09)    837538.000   (-0.26)
    parsec3/dedup           1228266.200  1235806.250  (0.61)     1241202.750  (1.05)     1236047.500  (0.63)     964756.250   (-21.45)
    parsec3/facesim         311081.400   311230.750   (0.05)     313872.250   (0.90)     312603.250   (0.49)     311492.250   (0.13)
    parsec3/ferret          99643.200    99475.500    (-0.17)    100978.250   (1.34)     99919.500    (0.28)     91212.250    (-8.46)
    parsec3/fluidanimate    531785.400   531791.750   (0.00)     531789.750   (0.00)     531802.000   (0.00)     521553.750   (-1.92)
    parsec3/freqmine        553073.250   552066.250   (-0.18)    557176.750   (0.74)     553438.000   (0.07)     61652.750    (-88.85)
    parsec3/raytrace        891769.000   894520.000   (0.31)     888645.000   (-0.35)    895220.750   (0.39)     317519.250   (-64.39)
    parsec3/streamcluster   110863.750   110855.750   (-0.01)    111166.250   (0.27)     111440.500   (0.52)     109723.750   (-1.03)
    parsec3/swaptions       5601.250     5596.500     (-0.08)    5572.500     (-0.51)    5606.250     (0.09)     3832.500     (-31.58)
    parsec3/vips            31700.500    32073.250    (1.18)     32480.000    (2.46)     32255.250    (1.75)     28992.250    (-8.54)
    parsec3/x264            81818.750    81976.750    (0.19)     83556.750    (2.12)     82595.750    (0.95)     81501.000    (-0.39)
    splash2x/barnes         1216483.750  1214765.000  (-0.14)    1224880.500  (0.69)     1219237.000  (0.23)     640870.250   (-47.32)
    splash2x/fft            9642673.500  9624317.750  (-0.19)    9668374.500  (0.27)     9656379.000  (0.14)     7902654.750  (-18.05)
    splash2x/lu_cb          510392.250   510644.500   (0.05)     514394.500   (0.78)     510296.250   (-0.02)    316416.000   (-38.01)
    splash2x/lu_ncb         510356.000   510389.000   (0.01)     513938.000   (0.70)     510293.750   (-0.01)    509983.250   (-0.07)
    splash2x/ocean_cp       3389913.500  3387216.250  (-0.08)    3439715.500  (1.47)     3409124.250  (0.57)     3405335.000  (0.45)
    splash2x/ocean_ncp      3920953.250  3933975.500  (0.33)     7178652.500  (83.08)    5911233.000  (50.76)    3320183.500  (-15.32)
    splash2x/radiosity      1471137.250  1474920.500  (0.26)     1485684.000  (0.99)     1475552.750  (0.30)     227249.000   (-84.55)
    splash2x/radix          1719975.750  1736323.750  (0.95)     1399421.250  (-18.64)   1722445.250  (0.14)     1407670.750  (-18.16)
    splash2x/raytrace       23253.000    23299.000    (0.20)     28562.000    (22.83)    26393.750    (13.51)    15519.000    (-33.26)
    splash2x/volrend        44093.000    44059.500    (-0.08)    44182.250    (0.20)     44332.500    (0.54)     32756.750    (-25.71)
    splash2x/water_nsquared 29434.000    29386.000    (-0.16)    29324.500    (-0.37)    29592.500    (0.54)     26151.250    (-11.15)
    splash2x/water_spatial  656795.000   656273.000   (-0.08)    656589.250   (-0.03)    654763.000   (-0.31)    463615.750   (-29.41)
    total                   28443775.000 28463559.000 (0.07)     31511500.000 (10.79)    30493863.000 (7.21)     21888500.000 (-23.05)

In total, 23.05% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 88.85% of residential sets and 22.38% of
system memory usage while incurring only 2.63% runtime overhead.
