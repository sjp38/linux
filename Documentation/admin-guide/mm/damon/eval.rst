.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by only -0.45% and
consumes less than 1% CPU time in most case.  It slows target workloads down by
only 0.55%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 66.36% of
THP memory overheads while preserving 54% of THP speedup.  Another experimental
DAMON-based 'proactive reclamation' implementation, 'prcl', reduces 89.16% of
residential sets and 25.39% of system memory footprint while incurring only
2.3% runtime overhead in the best case (parsec3/freqmine).

Setup
=====

On a QEMU/KVM based virtual machine utilizing 16GB of RAM and hosted by an
Intel i7 machine running Ubuntu 18.04, I measure runtime and consumed system
memory while running various realistic workloads with several configurations.
I use 13 and 12 workloads in PARSEC3 [3]_ and SPLASH-2X [4]_ benchmark suites,
respectively.  I use another wrapper scripts [5]_ for convenient setup and run
of the workloads.

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
- rec: 'orig' plus DAMON running with virtual memory access recording
- prec: 'orig' plus DAMON running with physical memory access recording
- thp: same with 'orig', but use 'always' THP policy
- ethp: 'orig' plus a DAMON operation scheme, 'efficient THP'
- prcl: 'orig' plus a DAMON operation scheme, 'proactive reclaim [6]_'

I use 'rec' for measurement of DAMON overheads to target workloads and system
memory.  'prec' is for physical memory monitroing and recording.  It monitors
12GB sized 'System RAM' region.  The remaining configs including 'thp', 'ethp',
and 'prcl' are for measurement of DAMON monitoring accuracy.

