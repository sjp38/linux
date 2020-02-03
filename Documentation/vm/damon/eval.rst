.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by 0.39% and slows
target workloads down by 1.16%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, namely 'ethp', removes
76.15% of THP memory overheads while preserving 51.25% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, namely 'prcl',
reduces 93.38% of residential sets and 23.63% of system memory footprint while
incurring only 1.22% runtime overhead in the best case (parsec3/freqmine).


Setup
=====

On QEMU/KVM based virtual machines utilizing 130GB of RAM and 36 vCPUs hosted
by AWS EC2 i3.metal instances that running a kernel that v24 DAMON patchset is
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

- orig: Linux v5.10 with 'madvise' THP policy
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
is as similar as the original work.  For example, those can be implemented as
below::

    # format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>
    # ethp: Use huge pages if a region shows >=5% access rate, use regular
    # pages if a region >=2MB shows 0 access rate for >=7 seconds
    min     max     5       max     min     max     hugepage
    2M      max     min     min     7s      max     nohugepage

    # prcl: If a region >=4KB shows 0 access rate for >=5 seconds, page out.
    4K      max     0       0       5s     max     pageout

Note that these examples are designed with my only straightforward intuition
because those are for only proof of concepts and monitoring accuracy of DAMON.
In other words, those are not for production.  For production use, those should
be more tuned.  For automation of such tuning, you can use a user space tool
called DAMOOS [8]_.  For the evaluation, we use 'ethp' as same to above
example, but we use DAMOOS-tuned 'prcl' for each workload.

The evaluation is done using the tests package for DAMON, ``damon-tests`` [7]_.
Using it, you can do the evaluation and generate a report on your own.

.. [1] "Redis latency problems troubleshooting", https://redis.io/topics/latency
.. [2] "Disable Transparent Huge Pages (THP)",
    https://docs.mongodb.com/manual/tutorial/transparent-huge-pages/
.. [3] "The PARSEC Becnhmark Suite", https://parsec.cs.princeton.edu/index.htm
.. [4] "SPLASH-2x", https://parsec.cs.princeton.edu/parsec3-doc.htm#splash2x
.. [5] "parsec3_on_ubuntu", https://github.com/sjp38/parsec3_on_ubuntu
.. [6] "Proactively reclaiming idle memory", https://lwn.net/Articles/787611/
.. [7] "damon-tests", https://github.com/awslabs/damon-tests
.. [8] "DAMOOS", https://github.com/awslabs/damoos


Results
=======

