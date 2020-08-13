.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by only 0.25% and slows
target workloads down by only 2.87%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 80.90% of
THP memory overheads while preserving 62.74% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 91.21% of residential sets and 25.50% of system memory footprint while
incurring only 1.70% runtime overhead in the best case (parsec3/freqmine).

Setup
=====

On QEMU/KVM based virtual machines utilizing 130GB of RAM and 36 vCPUs hosted
by AWS EC2 i3.metal instances that running a kernel that v20 DAMON patchset is
applied, I measure runtime and consumed system memory while running various
realistic workloads with several configurations.  I use 13 and 12 workloads in
PARSEC3 [3]_ and SPLASH-2X [4]_ benchmark suites, respectively.  I use another
wrapper scripts [5]_ for convenient setup and run of the workloads.

Measurement
-----------

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.  To make results more stable, I repeat the runs 5 times
and average results.

Configurations
--------------

The configurations I use are as below.

- orig: Linux v5.8 with 'madvise' THP policy
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

    # prcl: If a region >=4KB shows 0 access rate for >=10 seconds, page out.
    4K      max     0       0       10s     max     pageout

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
    parsec3/blackscholes    137.678  139.875  (1.60)     140.927  (2.36)     137.728  (0.04)     139.423  (1.27)     149.379  (8.50)
    parsec3/bodytrack       124.293  125.358  (0.86)     125.102  (0.65)     125.023  (0.59)     125.088  (0.64)     126.799  (2.02)
    parsec3/canneal         198.169  212.579  (7.27)     219.001  (10.51)    170.166  (-14.13)   188.856  (-4.70)    260.945  (31.68)
    parsec3/dedup           18.127   18.017   (-0.61)    18.238   (0.61)     17.983   (-0.79)    18.729   (3.32)     20.331   (12.16)
    parsec3/facesim         341.119  341.405  (0.08)     342.671  (0.46)     332.974  (-2.39)    340.470  (-0.19)    362.927  (6.39)
    parsec3/ferret          289.525  283.573  (-2.06)    290.808  (0.44)     289.632  (0.04)     281.739  (-2.69)    288.008  (-0.52)
    parsec3/fluidanimate    333.354  329.666  (-1.11)    337.867  (1.35)     319.984  (-4.01)    331.131  (-0.67)    338.331  (1.49)
    parsec3/freqmine        437.307  437.142  (-0.04)    436.350  (-0.22)    435.858  (-0.33)    435.948  (-0.31)    444.734  (1.70)
    parsec3/raytrace        182.294  185.817  (1.93)     183.530  (0.68)     183.391  (0.60)     184.201  (1.05)     212.175  (16.39)
    parsec3/streamcluster   590.075  715.451  (21.25)    616.715  (4.51)     551.440  (-6.55)    521.270  (-11.66)   658.285  (11.56)
    parsec3/swaptions       221.846  219.937  (-0.86)    220.916  (-0.42)    221.527  (-0.14)    222.046  (0.09)     224.605  (1.24)
    parsec3/vips            87.112   88.090   (1.12)     88.287   (1.35)     87.484   (0.43)     87.864   (0.86)     89.797   (3.08)
    parsec3/x264            111.975  116.828  (4.33)     113.487  (1.35)     114.927  (2.64)     115.926  (3.53)     117.039  (4.52)
    splash2x/barnes         131.127  130.606  (-0.40)    130.606  (-0.40)    117.111  (-10.69)   126.066  (-3.86)    167.868  (28.02)
    splash2x/fft            58.601   58.064   (-0.92)    58.912   (0.53)     45.775   (-21.89)   59.390   (1.35)     90.540   (54.50)
    splash2x/lu_cb          131.285  131.837  (0.42)     132.387  (0.84)     131.680  (0.30)     133.203  (1.46)     140.745  (7.21)
    splash2x/lu_ncb         147.441  149.167  (1.17)     149.812  (1.61)     150.871  (2.33)     152.875  (3.69)     154.081  (4.50)
    splash2x/ocean_cp       79.824   83.342   (4.41)     75.975   (-4.82)    81.519   (2.12)     81.029   (1.51)     107.836  (35.09)
    splash2x/ocean_ncp      156.907  150.808  (-3.89)    167.678  (6.86)     100.835  (-35.74)   140.421  (-10.51)   274.562  (74.98)
    splash2x/radiosity      148.163  143.284  (-3.29)    143.533  (-3.13)    143.931  (-2.86)    142.599  (-3.76)    151.820  (2.47)
    splash2x/radix          49.997   48.509   (-2.97)    50.697   (1.40)     44.444   (-11.11)   50.103   (0.21)     74.550   (49.11)
    splash2x/raytrace       133.136  134.488  (1.02)     134.159  (0.77)     131.259  (-1.41)    132.378  (-0.57)    140.280  (5.37)
    splash2x/volrend        120.425  120.608  (0.15)     121.220  (0.66)     120.146  (-0.23)    119.206  (-1.01)    121.144  (0.60)
    splash2x/water_nsquared 374.792  376.072  (0.34)     379.461  (1.25)     351.026  (-6.34)    351.286  (-6.27)    404.567  (7.94)
    splash2x/water_spatial  134.099  134.290  (0.14)     133.534  (-0.42)    134.729  (0.47)     133.523  (-0.43)    149.094  (11.18)
    total                   4738.670 4874.830 (2.87)     4811.890 (1.55)     4541.460 (-4.16)    4614.780 (-2.61)    5270.440 (11.22)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1831790.800  1835524.200  (0.20)     1828913.600  (-0.16)    1794934.400  (-2.01)    1800038.600  (-1.73)    1570047.600  (-14.29)
    parsec3/bodytrack       1428511.000  1440474.800  (0.84)     1439087.000  (0.74)     1395052.000  (-2.34)    1406088.400  (-1.57)    1404650.600  (-1.67)
    parsec3/canneal         1038558.800  1050856.200  (1.18)     1047210.600  (0.83)     1017683.200  (-2.01)    1034575.400  (-0.38)    1031644.600  (-0.67)
    parsec3/dedup           2520539.600  2526395.800  (0.23)     2492380.600  (-1.12)    2519617.600  (-0.04)    2513545.600  (-0.28)    2501534.200  (-0.75)
    parsec3/facesim         535444.400   553280.000   (3.33)     548723.200   (2.48)     525496.600   (-1.86)    532509.400   (-0.55)    469744.000   (-12.27)
    parsec3/ferret          318937.600   329848.000   (3.42)     327249.800   (2.61)     303570.400   (-4.82)    313124.800   (-1.82)    312872.800   (-1.90)
    parsec3/fluidanimate    567234.200   582203.800   (2.64)     578044.400   (1.91)     567180.800   (-0.01)    576577.800   (1.65)     475129.400   (-16.24)
    parsec3/freqmine        985478.800   996350.000   (1.10)     988950.400   (0.35)     969166.400   (-1.66)    974872.800   (-1.08)    734136.400   (-25.50)
    parsec3/raytrace        1739342.600  1753452.200  (0.81)     1736266.000  (-0.18)    1713985.000  (-1.46)    1729656.400  (-0.56)    1524839.800  (-12.33)
    parsec3/streamcluster   117172.400   157956.200   (34.81)    134518.200   (14.80)    116722.400   (-0.38)    125903.000   (7.45)     119823.600   (2.26)
    parsec3/swaptions       13285.000    30010.200    (125.89)   19468.600    (46.55)    7402.800     (-44.28)   15835.400    (19.20)    15230.400    (14.64)
    parsec3/vips            2983587.400  2999819.200  (0.54)     3003121.000  (0.65)     2948527.800  (-1.18)    2964735.600  (-0.63)    2952665.800  (-1.04)
    parsec3/x264            3236544.800  3253752.000  (0.53)     3248097.400  (0.36)     3203482.200  (-1.02)    3211024.800  (-0.79)    3215196.800  (-0.66)
    splash2x/barnes         1208462.000  1217858.000  (0.78)     1195067.400  (-1.11)    1210546.400  (0.17)     1210283.600  (0.15)     877632.600   (-27.38)
    splash2x/fft            9728490.200  9605781.200  (-1.26)    9326195.600  (-4.14)    9919958.200  (1.97)     9686276.200  (-0.43)    10211131.800 (4.96)
    splash2x/lu_cb          512591.600   523699.800   (2.17)     509565.000   (-0.59)    507349.400   (-1.02)    514470.400   (0.37)     333734.200   (-34.89)
    splash2x/lu_ncb         514435.800   529671.600   (2.96)     512644.000   (-0.35)    509428.800   (-0.97)    514796.600   (0.07)     417173.400   (-18.91)
    splash2x/ocean_cp       3330422.400  3328832.600  (-0.05)    3211530.200  (-3.57)    3356447.600  (0.78)     3307116.200  (-0.70)    3194443.200  (-4.08)
    splash2x/ocean_ncp      3920845.600  3930764.200  (0.25)     3863501.800  (-1.46)    7030597.200  (79.31)    4704457.600  (19.99)    3508018.600  (-10.53)
    splash2x/radiosity      1462733.200  1476790.600  (0.96)     1453313.200  (-0.64)    1471196.200  (0.58)     1468484.800  (0.39)     436011.000   (-70.19)
    splash2x/radix          2377940.000  2299220.000  (-3.31)    2285296.400  (-3.90)    2408838.200  (1.30)     2333525.000  (-1.87)    2467070.200  (3.75)
    splash2x/raytrace       41168.200    57008.800    (38.48)    48397.600    (17.56)    41546.400    (0.92)     51117.400    (24.17)    40869.000    (-0.73)
    splash2x/volrend        147909.800   162414.600   (9.81)     153684.400   (3.90)     138897.200   (-6.09)    149790.400   (1.27)     148473.200   (0.38)
    splash2x/water_nsquared 39947.600    53132.800    (33.01)    51969.200    (30.09)    39882.800    (-0.16)    50390.400    (26.14)    45484.400    (13.86)
    splash2x/water_spatial  669446.000   680384.800   (1.63)     667275.400   (-0.32)    666293.200   (-0.47)    674867.200   (0.81)     393837.800   (-41.17)
    total                   41270646.000 41375500.000 (0.25)     40670500.000 (-1.45)    44383600.000 (7.54)     41864100.000 (1.44)     38401200.000 (-6.95)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 2.87%