'ethp' and 'prcl' are simple DAMON-based operation schemes developed for
proof of concepts of DAMON.  'ethp' reduces memory space waste of THP by using
DAMON for the decision of promotions and demotion for huge pages, while 'prcl'
is as similar as the original work.  Those are implemented as below::

    # format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>
    # ethp: Use huge pages if a region >2MB shows >5% access rate, use regular
    # pages if a region >2MB shows <5% access rate for >2 seconds
    2M      null    5       null    null    null    hugepage
    2M      null    null    5       13s      null    nohugepage

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

    runtime                 orig     rec      (overhead) prec     (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
    parsec3/blackscholes    106.818  107.938  (1.05)     107.104  (0.27)     106.876  (0.05)     106.965  (0.14)     114.997  (7.66)
    parsec3/bodytrack       78.880   79.904   (1.30)     79.066   (0.24)     78.938   (0.07)     79.048   (0.21)     80.779   (2.41)
    parsec3/canneal         142.251  145.288  (2.14)     146.524  (3.00)     124.451  (-12.51)   133.523  (-6.14)    152.869  (7.46)
    parsec3/dedup           11.945   12.012   (0.56)     11.984   (0.32)     11.705   (-2.01)    11.950   (0.04)     13.358   (11.82)
    parsec3/facesim         210.061  213.836  (1.80)     210.905  (0.40)     205.304  (-2.26)    207.346  (-1.29)    213.990  (1.87)
    parsec3/ferret          190.955  192.236  (0.67)     192.379  (0.75)     190.720  (-0.12)    191.177  (0.12)     192.925  (1.03)
    parsec3/fluidanimate    212.457  214.905  (1.15)     215.798  (1.57)     209.373  (-1.45)    213.872  (0.67)     215.930  (1.63)
    parsec3/freqmine        289.294  290.358  (0.37)     291.275  (0.68)     289.887  (0.20)     290.322  (0.36)     295.961  (2.30)
    parsec3/raytrace        118.208  119.210  (0.85)     119.725  (1.28)     119.375  (0.99)     118.661  (0.38)     134.613  (13.88)
    parsec3/streamcluster   330.524  334.900  (1.32)     335.208  (1.42)     283.719  (-14.16)   288.752  (-12.64)   341.505  (3.32)
    parsec3/swaptions       154.055  154.688  (0.41)     156.273  (1.44)     155.510  (0.94)     155.432  (0.89)     155.115  (0.69)
    parsec3/vips            58.825   58.981   (0.27)     59.123   (0.51)     59.309   (0.82)     59.029   (0.35)     60.082   (2.14)
    parsec3/x264            70.905   67.380   (-4.97)    68.890   (-2.84)    73.331   (3.42)     64.857   (-8.53)    65.789   (-7.22)
    splash2x/barnes         80.632   81.246   (0.76)     81.968   (1.66)     74.138   (-8.05)    80.754   (0.15)     110.976  (37.63)
    splash2x/fft            33.683   33.916   (0.69)     34.038   (1.05)     25.450   (-24.44)   32.809   (-2.60)    43.372   (28.76)
    splash2x/lu_cb          85.662   85.634   (-0.03)    86.353   (0.81)     85.208   (-0.53)    86.075   (0.48)     90.356   (5.48)
    splash2x/lu_ncb         93.915   94.018   (0.11)     94.608   (0.74)     90.005   (-4.16)    95.919   (2.13)     94.573   (0.70)
    splash2x/ocean_cp       46.004   46.049   (0.10)     46.983   (2.13)     43.075   (-6.37)    46.098   (0.20)     47.242   (2.69)
    splash2x/ocean_ncp      88.804   89.182   (0.43)     91.497   (3.03)     51.220   (-42.32)   69.361   (-21.89)   137.835  (55.21)
    splash2x/radiosity      92.037   91.662   (-0.41)    92.526   (0.53)     90.509   (-1.66)    92.623   (0.64)     104.414  (13.45)
    splash2x/radix          31.823   31.747   (-0.24)    31.965   (0.45)     25.010   (-21.41)   31.272   (-1.73)    46.786   (47.02)
    splash2x/raytrace       84.642   84.717   (0.09)     84.274   (-0.44)    82.415   (-2.63)    83.926   (-0.85)    85.343   (0.83)
    splash2x/volrend        87.495   87.546   (0.06)     87.184   (-0.36)    85.760   (-1.98)    87.779   (0.33)     87.924   (0.49)
    splash2x/water_nsquared 238.413  237.802  (-0.26)    239.113  (0.29)     218.300  (-8.44)    223.854  (-6.11)    240.465  (0.86)
    splash2x/water_spatial  89.365   89.210   (-0.17)    89.200   (-0.19)    89.371   (0.01)     90.466   (1.23)     96.516   (8.00)
    total                   3027.640 3044.380 (0.55)     3053.960 (0.87)     2868.960 (-5.24)    2941.860 (-2.83)    3223.720 (6.48)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1802238.200  1806011.800  (0.21)     1800943.200  (-0.07)    1777784.000  (-1.36)    1782858.000  (-1.08)    1555850.600  (-13.67)
    parsec3/bodytrack       1397968.400  1403989.000  (0.43)     1412880.800  (1.07)     1375692.600  (-1.59)    1383830.400  (-1.01)    1378257.200  (-1.41)
    parsec3/canneal         1013394.400  1029361.000  (1.58)     1005957.600  (-0.73)    982658.800   (-3.03)    986869.400   (-2.62)    992715.200   (-2.04)
    parsec3/dedup           2444439.800  2336649.000  (-4.41)    2373152.400  (-2.92)    2403313.600  (-1.68)    2429320.200  (-0.62)    2370761.600  (-3.01)
    parsec3/facesim         526276.800   536105.800   (1.87)     534458.600   (1.55)     519190.800   (-1.35)    524337.400   (-0.37)    520213.400   (-1.15)
    parsec3/ferret          305199.200   315198.200   (3.28)     315796.800   (3.47)     295800.600   (-3.08)    304196.600   (-0.33)    303591.400   (-0.53)
    parsec3/fluidanimate    567214.200   576300.200   (1.60)     574757.800   (1.33)     564007.600   (-0.57)    570454.400   (0.57)     610850.600   (7.69)
    parsec3/freqmine        963227.600   973483.200   (1.06)     967950.600   (0.49)     945590.000   (-1.83)    951364.600   (-1.23)    718629.200   (-25.39)
    parsec3/raytrace        1731695.400  1735024.200  (0.19)     1719210.000  (-0.72)    1702991.200  (-1.66)    1710843.000  (-1.20)    1529754.200  (-11.66)
    parsec3/streamcluster   113578.800   126687.200   (11.54)    128355.200   (13.01)    112044.600   (-1.35)    120691.400   (6.26)     117356.400   (3.33)
    parsec3/swaptions       8678.800     19770.200    (127.80)   17826.200    (105.40)   5580.400     (-35.70)   14635.000    (68.63)    12494.200    (43.96)
    parsec3/vips            2921135.800  2936119.800  (0.51)     2929623.400  (0.29)     2856140.400  (-2.22)    2904266.000  (-0.58)    2902977.200  (-0.62)
    parsec3/x264            3149015.800  3160569.800  (0.37)     3142172.800  (-0.22)    3098789.400  (-1.59)    3111801.200  (-1.18)    3111774.400  (-1.18)
    splash2x/barnes         1205029.000  1204898.400  (-0.01)    1190710.000  (-1.19)    1209728.200  (0.39)     1200073.800  (-0.41)    911751.200   (-24.34)
    splash2x/fft            9382596.800  9229500.000  (-1.63)    8462232.000  (-9.81)    9430863.800  (0.51)     9215691.600  (-1.78)    9598278.400  (2.30)
    splash2x/lu_cb          509417.400   514866.800   (1.07)     504017.600   (-1.06)    511991.800   (0.51)     510458.000   (0.20)     322114.600   (-36.77)
    splash2x/lu_ncb         508359.400   515627.200   (1.43)     500918.600   (-1.46)    513402.400   (0.99)     511902.000   (0.70)     512092.200   (0.73)
    splash2x/ocean_cp       3349734.200  3299650.800  (-1.50)    3149056.600  (-5.99)    3379062.800  (0.88)     3294031.800  (-1.66)    3289963.400  (-1.78)
    splash2x/ocean_ncp      3908069.000  3878726.400  (-0.75)    3802927.600  (-2.69)    7061570.000  (80.69)    5198223.200  (33.01)    3613756.800  (-7.53)
    splash2x/radiosity      1462249.600  1463372.000  (0.08)     1431969.400  (-2.07)    1474922.600  (0.87)     1458668.400  (-0.24)    493359.600   (-66.26)
    splash2x/radix          1711385.600  1702230.200  (-0.53)    1627271.600  (-4.91)    1386774.600  (-18.97)   1661159.000  (-2.93)    1988290.600  (16.18)
    splash2x/raytrace       39448.400    49703.800    (26.00)    47856.400    (21.31)    42644.600    (8.10)     48197.400    (22.18)    40136.600    (1.74)
    splash2x/volrend        142892.400   155599.400   (8.89)     151978.000   (6.36)     139284.600   (-2.52)    147971.200   (3.55)     148731.000   (4.09)
    splash2x/water_nsquared 40879.600    50183.000    (22.76)    48930.600    (19.69)    38879.800    (-4.89)    46986.800    (14.94)    43383.600    (6.13)
    splash2x/water_spatial  662408.800   666122.200   (0.56)     650072.600   (-1.86)    658733.600   (-0.55)    660327.400   (-0.31)    537464.800   (-18.86)
    total                   39866500.000 39685800.000 (-0.45)    38491200.000 (-3.45)    42487400.000 (6.57)     40749300.000 (2.21)     37624800.000 (-5.62)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 0.55%
runtime overhead and -0.45% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead ( 0.87% for
runtime and -3.45% for memory footprint).

For a convenience test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 5.24% speedup but incurs 6.57% memory
overhead.  It achieves 42.32% speedup in the best case, but 80.69% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.83% speedup and 2.21% memory overhead.  In other words, 'ethp' removes
66.36% of THP memory waste while preserving 54% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 59.09% of THP memory waste
while preserving 51.72% of THP speedup.


Proactive Reclamation
---------------------

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
6.48% runtime overhead in total while achieving 5.62% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    592593.000   593708.000   (0.19)     593353.000   (0.13)     593932.000   (0.23)     593717.000   (0.19)     256305.000   (-56.75)
    parsec3/bodytrack       32244.200    32322.400    (0.24)     32225.200    (-0.06)    32360.800    (0.36)     32273.800    (0.09)     25276.600    (-21.61)
    parsec3/canneal         839853.200   838793.400   (-0.13)    840090.200   (0.03)     835110.600   (-0.56)    836953.600   (-0.35)    834654.400   (-0.62)
    parsec3/dedup           1210068.400  1182739.800  (-2.26)    1222808.200  (1.05)     1239838.800  (2.46)     1190533.000  (-1.61)    926740.600   (-23.41)
    parsec3/facesim         311302.000   311714.000   (0.13)     311372.800   (0.02)     315277.200   (1.28)     312964.800   (0.53)     303001.600   (-2.67)
    parsec3/ferret          99505.200    99704.000    (0.20)     99737.800    (0.23)     101184.200   (1.69)     100117.200   (0.62)     91092.400    (-8.45)
    parsec3/fluidanimate    531842.400   531864.800   (0.00)     531828.800   (-0.00)    531902.400   (0.01)     531760.200   (-0.02)    521562.400   (-1.93)
    parsec3/freqmine        552939.000   552337.400   (-0.11)    552254.800   (-0.12)    555928.400   (0.54)     553336.800   (0.07)     59944.800    (-89.16)
    parsec3/raytrace        894300.600   894191.600   (-0.01)    892107.600   (-0.25)    894179.800   (-0.01)    895297.400   (0.11)     311002.000   (-65.22)
    parsec3/streamcluster   110912.200   110849.200   (-0.06)    110883.200   (-0.03)    111547.800   (0.57)     111880.600   (0.87)     109868.400   (-0.94)
    parsec3/swaptions       5704.200     5659.000     (-0.79)    5633.000     (-1.25)    5728.200     (0.42)     5712.600     (0.15)     3898.600     (-31.65)
    parsec3/vips            31818.000    31831.800    (0.04)     31685.400    (-0.42)    31901.600    (0.26)     31794.200    (-0.07)    29020.000    (-8.79)
    parsec3/x264            81958.200    81745.000    (-0.26)    81600.600    (-0.44)    82696.000    (0.90)     82123.400    (0.20)     81115.200    (-1.03)
    splash2x/barnes         1216144.200  1216376.600  (0.02)     1216845.400  (0.06)     1227079.400  (0.90)     1218267.200  (0.17)     590483.800   (-51.45)
    splash2x/fft            9728971.800  9770172.000  (0.42)     9682539.600  (-0.48)    9959336.800  (2.37)     9546560.200  (-1.87)    7291492.600  (-25.05)
    splash2x/lu_cb          510926.000   509983.000   (-0.18)    510723.600   (-0.04)    514462.000   (0.69)     511226.600   (0.06)     317789.600   (-37.80)
    splash2x/lu_ncb         510786.600   510748.800   (-0.01)    509392.200   (-0.27)    513945.600   (0.62)     510811.600   (0.00)     509706.600   (-0.21)
    splash2x/ocean_cp       3398462.800  3406153.600  (0.23)     3409911.800  (0.34)     3444844.400  (1.36)     3384479.200  (-0.41)    3381901.400  (-0.49)
    splash2x/ocean_ncp      3925272.200  3928702.800  (0.09)     3927020.800  (0.04)     7182825.000  (82.99)    5315597.000  (35.42)    3160177.200  (-19.49)
    splash2x/radiosity      1474113.200  1476844.800  (0.19)     1470783.000  (-0.23)    1485701.600  (0.79)     1476519.600  (0.16)     215142.000   (-85.41)
    splash2x/radix          1778255.400  1757130.400  (-1.19)    1779710.800  (0.08)     1447898.600  (-18.58)   1758506.000  (-1.11)    1510297.200  (-15.07)
    splash2x/raytrace       23268.000    23251.200    (-0.07)    23266.400    (-0.01)    28786.400    (23.72)    27039.200    (16.21)    15778.800    (-32.19)
    splash2x/volrend        44100.000    44116.200    (0.04)     44076.800    (-0.05)    44611.800    (1.16)     44186.200    (0.20)     32662.200    (-25.94)
    splash2x/water_nsquared 29409.600    29403.200    (-0.02)    29403.200    (-0.02)    30182.000    (2.63)     29612.200    (0.69)     26038.000    (-11.46)
    splash2x/water_spatial  657423.600   657594.400   (0.03)     657354.000   (-0.01)    657674.000   (0.04)     657407.000   (-0.00)    503569.800   (-23.40)
    total                   28592300.000 28597900.000 (0.02)     28566654.000 (-0.09)    31868856.000 (11.46)    29758607.000 (4.08)     21108600.000 (-26.17)

In total, 26.17% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 89.16% of residential sets and 25.39% of
system memory usage while incurring only 2.3% runtime overhead.
