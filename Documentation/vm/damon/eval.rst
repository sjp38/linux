.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by 0.25% and slows
target workloads down by 0.89%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, namely 'ethp', removes
81.73% of THP memory overheads while preserving 95.29% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, namely 'prcl',
reduces 91.30% of residential sets and 23.45% of system memory footprint while
incurring only 2.08% runtime overhead in the best case (parsec3/freqmine).


Setup
=====

On QEMU/KVM based virtual machines utilizing 130GB of RAM and 36 vCPUs hosted
by AWS EC2 i3.metal instances that running a kernel that v21 DAMON patchset is
applied, I measure runtime and consumed system memory while running various
realistic workloads with several configurations.  From each of PARSEC3 [3]_ and
SPLASH-2X [4]_ benchmark suites I pick 12 workloads, so I use 24 workloads in
total.  I use another wrapper scripts [5]_ for convenient setup and run of the
workloads.


Measurement
-----------

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.  To make results more stable, I repeat the runs 5 times
and average results.


Configurations
--------------

The configurations I use are as below.

- orig: Linux v5.9 with 'madvise' THP policy
- rec: 'orig' plus DAMON running with virtual memory access recording
- prec: 'orig' plus DAMON running with physical memory access recording
- thp: same with 'orig', but use 'always' THP policy
- ethp: 'orig' plus a DAMON operation scheme, 'efficient THP'
- prcl: 'orig' plus a DAMON operation scheme, 'proactive reclaim [6]_'

I use 'rec' for measurement of DAMON overheads to target workloads and system
memory.  'prec' is for physical memory monitroing and recording.  It monitors
17GB sized 'System RAM' region.  The remaining configs including 'thp', 'ethp',
and 'prcl' are for measurement of DAMON monitoring accuracy.

'ethp' and 'prcl' are simple DAMON-based operation schemes developed for
proof of concepts of DAMON.  'ethp' reduces memory space waste of THP by using
DAMON for the decision of promotions and demotion for huge pages, while 'prcl'
is as similar as the original work.  Those are implemented as below::

    # format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>
    # ethp: Use huge pages if a region shows >=5% access rate, use regular
    # pages if a region >=2MB shows 0 access rate for >=7 seconds
    min     max     5       max     min     max     hugepage
    2M      max     min     min     7s      max     nohugepage

    # prcl: If a region >=4KB shows 0 access rate for >=5 seconds, page out.
    4K      max     0       0       5s     max     pageout

Note that both 'ethp' and 'prcl' are designed with my only straightforward
intuition because those are for only proof of concepts and monitoring accuracy
of DAMON.  In other words, those are not for production.  For production use,
those should be more tuned.

The evaluation is done using the tests package for DAMON, ``damon-tests`` [7]_.
You can run this and generate a report on your own using it.

.. [1] "Redis latency problems troubleshooting", https://redis.io/topics/latency
.. [2] "Disable Transparent Huge Pages (THP)",
    https://docs.mongodb.com/manual/tutorial/transparent-huge-pages/
.. [3] "The PARSEC Becnhmark Suite", https://parsec.cs.princeton.edu/index.htm
.. [4] "SPLASH-2x", https://parsec.cs.princeton.edu/parsec3-doc.htm#splash2x
.. [5] "parsec3_on_ubuntu", https://github.com/sjp38/parsec3_on_ubuntu
.. [6] "Proactively reclaiming idle memory", https://lwn.net/Articles/787611/
.. [7] "damon-tests", https://github.com/awslabs/damon-tests


Results
=======