Below two tables show the measurement results.  The runtimes are in seconds
while the memory usages are in KiB.  Each configuration except 'orig' shows
its overhead relative to 'orig' in percent within parenthesizes.::

    runtime                 orig     rec      (overhead) prec     (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
    parsec3/blackscholes    139.658  140.168  (0.37)     139.385  (-0.20)    138.367  (-0.92)    139.279  (-0.27)    147.024  (5.27)
    parsec3/bodytrack       123.788  124.622  (0.67)     123.636  (-0.12)    125.115  (1.07)     123.840  (0.04)     141.928  (14.65)
    parsec3/canneal         207.491  210.318  (1.36)     217.927  (5.03)     174.287  (-16.00)   202.609  (-2.35)    225.483  (8.67)
    parsec3/dedup           18.292   18.301   (0.05)     18.378   (0.47)     18.264   (-0.15)    18.298   (0.03)     20.541   (12.30)
    parsec3/facesim         343.893  340.286  (-1.05)    338.217  (-1.65)    332.953  (-3.18)    333.840  (-2.92)    365.650  (6.33)
    parsec3/fluidanimate    339.959  326.886  (-3.85)    330.286  (-2.85)    331.239  (-2.57)    326.011  (-4.10)    341.684  (0.51)
    parsec3/freqmine        445.987  436.332  (-2.16)    435.946  (-2.25)    435.704  (-2.31)    437.595  (-1.88)    451.414  (1.22)
    parsec3/raytrace        184.106  182.158  (-1.06)    182.056  (-1.11)    183.180  (-0.50)    183.545  (-0.30)    202.197  (9.83)
    parsec3/streamcluster   599.990  674.091  (12.35)    617.314  (2.89)     521.864  (-13.02)   551.971  (-8.00)    696.127  (16.02)
    parsec3/swaptions       220.462  222.637  (0.99)     220.449  (-0.01)    219.921  (-0.25)    221.607  (0.52)     223.956  (1.59)
    parsec3/vips            87.767   88.700   (1.06)     87.461   (-0.35)    87.466   (-0.34)    87.875   (0.12)     91.768   (4.56)
    parsec3/x264            110.843  117.856  (6.33)     113.023  (1.97)     108.665  (-1.97)    115.434  (4.14)     117.811  (6.29)
    splash2x/barnes         131.441  129.275  (-1.65)    128.341  (-2.36)    119.317  (-9.22)    126.199  (-3.99)    147.602  (12.30)
    splash2x/fft            59.705   58.382   (-2.22)    58.858   (-1.42)    45.949   (-23.04)   59.939   (0.39)     64.548   (8.11)
    splash2x/lu_cb          132.552  131.604  (-0.72)    131.846  (-0.53)    132.320  (-0.18)    132.100  (-0.34)    140.289  (5.84)
    splash2x/lu_ncb         150.215  149.670  (-0.36)    149.646  (-0.38)    148.823  (-0.93)    149.416  (-0.53)    152.338  (1.41)
    splash2x/ocean_cp       84.033   76.405   (-9.08)    75.104   (-10.63)   73.487   (-12.55)   77.789   (-7.43)    77.380   (-7.92)
    splash2x/ocean_ncp      153.833  154.247  (0.27)     156.227  (1.56)     106.619  (-30.69)   139.299  (-9.45)    165.030  (7.28)
    splash2x/radiosity      143.566  143.654  (0.06)     142.426  (-0.79)    141.193  (-1.65)    141.740  (-1.27)    157.817  (9.93)
    splash2x/radix          49.984   49.996   (0.02)     50.519   (1.07)     46.573   (-6.82)    50.724   (1.48)     50.695   (1.42)
    splash2x/raytrace       133.238  134.337  (0.83)     134.389  (0.86)     134.833  (1.20)     131.073  (-1.62)    145.541  (9.23)
    splash2x/volrend        121.700  120.652  (-0.86)    120.560  (-0.94)    120.629  (-0.88)    119.581  (-1.74)    129.422  (6.35)
    splash2x/water_nsquared 370.771  375.236  (1.20)     376.829  (1.63)     355.592  (-4.09)    354.087  (-4.50)    419.606  (13.17)
    splash2x/water_spatial  133.295  132.931  (-0.27)    132.762  (-0.40)    133.090  (-0.15)    133.809  (0.39)     153.647  (15.27)
    total                   4486.580 4538.750 (1.16)     4481.600 (-0.11)    4235.430 (-5.60)    4357.660 (-2.87)    4829.510 (7.64)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1828693.600  1834084.000  (0.29)     1823839.800  (-0.27)    1819296.600  (-0.51)    1830281.800  (0.09)     1603975.800  (-12.29)
    parsec3/bodytrack       1424963.400  1440085.800  (1.06)     1438384.200  (0.94)     1421718.400  (-0.23)    1432834.600  (0.55)     1439283.000  (1.00)
    parsec3/canneal         1036782.600  1052828.800  (1.55)     1050148.600  (1.29)     1035104.400  (-0.16)    1051145.400  (1.39)     1050019.400  (1.28)
    parsec3/dedup           2511841.400  2507374.000  (-0.18)    2472450.600  (-1.57)    2523557.600  (0.47)     2508912.000  (-0.12)    2493347.200  (-0.74)
    parsec3/facesim         537769.800   550740.800   (2.41)     548683.600   (2.03)     543547.800   (1.07)     560556.600   (4.24)     482782.600   (-10.23)
    parsec3/fluidanimate    570268.600   585598.000   (2.69)     579837.800   (1.68)     571433.000   (0.20)     582112.800   (2.08)     470073.400   (-17.57)
    parsec3/freqmine        982941.400   996253.200   (1.35)     993919.800   (1.12)     990531.800   (0.77)     1000994.400  (1.84)     750685.800   (-23.63)
    parsec3/raytrace        1737446.000  1749908.800  (0.72)     1741183.800  (0.22)     1726674.800  (-0.62)    1748530.200  (0.64)     1552275.600  (-10.66)
    parsec3/streamcluster   115857.000   155194.400   (33.95)    158272.800   (36.61)    122125.200   (5.41)     134545.600   (16.13)    133448.600   (15.18)
    parsec3/swaptions       13694.200    28451.800    (107.76)   28464.600    (107.86)   12797.800    (-6.55)    25328.200    (84.96)    28138.400    (105.48)
    parsec3/vips            2976126.400  3002408.600  (0.88)     3008218.800  (1.08)     2978258.600  (0.07)     2995428.600  (0.65)     2936338.600  (-1.34)
    parsec3/x264            3233886.200  3258790.200  (0.77)     3248355.000  (0.45)     3232070.000  (-0.06)    3256360.200  (0.69)     3254707.400  (0.64)
    splash2x/barnes         1210470.600  1211918.600  (0.12)     1204507.000  (-0.49)    1210892.800  (0.03)     1217414.800  (0.57)     944053.400   (-22.01)
    splash2x/fft            9697440.000  9604535.600  (-0.96)    9210571.800  (-5.02)    9867368.000  (1.75)     9637571.800  (-0.62)    9804092.000  (1.10)
    splash2x/lu_cb          510680.400   521792.600   (2.18)     517724.600   (1.38)     513500.800   (0.55)     519980.600   (1.82)     351787.000   (-31.11)
    splash2x/lu_ncb         512896.200   529353.600   (3.21)     521248.600   (1.63)     513493.200   (0.12)     523793.400   (2.12)     418701.600   (-18.37)
    splash2x/ocean_cp       3320800.200  3313688.400  (-0.21)    3225585.000  (-2.87)    3359032.200  (1.15)     3316591.800  (-0.13)    3304702.200  (-0.48)
    splash2x/ocean_ncp      3915132.400  3917401.000  (0.06)     3884086.400  (-0.79)    7050398.600  (80.08)    4532528.600  (15.77)    3920395.800  (0.13)
    splash2x/radiosity      1456908.200  1467611.800  (0.73)     1453612.600  (-0.23)    1466695.400  (0.67)     1467495.600  (0.73)     421197.600   (-71.09)
    splash2x/radix          2345874.600  2318202.200  (-1.18)    2261499.200  (-3.60)    2438228.400  (3.94)     2373697.800  (1.19)     2336605.600  (-0.40)
    splash2x/raytrace       43258.800    57624.200    (33.21)    55164.600    (27.52)    46204.400    (6.81)     60475.000    (39.80)    48865.400    (12.96)
    splash2x/volrend        149615.000   163809.400   (9.49)     162115.400   (8.36)     149119.600   (-0.33)    162747.800   (8.78)     157734.600   (5.43)
    splash2x/water_nsquared 40384.400    54848.600    (35.82)    53796.600    (33.21)    41455.800    (2.65)     53226.400    (31.80)    58260.600    (44.27)
    splash2x/water_spatial  670580.200   680444.200   (1.47)     670020.400   (-0.08)    668262.400   (-0.35)    678552.000   (1.19)     372931.000   (-44.39)
    total                   40844300.000 41002900.000 (0.39)     40311600.000 (-1.30)    44301900.000 (8.47)     41671052.000 (2.02)     38334431.000 (-6.14)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 1.16%
runtime overhead and 0.39% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead (-0.11% for
runtime and -1.30% for memory footprint).

For a convenient test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 5.60% speedup but incurs 8.47% memory
overhead.  It achieves 30.69% speedup in the best case, but 80.08% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.87% speedup and 2.02% memory overhead.  In other words, 'ethp' removes
76.15% of THP memory waste while preserving 51.25% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 80.30% of THP memory waste
while preserving 30.79% of THP speedup.


Proactive Reclamation
---------------------

As similar to the original work, I use 4G 'zram' swap device for this
configuration.  Also note that we use DAMOOS-tuned ethp schemes for each
workload.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
7.64% runtime overhead in total while achieving 6.14% system memory footprint
reduction.  Even in the worst case, the runtime overhead was only 16.02%.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    587536.800   585720.000   (-0.31)    586233.400   (-0.22)    587045.400   (-0.08)    586753.400   (-0.13)    252207.400   (-57.07)
    parsec3/bodytrack       32302.200    32290.600    (-0.04)    32261.800    (-0.13)    32215.800    (-0.27)    32173.000    (-0.40)    6798.800     (-78.95)
    parsec3/canneal         842370.600   841443.400   (-0.11)    844012.400   (0.19)     838074.400   (-0.51)    841700.800   (-0.08)    840804.000   (-0.19)
    parsec3/dedup           1180414.800  1164634.600  (-1.34)    1188886.200  (0.72)     1207821.000  (2.32)     1193896.200  (1.14)     572359.200   (-51.51)
    parsec3/facesim         311848.400   311709.800   (-0.04)    311790.800   (-0.02)    317345.800   (1.76)     315443.400   (1.15)     188488.000   (-39.56)
    parsec3/fluidanimate    531868.000   531885.600   (0.00)     531828.800   (-0.01)    532988.000   (0.21)     532959.600   (0.21)     415153.200   (-21.94)
    parsec3/freqmine        552491.000   552718.600   (0.04)     552807.200   (0.06)     556574.200   (0.74)     554374.600   (0.34)     36573.400    (-93.38)
    parsec3/raytrace        879683.400   880752.200   (0.12)     879907.000   (0.03)     870631.000   (-1.03)    880952.200   (0.14)     293119.200   (-66.68)
    parsec3/streamcluster   110991.800   110937.200   (-0.05)    110964.600   (-0.02)    115606.800   (4.16)     116199.000   (4.69)     110108.200   (-0.80)
    parsec3/swaptions       5665.000     5718.400     (0.94)     5720.600     (0.98)     5682.200     (0.30)     5628.600     (-0.64)    3613.800     (-36.21)
    parsec3/vips            32143.600    31823.200    (-1.00)    31912.200    (-0.72)    33164.200    (3.18)     33925.800    (5.54)     27813.600    (-13.47)
    parsec3/x264            81534.000    81811.000    (0.34)     81708.400    (0.21)     83052.400    (1.86)     83758.800    (2.73)     81691.800    (0.19)
    splash2x/barnes         1220515.200  1218291.200  (-0.18)    1217699.600  (-0.23)    1228551.600  (0.66)     1220669.800  (0.01)     681096.000   (-44.20)
    splash2x/fft            9915850.400  10036461.000 (1.22)     9881242.800  (-0.35)    10334603.600 (4.22)     10006993.200 (0.92)     8975181.200  (-9.49)
    splash2x/lu_cb          511327.200   511679.000   (0.07)     511761.600   (0.08)     511971.600   (0.13)     511711.200   (0.08)     338005.000   (-33.90)
    splash2x/lu_ncb         511505.000   506816.800   (-0.92)    511392.800   (-0.02)    496623.000   (-2.91)    511410.200   (-0.02)    404734.000   (-20.87)
    splash2x/ocean_cp       3398834.000  3405017.800  (0.18)     3415287.800  (0.48)     3443604.600  (1.32)     3416264.200  (0.51)     3387134.000  (-0.34)
    splash2x/ocean_ncp      3947092.800  3939805.400  (-0.18)    3952311.600  (0.13)     7165858.800  (81.55)    4610075.000  (16.80)    3944753.400  (-0.06)
    splash2x/radiosity      1475024.000  1474053.200  (-0.07)    1475032.400  (0.00)     1483718.800  (0.59)     1475919.600  (0.06)     99637.200    (-93.25)
    splash2x/radix          2431302.200  2416928.600  (-0.59)    2455596.800  (1.00)     2568526.400  (5.64)     2479966.800  (2.00)     2437406.600  (0.25)
    splash2x/raytrace       23274.400    23278.400    (0.02)     23287.200    (0.05)     28828.000    (23.86)    27800.200    (19.45)    5667.000     (-75.65)
    splash2x/volrend        44106.800    44151.400    (0.10)     44186.000    (0.18)     45200.400    (2.48)     44751.200    (1.46)     16912.000    (-61.66)
    splash2x/water_nsquared 29427.200    29425.600    (-0.01)    29402.400    (-0.08)    28055.400    (-4.66)    28572.400    (-2.90)    13207.800    (-55.12)
    splash2x/water_spatial  664312.200   664095.600   (-0.03)    663025.200   (-0.19)    664100.600   (-0.03)    663597.400   (-0.11)    261214.200   (-60.68)
    total                   29321300.000 29401500.000 (0.27)     29338300.000 (0.06)     33179900.000 (13.16)    30175600.000 (2.91)     23393600.000 (-20.22)

In total, 20.22% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 93.38% of residential sets and 23.63% of
system memory usage while incurring only 1.22% runtime overhead.
