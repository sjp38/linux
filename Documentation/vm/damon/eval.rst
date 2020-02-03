.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by 0.42% and slows
target workloads down by 0.39%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, namely 'ethp', removes
81.45% of THP memory overheads while preserving 50.09% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, namely 'prcl',
reduces 91.45% of residential sets and 22.91% of system memory footprint while
incurring only 2.43% runtime overhead in the best case (parsec3/freqmine).


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
Using it, you can do the evaluation and generate a report on your own.

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
    parsec3/blackscholes    138.247  139.131  (0.64)     138.872  (0.45)     138.436  (0.14)     138.599  (0.25)     151.104  (9.30)
    parsec3/bodytrack       124.338  124.450  (0.09)     124.624  (0.23)     124.357  (0.02)     124.705  (0.29)     125.329  (0.80)
    parsec3/canneal         211.054  216.642  (2.65)     213.773  (1.29)     176.039  (-16.59)   214.460  (1.61)     249.492  (18.21)
    parsec3/dedup           18.452   18.218   (-1.27)    18.334   (-0.64)    18.074   (-2.05)    18.315   (-0.74)    20.489   (11.04)
    parsec3/facesim         347.473  352.724  (1.51)     345.951  (-0.44)    340.480  (-2.01)    344.158  (-0.95)    371.561  (6.93)
    parsec3/fluidanimate    339.895  337.531  (-0.70)    335.378  (-1.33)    326.410  (-3.97)    333.322  (-1.93)    332.785  (-2.09)
    parsec3/freqmine        436.827  437.962  (0.26)     439.345  (0.58)     436.844  (0.00)     438.301  (0.34)     447.430  (2.43)
    parsec3/raytrace        185.539  183.376  (-1.17)    185.962  (0.23)     186.311  (0.42)     184.981  (-0.30)    207.715  (11.95)
    parsec3/streamcluster   682.926  686.849  (0.57)     677.420  (-0.81)    599.544  (-12.21)   615.506  (-9.87)    789.596  (15.62)
    parsec3/swaptions       219.616  221.386  (0.81)     221.320  (0.78)     220.269  (0.30)     221.426  (0.82)     -100.000 (0.00)
    parsec3/vips            88.397   88.504   (0.12)     87.550   (-0.96)    87.801   (-0.67)    87.638   (-0.86)    89.135   (0.84)
    parsec3/x264            113.634  114.143  (0.45)     116.506  (2.53)     112.728  (-0.80)    116.572  (2.59)     114.607  (0.86)
    splash2x/barnes         130.160  130.475  (0.24)     130.006  (-0.12)    119.679  (-8.05)    128.869  (-0.99)    173.767  (33.50)
    splash2x/fft            61.243   60.419   (-1.35)    60.144   (-1.79)    46.930   (-23.37)   58.679   (-4.19)    94.651   (54.55)
    splash2x/lu_cb          132.438  132.733  (0.22)     132.746  (0.23)     131.756  (-0.52)    132.492  (0.04)     146.579  (10.68)
    splash2x/lu_ncb         151.133  150.656  (-0.32)    151.187  (0.04)     150.106  (-0.68)    149.088  (-1.35)    156.120  (3.30)
    splash2x/ocean_cp       87.010   88.161   (1.32)     90.317   (3.80)     77.344   (-11.11)   77.739   (-10.65)   113.273  (30.18)
    splash2x/ocean_ncp      161.819  160.428  (-0.86)    161.508  (-0.19)    117.250  (-27.54)   141.303  (-12.68)   279.021  (72.43)
    splash2x/radiosity      144.159  142.662  (-1.04)    145.874  (1.19)     141.937  (-1.54)    142.184  (-1.37)    151.460  (5.06)
    splash2x/radix          51.341   51.156   (-0.36)    51.601   (0.51)     46.678   (-9.08)    49.119   (-4.33)    82.058   (59.83)
    splash2x/raytrace       133.543  134.201  (0.49)     134.022  (0.36)     132.010  (-1.15)    133.065  (-0.36)    141.626  (6.05)
    splash2x/volrend        120.229  120.489  (0.22)     121.690  (1.22)     119.702  (-0.44)    119.693  (-0.45)    122.247  (1.68)
    splash2x/water_nsquared 371.382  375.238  (1.04)     373.726  (0.63)     355.410  (-4.30)    358.243  (-3.54)    403.058  (8.53)
    splash2x/water_spatial  133.738  134.831  (0.82)     133.865  (0.10)     133.270  (-0.35)    133.320  (-0.31)    152.743  (14.21)
    total                   4584.600 4602.380 (0.39)     4591.740 (0.16)     4339.370 (-5.35)    4461.770 (-2.68)    4915.870 (7.23)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1822419.200  1832932.800  (0.58)     1825942.600  (0.19)     1817011.600  (-0.30)    1830445.600  (0.44)     1595311.600  (-12.46)
    parsec3/bodytrack       1424439.600  1437080.200  (0.89)     1438747.200  (1.00)     1423658.600  (-0.05)    1434771.600  (0.73)     1437144.200  (0.89)
    parsec3/canneal         1036933.000  1054711.800  (1.71)     1050022.200  (1.26)     1032368.400  (-0.44)    1052744.400  (1.52)     1049121.200  (1.18)
    parsec3/dedup           2500773.600  2502254.800  (0.06)     2467656.000  (-1.32)    2511153.400  (0.42)     2495594.600  (-0.21)    2488489.200  (-0.49)
    parsec3/facesim         535653.600   550504.000   (2.77)     547305.400   (2.18)     542355.200   (1.25)     552392.400   (3.12)     484499.000   (-9.55)
    parsec3/fluidanimate    572288.600   585018.400   (2.22)     582106.200   (1.72)     571557.400   (-0.13)    583349.400   (1.93)     493663.400   (-13.74)
    parsec3/freqmine        982803.000   997657.400   (1.51)     995492.200   (1.29)     986962.000   (0.42)     998352.400   (1.58)     757675.800   (-22.91)
    parsec3/raytrace        1742834.000  1754262.200  (0.66)     1747630.800  (0.28)     1731301.600  (-0.66)    1749506.400  (0.38)     1543049.400  (-11.46)
    parsec3/streamcluster   117851.800   158437.400   (34.44)    158582.400   (34.56)    122982.600   (4.35)     135280.200   (14.79)    136526.600   (15.85)
    parsec3/swaptions       14375.800    28709.600    (99.71)    28302.000    (96.87)    13821.400    (-3.86)    25697.800    (78.76)    -100.000     (0.00)
    parsec3/vips            2982188.400  2998594.600  (0.55)     3004458.800  (0.75)     2981225.200  (-0.03)    2997897.400  (0.53)     2979860.000  (-0.08)
    parsec3/x264            3241201.800  3250602.600  (0.29)     3257842.600  (0.51)     3238675.800  (-0.08)    3254314.200  (0.40)     3243305.667  (0.06)
    splash2x/barnes         1202953.000  1212273.400  (0.77)     1199432.200  (-0.29)    1214065.600  (0.92)     1218764.400  (1.31)     881206.000   (-26.75)
    splash2x/fft            9729496.200  9631956.200  (-1.00)    9282596.600  (-4.59)    9892176.200  (1.67)     9632687.800  (-0.99)    10320735.333 (6.08)
    splash2x/lu_cb          512464.200   523658.200   (2.18)     515659.200   (0.62)     513609.000   (0.22)     520062.000   (1.48)     338391.667   (-33.97)
    splash2x/lu_ncb         512790.400   528954.400   (3.15)     521128.600   (1.63)     513166.000   (0.07)     523937.800   (2.17)     426409.333   (-16.85)
    splash2x/ocean_cp       3342031.600  3326082.400  (-0.48)    3258501.400  (-2.50)    3367646.400  (0.77)     3314408.400  (-0.83)    3181677.000  (-4.80)
    splash2x/ocean_ncp      3904158.200  3922279.200  (0.46)     3870676.800  (-0.86)    7071312.600  (81.12)    4513390.200  (15.60)    3517213.000  (-9.91)
    splash2x/radiosity      1460571.200  1463947.200  (0.23)     1454906.200  (-0.39)    1470355.800  (0.67)     1465063.200  (0.31)     450619.333   (-69.15)
    splash2x/radix          2379050.200  2377324.000  (-0.07)    2270805.200  (-4.55)    2477275.200  (4.13)     2313398.800  (-2.76)    2433462.333  (2.29)
    splash2x/raytrace       42587.000    55138.400    (29.47)    55933.200    (31.34)    49202.200    (15.53)    59114.400    (38.81)    50805.000    (19.30)
    splash2x/volrend        149927.000   163164.400   (8.83)     161644.400   (7.82)     149249.000   (-0.45)    160589.600   (7.11)     159004.000   (6.05)
    splash2x/water_nsquared 39653.400    54180.600    (36.64)    53137.800    (34.01)    42475.000    (7.12)     52911.800    (33.44)    47500.333    (19.79)
    splash2x/water_spatial  669766.600   681525.600   (1.76)     674610.800   (0.72)     670925.800   (0.17)     679559.000   (1.46)     405725.667   (-39.42)
    total                   40919400.000 41091400.000 (0.42)     40423000.000 (-1.21)    44404600.000 (8.52)     41564259.000 (1.58)     38421300.000 (-6.10)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 0.39%