Below two tables show the measurement results.  The runtimes are in seconds
while the memory usages are in KiB.  Each configuration except 'orig' shows
its overhead relative to 'orig' in percent within parenthesizes.::

    runtime                 orig     rec      (overhead) prec     (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
    parsec3/blackscholes    138.208  139.078  (0.63)     138.962  (0.55)     139.357  (0.83)     139.132  (0.67)     152.354  (10.24)
    parsec3/bodytrack       123.803  124.525  (0.58)     123.751  (-0.04)    123.908  (0.08)     123.528  (-0.22)    126.714  (2.35)
    parsec3/canneal         210.538  205.626  (-2.33)    217.886  (3.49)     190.580  (-9.48)    206.514  (-1.91)    234.559  (11.41)
    parsec3/dedup           17.959   18.370   (2.29)     18.503   (3.03)     18.183   (1.25)     18.058   (0.55)     20.268   (12.86)
    parsec3/facesim         349.911  339.685  (-2.92)    350.295  (0.11)     332.965  (-4.84)    340.523  (-2.68)    361.546  (3.33)
    parsec3/fluidanimate    338.126  337.623  (-0.15)    336.554  (-0.47)    332.614  (-1.63)    326.699  (-3.38)    334.859  (-0.97)
    parsec3/freqmine        436.102  435.539  (-0.13)    439.250  (0.72)     436.600  (0.11)     437.302  (0.28)     445.161  (2.08)
    parsec3/raytrace        182.141  182.190  (0.03)     183.468  (0.73)     183.476  (0.73)     185.025  (1.58)     205.497  (12.82)
    parsec3/streamcluster   646.643  712.713  (10.22)    648.129  (0.23)     635.973  (-1.65)    543.135  (-16.01)   712.380  (10.17)
    parsec3/swaptions       219.022  219.598  (0.26)     219.895  (0.40)     221.296  (1.04)     221.085  (0.94)     221.645  (1.20)
    parsec3/vips            88.331   87.952   (-0.43)    87.964   (-0.42)    88.928   (0.68)     87.761   (-0.65)    89.482   (1.30)
    parsec3/x264            118.899  112.892  (-5.05)    120.804  (1.60)     108.313  (-8.90)    108.274  (-8.94)    111.550  (-6.18)
    splash2x/barnes         131.914  132.544  (0.48)     129.800  (-1.60)    119.006  (-9.78)    127.286  (-3.51)    174.193  (32.05)
    splash2x/fft            58.555   58.440   (-0.20)    58.585   (0.05)     46.276   (-20.97)   57.530   (-1.75)    90.741   (54.97)
    splash2x/lu_cb          133.300  134.141  (0.63)     132.406  (-0.67)    132.350  (-0.71)    132.668  (-0.47)    142.068  (6.58)
    splash2x/lu_ncb         149.119  152.106  (2.00)     150.765  (1.10)     151.501  (1.60)     148.956  (-0.11)    153.701  (3.07)
    splash2x/ocean_cp       75.054   78.269   (4.28)     76.888   (2.44)     73.014   (-2.72)    84.143   (12.11)    121.053  (61.29)
    splash2x/ocean_ncp      160.563  150.627  (-6.19)    152.911  (-4.77)    95.034   (-40.81)   141.612  (-11.80)   277.269  (72.69)
    splash2x/radiosity      143.127  142.501  (-0.44)    142.117  (-0.71)    142.312  (-0.57)    143.355  (0.16)     152.270  (6.39)
    splash2x/radix          52.191   49.788   (-4.60)    50.223   (-3.77)    44.351   (-15.02)   48.513   (-7.05)    81.601   (56.35)
    splash2x/raytrace       133.755  135.314  (1.17)     132.448  (-0.98)    132.043  (-1.28)    133.600  (-0.12)    138.558  (3.59)
    splash2x/volrend        120.503  119.950  (-0.46)    121.021  (0.43)     119.837  (-0.55)    119.831  (-0.56)    120.592  (0.07)
    splash2x/water_nsquared 376.371  375.451  (-0.24)    375.487  (-0.23)    354.005  (-5.94)    354.730  (-5.75)    397.614  (5.64)
    splash2x/water_spatial  133.994  133.460  (-0.40)    132.586  (-1.05)    132.831  (-0.87)    134.327  (0.25)     150.644  (12.43)
    total                   4538.100 4578.380 (0.89)     4540.720 (0.06)     4354.760 (-4.04)    4363.570 (-3.85)    5016.310 (10.54)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1826309.600  1833818.200  (0.41)     1828786.000  (0.14)     1820143.600  (-0.34)    1830923.600  (0.25)     1598872.000  (-12.45)
    parsec3/bodytrack       1424217.000  1436974.600  (0.90)     1436398.000  (0.86)     1421633.800  (-0.18)    1434718.200  (0.74)     1434411.200  (0.72)
    parsec3/canneal         1040253.400  1052139.800  (1.14)     1052512.400  (1.18)     1035381.800  (-0.47)    1049653.400  (0.90)     1049317.800  (0.87)
    parsec3/dedup           2501867.800  2526307.800  (0.98)     2466466.000  (-1.42)    2526893.000  (1.00)     2509818.000  (0.32)     2497495.600  (-0.17)
    parsec3/facesim         535597.000   549611.600   (2.62)     548756.200   (2.46)     537688.400   (0.39)     553604.200   (3.36)     484130.600   (-9.61)
    parsec3/fluidanimate    567666.200   579418.800   (2.07)     579690.400   (2.12)     567742.400   (0.01)     580155.600   (2.20)     491283.200   (-13.46)
    parsec3/freqmine        987479.800   997061.400   (0.97)     994319.400   (0.69)     988948.400   (0.15)     998694.000   (1.14)     755928.400   (-23.45)
    parsec3/raytrace        1738269.000  1753006.000  (0.85)     1744824.200  (0.38)     1730549.200  (-0.44)    1750131.800  (0.68)     1548381.400  (-10.92)
    parsec3/streamcluster   117605.200   158332.400   (34.63)    159858.800   (35.93)    120675.600   (2.61)     134289.800   (14.19)    129397.000   (10.03)
    parsec3/swaptions       13600.000    27782.000    (104.28)   31959.600    (135.00)   12666.000    (-6.87)    25009.600    (83.89)    25763.000    (89.43)
    parsec3/vips            2985688.800  2999933.000  (0.48)     3007744.400  (0.74)     2986884.000  (0.04)     3002386.000  (0.56)     2978898.800  (-0.23)
    parsec3/x264            3245603.400  3247109.400  (0.05)     3263116.600  (0.54)     3232282.600  (-0.41)    3247899.800  (0.07)     3246118.400  (0.02)
    splash2x/barnes         1201901.800  1214834.400  (1.08)     1202295.800  (0.03)     1209412.600  (0.62)     1214202.400  (1.02)     884999.000   (-26.37)
    splash2x/fft            9664686.600  9600248.400  (-0.67)    9349118.800  (-3.27)    9933514.600  (2.78)     9631206.600  (-0.35)    10280275.800 (6.37)
    splash2x/lu_cb          510420.400   523148.200   (2.49)     514914.600   (0.88)     513755.400   (0.65)     520163.400   (1.91)     336801.200   (-34.01)
    splash2x/lu_ncb         511532.200   529326.600   (3.48)     519711.000   (1.60)     537526.600   (5.08)     523745.800   (2.39)     429269.200   (-16.08)
    splash2x/ocean_cp       3319439.200  3302381.000  (-0.51)    3238411.400  (-2.44)    3361820.800  (1.28)     3327733.200  (0.25)     3153352.000  (-5.00)
    splash2x/ocean_ncp      3909858.200  3903840.600  (-0.15)    3860902.600  (-1.25)    7022147.400  (79.60)    4470036.000  (14.33)    3521609.000  (-9.93)
    splash2x/radiosity      1460921.000  1465081.000  (0.28)     1456779.800  (-0.28)    1470047.000  (0.62)     1467061.600  (0.42)     446035.400   (-69.47)
    splash2x/radix          2427095.200  2336602.600  (-3.73)    2250746.200  (-7.27)    2399454.800  (-1.14)    2292519.600  (-5.54)    2458012.200  (1.27)
    splash2x/raytrace       42109.600    56762.400    (34.80)    55746.200    (32.38)    49447.000    (17.42)    59412.200    (41.09)    49360.600    (17.22)
    splash2x/volrend        149513.000   162802.400   (8.89)     162495.800   (8.68)     148992.000   (-0.35)    161995.600   (8.35)     159614.800   (6.76)
    splash2x/water_nsquared 39106.600    54252.000    (38.73)    54117.000    (38.38)    39747.600    (1.64)     54016.000    (38.12)    50599.800    (29.39)
    splash2x/water_spatial  667480.200   678556.600   (1.66)     674177.400   (1.00)     669400.400   (0.29)     678370.800   (1.63)     413530.600   (-38.05)
    total                   40888200.000 40989200.000 (0.25)     40453800.000 (-1.06)    44336700.000 (8.43)     41517700.000 (1.54)     38423500.000 (-6.03)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 0.89%
runtime overhead and 0.25% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead (0.06% for
runtime and -1.06% for memory footprint).

For a convenient test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 4.04% speedup but incurs 8.43% memory
overhead.  It achieves 40.81% speedup in the best case, but 79.60% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 3.85% speedup and 1.54% memory overhead.  In other words, 'ethp' removes
81.73% of THP memory waste while preserving 95.29% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 81.99% of THP memory waste
while preserving 28.91% of THP speedup.


Proactive Reclamation
---------------------

As similar to the original work, I use 4G 'zram' swap device for this
configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
10.54% runtime overhead in total while achieving 6.03% system memory footprint
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    588097.000   586885.200   (-0.21)    586744.600   (-0.23)    587201.800   (-0.15)    587311.800   (-0.13)    240537.000   (-59.10)
    parsec3/bodytrack       32399.200    32313.800    (-0.26)    32348.600    (-0.16)    32461.400    (0.19)     32323.400    (-0.23)    18773.400    (-42.06)
    parsec3/canneal         844943.600   841299.200   (-0.43)    844106.000   (-0.10)    840850.800   (-0.48)    841133.400   (-0.45)    826411.600   (-2.19)
    parsec3/dedup           1176571.000  1169106.600  (-0.63)    1186366.000  (0.83)     1209152.400  (2.77)     1177493.600  (0.08)     566093.600   (-51.89)
    parsec3/facesim         311871.800   311856.400   (-0.00)    311872.800   (0.00)     316593.800   (1.51)     315922.000   (1.30)     190055.200   (-39.06)
    parsec3/fluidanimate    531868.800   531871.200   (0.00)     531865.600   (-0.00)    533324.600   (0.27)     532909.200   (0.20)     439318.000   (-17.40)
    parsec3/freqmine        552617.200   552677.000   (0.01)     552905.400   (0.05)     556087.800   (0.63)     554862.600   (0.41)     48064.800    (-91.30)
    parsec3/raytrace        879575.200   882800.000   (0.37)     885056.600   (0.62)     872658.000   (-0.79)    879860.400   (0.03)     265878.400   (-69.77)
    parsec3/streamcluster   110927.000   110883.800   (-0.04)    110891.000   (-0.03)    115691.000   (4.29)     115954.800   (4.53)     109740.000   (-1.07)
    parsec3/swaptions       5681.600     5655.400     (-0.46)    5691.000     (0.17)     5667.800     (-0.24)    5703.600     (0.39)     3727.600     (-34.39)
    parsec3/vips            32070.600    31970.000    (-0.31)    32084.800    (0.04)     34018.400    (6.07)     33693.600    (5.06)     28923.600    (-9.81)
    parsec3/x264            81945.400    81576.200    (-0.45)    81549.000    (-0.48)    83007.200    (1.30)     83291.400    (1.64)     80758.000    (-1.45)
    splash2x/barnes         1219427.800  1218697.800  (-0.06)    1218086.600  (-0.11)    1229194.000  (0.80)     1221392.200  (0.16)     474703.400   (-61.07)
    splash2x/fft            10017796.200 9985709.600  (-0.32)    9977135.000  (-0.41)    10340846.200 (3.22)     9674628.200  (-3.43)    6946312.200  (-30.66)
    splash2x/lu_cb          512014.400   511950.800   (-0.01)    511906.000   (-0.02)    512169.600   (0.03)     511962.800   (-0.01)    321004.400   (-37.31)
    splash2x/lu_ncb         511463.600   511441.000   (-0.00)    511419.400   (-0.01)    511313.800   (-0.03)    511552.600   (0.02)     413957.000   (-19.06)
    splash2x/ocean_cp       3404969.200  3385687.000  (-0.57)    3403813.800  (-0.03)    3435857.600  (0.91)     3422585.200  (0.52)     2231218.200  (-34.47)
    splash2x/ocean_ncp      3939590.400  3947029.400  (0.19)     3949499.400  (0.25)     7186627.400  (82.42)    4522456.000  (14.80)    2382259.600  (-39.53)
    splash2x/radiosity      1474598.000  1472188.800  (-0.16)    1475263.600  (0.05)     1485444.800  (0.74)     1475750.600  (0.08)     138284.600   (-90.62)
    splash2x/radix          2497487.400  2406411.000  (-3.65)    2437140.600  (-2.42)    2466633.200  (-1.24)    2388150.000  (-4.38)    1611689.400  (-35.47)
    splash2x/raytrace       23265.600    23294.400    (0.12)     23277.600    (0.05)     28612.600    (22.98)    27758.000    (19.31)    13457.400    (-42.16)
    splash2x/volrend        43833.400    44100.600    (0.61)     44112.600    (0.64)     44937.200    (2.52)     44933.000    (2.51)     29907.000    (-31.77)
    splash2x/water_nsquared 29396.800    29381.600    (-0.05)    29422.400    (0.09)     30712.800    (4.48)     29536.000    (0.47)     21251.000    (-27.71)
    splash2x/water_spatial  664097.000   664098.400   (0.00)     664158.000   (0.01)     664195.400   (0.01)     664306.000   (0.03)     306858.400   (-53.79)
    total                   29486597.000 29339000.000 (-0.50)    29406758.000 (-0.27)    33123200.000 (12.33)    29655583.000 (0.57)     17709300.000 (-39.94)

In total, 39.94% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 91.30% of residential sets and 23.45% of
system memory usage while incurring only 2.08% runtime overhead.