runtime overhead and 0.25% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead (1.55% for
runtime and -1.45% for memory footprint).

For a convenience test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 4.16% speedup but incurs 7.54% memory
overhead.  It achieves 35.74% speedup in the best case, but 79.31% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.61% speedup and 1.44% memory overhead.  In other words, 'ethp' removes
80.90% of THP memory waste while preserving 62.74% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 74.79% of THP memory waste
while preserving 29.40% of THP speedup.


Proactive Reclamation
---------------------

As similar to the original work, I use 4G 'zram' swap device for this
configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
11.22% runtime overhead in total while achieving 6.95% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    587132.800   587417.200   (0.05)     585858.400   (-0.22)    587258.200   (0.02)     585562.200   (-0.27)    240195.800   (-59.09)
    parsec3/bodytrack       32390.600    32443.400    (0.16)     32390.800    (0.00)     32277.600    (-0.35)    32485.000    (0.29)     18872.200    (-41.74)
    parsec3/canneal         843143.800   843473.200   (0.04)     843595.400   (0.05)     837078.200   (-0.72)    839919.400   (-0.38)    823627.000   (-2.31)
    parsec3/dedup           1175237.200  1183441.400  (0.70)     1179502.800  (0.36)     1198850.600  (2.01)     1181212.000  (0.51)     545686.000   (-53.57)
    parsec3/facesim         311916.800   311879.200   (-0.01)    311955.200   (0.01)     318343.000   (2.06)     315630.200   (1.19)     196713.200   (-36.93)
    parsec3/ferret          99564.200    99748.000    (0.18)     99747.600    (0.18)     101603.000   (2.05)     100486.000   (0.93)     82983.600    (-16.65)
    parsec3/fluidanimate    530770.000   531108.600   (0.06)     530561.000   (-0.04)    531953.800   (0.22)     532233.600   (0.28)     426740.200   (-19.60)
    parsec3/freqmine        552931.200   552461.400   (-0.08)    552514.600   (-0.08)    556013.400   (0.56)     554397.800   (0.27)     48582.800    (-91.21)
    parsec3/raytrace        879071.600   881414.400   (0.27)     878413.600   (-0.07)    872480.400   (-0.75)    881952.800   (0.33)     266763.800   (-69.65)
    parsec3/streamcluster   110838.600   110820.200   (-0.02)    110878.400   (0.04)     115551.000   (4.25)     115354.800   (4.07)     109679.600   (-1.05)
    parsec3/swaptions       5670.200     5690.200     (0.35)     5694.800     (0.43)     5724.400     (0.96)     5741.000     (1.25)     3711.400     (-34.55)
    parsec3/vips            32039.600    32147.400    (0.34)     32068.000    (0.09)     34177.800    (6.67)     34601.400    (8.00)     28813.200    (-10.07)
    parsec3/x264            81520.800    81647.800    (0.16)     81359.800    (-0.20)    83244.200    (2.11)     83276.600    (2.15)     80759.400    (-0.93)
    splash2x/barnes         1209988.800  1211618.000  (0.13)     1212421.600  (0.20)     1221536.800  (0.95)     1216069.200  (0.50)     486504.600   (-59.79)
    splash2x/fft            9861055.200  9985354.400  (1.26)     9822382.000  (-0.39)    10167093.200 (3.10)     9907205.600  (0.47)     6757148.000  (-31.48)
    splash2x/lu_cb          509711.000   508904.000   (-0.16)    509681.600   (-0.01)    509216.600   (-0.10)    509163.000   (-0.11)    323293.400   (-36.57)
    splash2x/lu_ncb         509145.200   508225.800   (-0.18)    508967.600   (-0.03)    509409.000   (0.05)     509452.800   (0.06)     410636.200   (-19.35)
    splash2x/ocean_cp       3367707.800  3391657.600  (0.71)     3384185.000  (0.49)     3411121.400  (1.29)     3390058.600  (0.66)     2468433.400  (-26.70)
    splash2x/ocean_ncp      3924482.800  3927235.600  (0.07)     3925233.800  (0.02)     7120158.200  (81.43)    4727458.000  (20.46)    2437422.000  (-37.89)
    splash2x/radiosity      1467469.800  1467435.400  (-0.00)    1466348.400  (-0.08)    1478055.200  (0.72)     1468756.800  (0.09)     135722.000   (-90.75)
    splash2x/radix          2401702.800  2349569.800  (-2.17)    2388126.800  (-0.57)    2443838.200  (1.75)     2391594.200  (-0.42)    1760714.000  (-26.69)
    splash2x/raytrace       23191.200    23217.800    (0.11)     23236.000    (0.19)     29102.400    (25.49)    27292.400    (17.68)    13533.400    (-41.64)
    splash2x/volrend        44066.200    44035.400    (-0.07)    44169.600    (0.23)     45019.600    (2.16)     44836.400    (1.75)     30627.600    (-30.50)
    splash2x/water_nsquared 29411.200    29406.400    (-0.02)    29407.200    (-0.01)    30526.400    (3.79)     30444.000    (3.51)     21834.800    (-25.76)
    splash2x/water_spatial  659771.400   660783.800   (0.15)     657683.400   (-0.32)    659992.000   (0.03)     660823.400   (0.16)     294847.000   (-55.31)
    total                   29250100.000 29361100.000 (0.38)     29216300.000 (-0.12)    32899592.000 (12.48)    30146000.000 (3.06)     18013747.000 (-38.41)

In total, 38.41% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 91.21% of residential sets and 25.50% of
system memory usage while incurring only 1.70% runtime overhead.