runtime overhead and 0.42% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead (0.16% for
runtime and -1.21% for memory footprint).

For a convenient test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 5.35% speedup but incurs 8.52% memory
overhead.  It achieves 27.54% speedup in the best case, but 81.72% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.68% speedup and 1.58% memory overhead.  In other words, 'ethp' removes
81.45% of THP memory waste while preserving 50.09% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 80.76% of THP memory waste
while preserving 46.04% of THP speedup.


Proactive Reclamation
---------------------

As similar to the original work, I use 4G 'zram' swap device for this
configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
7.23% runtime overhead in total while achieving 6.10% system memory footprint
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    585288.600   586175.800   (0.15)     586433.800   (0.20)     587028.600   (0.30)     587996.000   (0.46)     240808.600   (-58.86)
    parsec3/bodytrack       32139.000    32312.400    (0.54)     32201.800    (0.20)     32357.000    (0.68)     32263.000    (0.39)     18371.000    (-42.84)
    parsec3/canneal         843125.000   842998.800   (-0.01)    842991.000   (-0.02)    837536.400   (-0.66)    843580.600   (0.05)     825739.000   (-2.06)
    parsec3/dedup           1187272.400  1175883.400  (-0.96)    1183341.800  (-0.33)    1192656.600  (0.45)     1178204.600  (-0.76)    582322.000   (-50.95)
    parsec3/facesim         311757.600   311792.200   (0.01)     311751.400   (-0.00)    317679.400   (1.90)     315929.200   (1.34)     187274.800   (-39.93)
    parsec3/fluidanimate    531844.800   531840.800   (-0.00)    531816.800   (-0.01)    532855.200   (0.19)     532576.400   (0.14)     439993.400   (-17.27)
    parsec3/freqmine        552634.600   552707.800   (0.01)     552549.600   (-0.02)    555529.400   (0.52)     554548.200   (0.35)     47231.400    (-91.45)
    parsec3/raytrace        887301.000   883878.400   (-0.39)    884147.800   (-0.36)    874717.000   (-1.42)    881240.200   (-0.68)    264899.000   (-70.15)
    parsec3/streamcluster   110901.000   110899.200   (-0.00)    110906.200   (0.00)     115357.800   (4.02)     115521.800   (4.17)     109695.400   (-1.09)
    parsec3/swaptions       5697.800     5682.600     (-0.27)    5704.400     (0.12)     5684.000     (-0.24)    5668.600     (-0.51)    -100.000     (0.00)
    parsec3/vips            32083.400    31877.000    (-0.64)    31873.800    (-0.65)    33041.200    (2.99)     33781.600    (5.29)     28844.667    (-10.09)
    parsec3/x264            81776.600    81616.600    (-0.20)    81822.800    (0.06)     84827.400    (3.73)     83490.400    (2.10)     81161.333    (-0.75)
    splash2x/barnes         1219285.200  1218478.600  (-0.07)    1218261.800  (-0.08)    1227469.800  (0.67)     1222605.400  (0.27)     460175.000   (-62.26)
    splash2x/fft            10080559.600 10000486.200 (-0.79)    9996101.600  (-0.84)    10296965.200 (2.15)     9974327.200  (-1.05)    6932814.000  (-31.23)
    splash2x/lu_cb          511985.800   511815.600   (-0.03)    511759.600   (-0.04)    511275.800   (-0.14)    511932.400   (-0.01)    319837.000   (-37.53)
    splash2x/lu_ncb         511416.400   511389.800   (-0.01)    511257.800   (-0.03)    511574.800   (0.03)     511356.400   (-0.01)    412134.333   (-19.41)
    splash2x/ocean_cp       3424155.800  3421099.600  (-0.09)    3415628.600  (-0.25)    3443500.000  (0.56)     3415558.200  (-0.25)    2436061.333  (-28.86)
    splash2x/ocean_ncp      3939855.400  3934175.600  (-0.14)    3938673.800  (-0.03)    7177367.200  (82.17)    4581698.000  (16.29)    2391616.000  (-39.30)
    splash2x/radiosity      1471925.400  1418593.800  (-3.62)    1474347.000  (0.16)     1485447.800  (0.92)     1475442.800  (0.24)     144195.333   (-90.20)
    splash2x/radix          2465408.000  2484122.000  (0.76)     2449926.000  (-0.63)    2562083.200  (3.92)     2403580.400  (-2.51)    1539977.333  (-37.54)
    splash2x/raytrace       23279.200    23288.800    (0.04)     23319.200    (0.17)     29137.000    (25.16)    26747.000    (14.90)    13287.667    (-42.92)
    splash2x/volrend        44203.600    44115.000    (-0.20)    43493.000    (-1.61)    45079.000    (1.98)     45301.000    (2.48)     30139.333    (-31.82)
    splash2x/water_nsquared 29424.000    29413.600    (-0.04)    29425.600    (0.01)     30163.800    (2.51)     30527.400    (3.75)     22633.667    (-23.08)
    splash2x/water_spatial  663586.800   664276.200   (0.10)     664012.800   (0.06)     664078.800   (0.07)     663830.800   (0.04)     299712.667   (-54.83)
    total                   29547000.000 29408900.000 (-0.47)    29431800.000 (-0.39)    33153500.000 (12.21)    30027700.000 (1.63)     17828900.000 (-39.66)

In total, 39.66% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 91.45% of residential sets and 22.91% of
system memory usage while incurring only 2.43% runtime overhead.
