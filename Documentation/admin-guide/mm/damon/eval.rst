.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by only -0.39% and
consumes less than 1% CPU time in most case.  It slows target workloads down by
only 0.63%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 69.43% of
THP memory overheads while preserving 37.11% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 89.30% of residential sets and 22.40% of system memory footprint while
incurring only 1.98% runtime overhead in the best case (parsec3/freqmine).

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

    runtime                 orig     rec      (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
    parsec3/blackscholes    106.888  108.158  (1.19)     107.082  (0.18)     107.429  (0.51)     115.307  (7.88)
    parsec3/bodytrack       79.235   79.631   (0.50)     79.379   (0.18)     79.368   (0.17)     80.493   (1.59)
    parsec3/canneal         139.014  140.668  (1.19)     122.524  (-11.86)   134.214  (-3.45)    143.914  (3.52)
    parsec3/dedup           11.883   11.933   (0.42)     11.737   (-1.23)    11.904   (0.18)     13.236   (11.39)
    parsec3/facesim         208.675  209.123  (0.21)     205.758  (-1.40)    207.150  (-0.73)    210.075  (0.67)
    parsec3/ferret          190.099  191.667  (0.82)     190.354  (0.13)     191.256  (0.61)     192.305  (1.16)
    parsec3/fluidanimate    211.297  212.724  (0.68)     209.383  (-0.91)    212.653  (0.64)     212.622  (0.63)
    parsec3/freqmine        290.441  292.775  (0.80)     289.952  (-0.17)    291.501  (0.36)     296.205  (1.98)
    parsec3/raytrace        119.029  119.661  (0.53)     119.551  (0.44)     120.368  (1.12)     133.943  (12.53)
    parsec3/streamcluster   323.952  328.647  (1.45)     284.653  (-12.13)   289.881  (-10.52)   328.274  (1.33)
    parsec3/swaptions       154.964  155.210  (0.16)     155.238  (0.18)     155.839  (0.56)     156.107  (0.74)
    parsec3/vips            59.052   58.983   (-0.12)    58.950   (-0.17)    58.967   (-0.14)    59.978   (1.57)
    parsec3/x264            72.536   69.273   (-4.50)    63.240   (-12.82)   71.688   (-1.17)    68.089   (-6.13)
    splash2x/barnes         80.328   81.241   (1.14)     74.921   (-6.73)    78.784   (-1.92)    110.985  (38.16)
    splash2x/fft            33.250   33.527   (0.83)     23.007   (-30.81)   32.634   (-1.85)    40.968   (23.21)
    splash2x/lu_cb          85.438   85.634   (0.23)     85.112   (-0.38)    85.972   (0.63)     90.131   (5.49)
    splash2x/lu_ncb         92.407   93.775   (1.48)     90.297   (-2.28)    93.443   (1.12)     94.041   (1.77)
    splash2x/ocean_cp       44.578   44.827   (0.56)     43.087   (-3.35)    43.977   (-1.35)    45.840   (2.83)
    splash2x/ocean_ncp      82.248   81.990   (-0.31)    51.128   (-37.84)   64.951   (-21.03)   112.929  (37.30)
    splash2x/radiosity      91.234   91.869   (0.70)     90.466   (-0.84)    91.623   (0.43)     104.004  (14.00)
    splash2x/radix          31.340   31.533   (0.61)     25.151   (-19.75)   29.634   (-5.44)    41.771   (33.28)
    splash2x/raytrace       84.232   84.939   (0.84)     82.813   (-1.69)    83.432   (-0.95)    85.422   (1.41)
    splash2x/volrend        87.056   88.313   (1.44)     86.126   (-1.07)    87.473   (0.48)     88.180   (1.29)
    splash2x/water_nsquared 232.762  234.777  (0.87)     220.799  (-5.14)    222.883  (-4.24)    235.231  (1.06)
    splash2x/water_spatial  89.835   89.925   (0.10)     89.730   (-0.12)    89.643   (-0.21)    97.167   (8.16)
    total                   3001.780 3020.810 (0.63)     2860.450 (-4.71)    2936.640 (-2.17)    3157.220 (5.18)
 
 
    memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1820134.000  1830056.000  (0.55)     1820762.000  (0.03)     1833280.400  (0.72)     1620112.200  (-10.99)
    parsec3/bodytrack       1419200.200  1430358.200  (0.79)     1415382.400  (-0.27)    1431830.400  (0.89)     1428315.400  (0.64)
    parsec3/canneal         1041700.200  1054783.600  (1.26)     1039941.200  (-0.17)    1049565.600  (0.76)     1051594.600  (0.95)
    parsec3/dedup           2403454.400  2413097.800  (0.40)     2411733.600  (0.34)     2404252.000  (0.03)     2412274.600  (0.37)
    parsec3/facesim         539662.800   552780.800   (2.43)     540933.200   (0.24)     550971.400   (2.10)     548370.000   (1.61)
    parsec3/ferret          320565.800   331712.000   (3.48)     319322.600   (-0.39)    332973.200   (3.87)     331142.000   (3.30)
    parsec3/fluidanimate    572426.400   584585.200   (2.12)     575249.600   (0.49)     584782.000   (2.16)     574169.400   (0.30)
    parsec3/freqmine        985943.600   998553.400   (1.28)     1028516.600  (4.32)     998298.000   (1.25)     765104.200   (-22.40)
    parsec3/raytrace        1747157.200  1757628.000  (0.60)     1743396.200  (-0.22)    1754790.600  (0.44)     1569932.000  (-10.14)
    parsec3/streamcluster   121307.000   134103.200   (10.55)    118746.200   (-2.11)    133971.600   (10.44)    132233.400   (9.01)
    parsec3/swaptions       14363.400    26875.400    (87.11)    14055.000    (-2.15)    26496.200    (84.47)    27033.800    (88.21)
    parsec3/vips            2945431.400  2963379.200  (0.61)     2956226.200  (0.37)     2943295.800  (-0.07)    2957360.400  (0.41)
    parsec3/x264            3191271.200  3196683.800  (0.17)     3161188.400  (-0.94)    3200352.000  (0.28)     3184628.600  (-0.21)
    splash2x/barnes         1213799.200  1216094.000  (0.19)     1217834.800  (0.33)     1212186.200  (-0.13)    928636.400   (-23.49)
    splash2x/fft            9462681.600  9183411.000  (-2.95)    9265091.400  (-2.09)    9166673.600  (-3.13)    9424556.600  (-0.40)
    splash2x/lu_cb          515952.400   521722.600   (1.12)     520563.600   (0.89)     520948.600   (0.97)     335968.400   (-34.88)
    splash2x/lu_ncb         515638.000   522689.600   (1.37)     521192.200   (1.08)     523513.600   (1.53)     523046.000   (1.44)
    splash2x/ocean_cp       3349483.000  3290003.000  (-1.78)    3380084.400  (0.91)     3340689.800  (-0.26)    3292473.200  (-1.70)
    splash2x/ocean_ncp      3895485.600  3871371.400  (-0.62)    7066323.600  (81.40)    4970278.200  (27.59)    3637734.400  (-6.62)
    splash2x/radiosity      1472652.200  1470431.600  (-0.15)    1482235.000  (0.65)     1471199.800  (-0.10)    513667.600   (-65.12)
    splash2x/radix          1692530.000  1686171.200  (-0.38)    1382322.000  (-18.33)   1583722.600  (-6.43)    1892434.400  (11.81)
    splash2x/raytrace       46488.400    58600.600    (26.05)    51538.200    (10.86)    60059.400    (29.19)    55203.000    (18.75)
    splash2x/volrend        150022.400   166788.400   (11.18)    151466.000   (0.96)     163678.600   (9.10)     161593.400   (7.71)
    splash2x/water_nsquared 49073.200    62559.000    (27.48)    60601.800    (23.49)    64762.800    (31.97)    53323.000    (8.66)
    splash2x/water_spatial  666074.200   672698.600   (0.99)     668054.000   (0.30)     673098.200   (1.05)     536452.200   (-19.46)
    total                   40152600.000 39997300.000 (-0.39)    42912754.000 (6.87)     40995700.000 (2.10)     37957300.000 (-5.47)


DAMON Overheads
---------------

In total, DAMON recording feature incurs 0.63% runtime overhead and -0.39%
memory space overhead.

For a convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes a high memory
overhead if the target workload has a small memory footprint.  Nonetheless, the
overheads are not from DAMON, but from the wrapper, and thus should be ignored.
This fake memory overhead continues in 'ethp' and 'prcl', as those
configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 4.71% speedup but incurs 6.87% memory
overhead.  It achieves 37.84% speedup in the best case, but 81.40% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.17% speedup and 2.10% memory overhead.  In other words, 'ethp' removes
69.43% of THP memory waste while preserving 46.07% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 66.10% of THP memory waste
while preserving 55.57% of THP speedup.


Proactive Reclamation
---------------------

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
5.18% runtime overhead in total while achieving 5.47% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    590255.400   590427.600   (0.03)     592919.200   (0.45)     589924.600   (-0.06)    272620.800   (-53.81)
    parsec3/bodytrack       32331.400    32280.200    (-0.16)    32304.600    (-0.08)    32295.600    (-0.11)    25386.400    (-21.48)
    parsec3/canneal         839327.800   840517.600   (0.14)     837729.400   (-0.19)    839485.000   (0.02)     838981.200   (-0.04)
    parsec3/dedup           1214897.200  1205565.200  (-0.77)    1231421.600  (1.36)     1206902.400  (-0.66)    909662.200   (-25.12)
    parsec3/facesim         311656.600   310912.800   (-0.24)    314467.200   (0.90)     312564.200   (0.29)     308407.800   (-1.04)
    parsec3/ferret          99738.600    99711.000    (-0.03)    101068.200   (1.33)     100018.200   (0.28)     90036.000    (-9.73)
    parsec3/fluidanimate    531879.200   531871.200   (-0.00)    531891.000   (0.00)     532066.000   (0.04)     521523.000   (-1.95)
    parsec3/freqmine        552169.800   553773.200   (0.29)     556495.800   (0.78)     553485.800   (0.24)     59071.000    (-89.30)
    parsec3/raytrace        894327.600   894141.000   (-0.02)    890956.000   (-0.38)    892035.400   (-0.26)    303397.200   (-66.08)
    parsec3/streamcluster   110943.200   110954.000   (0.01)     111407.400   (0.42)     111340.000   (0.36)     109841.000   (-0.99)
    parsec3/swaptions       5680.000     5563.800     (-2.05)    5660.200     (-0.35)    5658.200     (-0.38)    3846.000     (-32.29)
    parsec3/vips            31890.400    32098.200    (0.65)     32154.600    (0.83)     32321.600    (1.35)     29437.400    (-7.69)
    parsec3/x264            81907.200    81854.000    (-0.06)    83120.400    (1.48)     82805.600    (1.10)     81796.200    (-0.14)
    splash2x/barnes         1216312.800  1215561.000  (-0.06)    1224936.000  (0.71)     1219227.600  (0.24)     576351.600   (-52.61)
    splash2x/fft            9587848.800  9587352.000  (-0.01)    9646184.400  (0.61)     9625447.600  (0.39)     7134239.600  (-25.59)
    splash2x/lu_cb          510697.400   510591.800   (-0.02)    514411.400   (0.73)     510292.400   (-0.08)    314724.000   (-38.37)
    splash2x/lu_ncb         510252.600   510340.200   (0.02)     513900.400   (0.71)     510359.200   (0.02)     510247.400   (-0.00)
    splash2x/ocean_cp       3411545.800  3408327.400  (-0.09)    3441360.800  (0.87)     3394161.600  (-0.51)    3333649.000  (-2.28)
    splash2x/ocean_ncp      3931731.200  3921194.600  (-0.27)    7178746.200  (82.58)    5099068.400  (29.69)    3157059.400  (-19.70)
    splash2x/radiosity      1476138.800  1472629.200  (-0.24)    1485801.000  (0.65)     1477227.000  (0.07)     229545.600   (-84.45)
    splash2x/radix          1757003.200  1760448.800  (0.20)     1435953.000  (-18.27)   1661870.000  (-5.41)    1456666.000  (-17.09)
    splash2x/raytrace       23309.600    23310.400    (0.00)     28946.400    (24.18)    26803.600    (14.99)    15641.800    (-32.90)
    splash2x/volrend        44090.600    44095.800    (0.01)     44491.400    (0.91)     44292.200    (0.46)     32785.000    (-25.64)
    splash2x/water_nsquared 29437.600    29428.000    (-0.03)    29851.200    (1.41)     29746.600    (1.05)     26154.600    (-11.15)
    splash2x/water_spatial  656379.400   656150.200   (-0.03)    656559.000   (0.03)     656055.200   (-0.05)    484139.600   (-26.24)
    total                   28451700.000 28429000.000 (-0.08)    31522759.000 (10.79)    29545600.000 (3.84)     20825100.000 (-26.81)

In total, 26.81% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 89.30% of residential sets and 22.40% of
system memory usage while incurring only 1.98% runtime overhead.
