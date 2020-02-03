.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by 0.12% and slows
target workloads down by 1.39%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 88.16% of
THP memory overheads while preserving 88.73% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 91.34% of residential sets and 25.59% of system memory footprint while
incurring only 1.58% runtime overhead in the best case (parsec3/freqmine).


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
    parsec3/blackscholes    137.688  139.910  (1.61)     138.226  (0.39)     138.524  (0.61)     138.548  (0.62)     150.562  (9.35)
    parsec3/bodytrack       124.496  123.294  (-0.97)    124.482  (-0.01)    124.874  (0.30)     123.514  (-0.79)    126.380  (1.51)
    parsec3/canneal         196.513  209.465  (6.59)     223.213  (13.59)    189.302  (-3.67)    199.453  (1.50)     242.217  (23.26)
    parsec3/dedup           18.060   18.128   (0.38)     18.378   (1.76)     18.210   (0.83)     18.397   (1.87)     20.545   (13.76)
    parsec3/facesim         343.697  344.917  (0.36)     341.367  (-0.68)    337.696  (-1.75)    344.805  (0.32)     361.169  (5.08)
    parsec3/ferret          288.868  286.110  (-0.95)    292.308  (1.19)     287.814  (-0.36)    284.243  (-1.60)    284.200  (-1.62)
    parsec3/fluidanimate    342.267  337.743  (-1.32)    330.680  (-3.39)    337.356  (-1.43)    340.604  (-0.49)    343.565  (0.38)
    parsec3/freqmine        437.385  436.854  (-0.12)    437.641  (0.06)     435.008  (-0.54)    436.998  (-0.09)    444.276  (1.58)
    parsec3/raytrace        183.036  182.039  (-0.54)    184.859  (1.00)     187.330  (2.35)     185.660  (1.43)     209.707  (14.57)
    parsec3/streamcluster   611.075  675.108  (10.48)    656.373  (7.41)     541.711  (-11.35)   473.679  (-22.48)   815.450  (33.45)
    parsec3/swaptions       220.338  220.948  (0.28)     220.891  (0.25)     220.387  (0.02)     219.986  (-0.16)    -100.000 (0.00)
    parsec3/vips            87.710   88.581   (0.99)     88.423   (0.81)     88.460   (0.86)     88.471   (0.87)     89.661   (2.22)
    parsec3/x264            114.927  117.774  (2.48)     116.630  (1.48)     112.237  (-2.34)    110.709  (-3.67)    124.560  (8.38)
    splash2x/barnes         131.034  130.895  (-0.11)    129.088  (-1.48)    118.213  (-9.78)    124.497  (-4.99)    167.966  (28.19)
    splash2x/fft            59.805   60.237   (0.72)     59.895   (0.15)     47.008   (-21.40)   57.962   (-3.08)    87.183   (45.78)
    splash2x/lu_cb          132.353  132.157  (-0.15)    132.473  (0.09)     131.561  (-0.60)    135.541  (2.41)     141.720  (7.08)
    splash2x/lu_ncb         149.050  150.496  (0.97)     151.912  (1.92)     150.974  (1.29)     148.329  (-0.48)    152.227  (2.13)
    splash2x/ocean_cp       82.189   77.735   (-5.42)    84.466   (2.77)     77.498   (-5.71)    82.586   (0.48)     113.737  (38.38)
    splash2x/ocean_ncp      154.934  154.656  (-0.18)    164.204  (5.98)     101.861  (-34.26)   142.600  (-7.96)    281.650  (81.79)
    splash2x/radiosity      142.710  141.643  (-0.75)    143.940  (0.86)     141.982  (-0.51)    142.017  (-0.49)    152.116  (6.59)
    splash2x/radix          50.357   50.331   (-0.05)    50.717   (0.72)     45.664   (-9.32)    50.222   (-0.27)    73.981   (46.91)
    splash2x/raytrace       134.039  132.650  (-1.04)    134.583  (0.41)     131.570  (-1.84)    133.050  (-0.74)    141.463  (5.54)
    splash2x/volrend        120.769  120.220  (-0.45)    119.895  (-0.72)    120.159  (-0.50)    119.311  (-1.21)    119.581  (-0.98)
    splash2x/water_nsquared 376.599  373.411  (-0.85)    382.601  (1.59)     348.701  (-7.41)    357.033  (-5.20)    397.427  (5.53)
    splash2x/water_spatial  132.619  133.432  (0.61)     135.505  (2.18)     134.865  (1.69)     133.940  (1.00)     148.196  (11.75)
    total                   4772.510 4838.740 (1.39)     4862.740 (1.89)     4568.970 (-4.26)    4592.160 (-3.78)    5189.560 (8.74)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1825022.800  1863815.200  (2.13)     1830082.000  (0.28)     1800999.800  (-1.32)    1807743.800  (-0.95)    1580027.800  (-13.42)
    parsec3/bodytrack       1425506.800  1438323.400  (0.90)     1439260.600  (0.96)     1400505.600  (-1.75)    1412295.200  (-0.93)    1412759.600  (-0.89)
    parsec3/canneal         1040902.600  1050404.000  (0.91)     1053535.200  (1.21)     1027175.800  (-1.32)    1035229.400  (-0.55)    1039159.400  (-0.17)
    parsec3/dedup           2526700.400  2540671.600  (0.55)     2503689.800  (-0.91)    2544440.200  (0.70)     2510519.000  (-0.64)    2503148.200  (-0.93)
    parsec3/facesim         545844.600   550680.000   (0.89)     543658.600   (-0.40)    532320.200   (-2.48)    539429.600   (-1.18)    470836.800   (-13.74)
    parsec3/ferret          352118.600   326782.600   (-7.20)    322645.600   (-8.37)    304054.800   (-13.65)   317259.000   (-9.90)    313532.400   (-10.96)
    parsec3/fluidanimate    651597.600   580045.200   (-10.98)   578297.400   (-11.25)   569431.600   (-12.61)   577322.800   (-11.40)   482061.600   (-26.02)
    parsec3/freqmine        989212.000   996291.200   (0.72)     989405.000   (0.02)     970891.000   (-1.85)    981122.000   (-0.82)    736030.000   (-25.59)
    parsec3/raytrace        1749470.400  1751183.200  (0.10)     1740937.600  (-0.49)    1717138.800  (-1.85)    1731298.200  (-1.04)    1528069.000  (-12.66)
    parsec3/streamcluster   123425.400   151548.200   (22.79)    144024.800   (16.69)    118379.000   (-4.09)    124845.400   (1.15)     118629.800   (-3.89)
    parsec3/swaptions       4150.600     25679.200    (518.69)   19914.800    (379.80)   8577.000     (106.64)   17348.200    (317.97)   -100.000     (0.00)
    parsec3/vips            2989801.200  3003285.400  (0.45)     3012055.400  (0.74)     2958369.000  (-1.05)    2970897.800  (-0.63)    2962063.000  (-0.93)
    parsec3/x264            3242663.400  3256091.000  (0.41)     3248949.400  (0.19)     3195605.400  (-1.45)    3206571.600  (-1.11)    3219046.333  (-0.73)
    splash2x/barnes         1208017.600  1212702.600  (0.39)     1194143.600  (-1.15)    1208450.200  (0.04)     1212607.600  (0.38)     878554.667   (-27.27)
    splash2x/fft            9786259.000  9705563.600  (-0.82)    9391006.800  (-4.04)    9967230.600  (1.85)     9657639.400  (-1.31)    10215759.333 (4.39)
    splash2x/lu_cb          512130.400   521431.800   (1.82)     513051.400   (0.18)     508534.200   (-0.70)    512643.600   (0.10)     328017.333   (-35.95)
    splash2x/lu_ncb         511156.200   526566.400   (3.01)     513230.400   (0.41)     509823.800   (-0.26)    516302.000   (1.01)     418078.333   (-18.21)
    splash2x/ocean_cp       3353269.200  3319496.000  (-1.01)    3251575.000  (-3.03)    3379639.800  (0.79)     3326416.600  (-0.80)    3143859.667  (-6.24)
    splash2x/ocean_ncp      3905538.200  3914929.600  (0.24)     3877493.200  (-0.72)    7053949.400  (80.61)    4633035.000  (18.63)    3527482.667  (-9.68)
    splash2x/radiosity      1462030.400  1468050.000  (0.41)     1454997.600  (-0.48)    1466985.400  (0.34)     1461777.400  (-0.02)    441332.000   (-69.81)
    splash2x/radix          2367200.800  2363995.000  (-0.14)    2251124.600  (-4.90)    2417603.800  (2.13)     2317804.000  (-2.09)    2495581.667  (5.42)
    splash2x/raytrace       42356.200    56270.200    (32.85)    49419.000    (16.67)    86408.400    (104.00)   50547.600    (19.34)    40341.000    (-4.76)
    splash2x/volrend        148631.600   162954.600   (9.64)     153305.200   (3.14)     140089.200   (-5.75)    149831.200   (0.81)     150232.000   (1.08)
    splash2x/water_nsquared 39835.800    54268.000    (36.23)    53659.400    (34.70)    41073.600    (3.11)     85322.600    (114.19)   49463.667    (24.17)
    splash2x/water_spatial  669746.600   679634.200   (1.48)     667518.600   (-0.33)    664383.800   (-0.80)    684470.200   (2.20)     401946.000   (-39.99)
    total                   41472600.000 41520700.000 (0.12)     40796900.000 (-1.63)    44592000.000 (7.52)     41840100.000 (0.89)     38456146.000 (-7.27)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 1.39%
runtime overhead and 0.12% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead (1.89% for
runtime and -1.63% for memory footprint).

For a convenient test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 4.26% speedup but incurs 7.52% memory
overhead.  It achieves 34.26% speedup in the best case, but 80.61% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 3.78% speedup and 0.89% memory overhead.  In other words, 'ethp' removes
88.16% of THP memory waste while preserving 88.73% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 76.90% of THP memory waste
while preserving 23.23% of THP speedup.


Proactive Reclamation
---------------------

As similar to the original work, I use 4G 'zram' swap device for this
configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
8.74% runtime overhead in total while achieving 7.27% system memory footprint
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    587078.800   586930.400   (-0.03)    586355.200   (-0.12)    586147.400   (-0.16)    585203.400   (-0.32)    243110.800   (-58.59)
    parsec3/bodytrack       32470.800    32488.400    (0.05)     32351.000    (-0.37)    32433.400    (-0.12)    32429.000    (-0.13)    18804.800    (-42.09)
    parsec3/canneal         842418.600   842442.800   (0.00)     844396.000   (0.23)     840756.400   (-0.20)    841242.000   (-0.14)    825296.200   (-2.03)
    parsec3/dedup           1180100.000  1179309.200  (-0.07)    1160477.800  (-1.66)    1198789.200  (1.58)     1171802.600  (-0.70)    595531.600   (-49.54)
    parsec3/facesim         312056.000   312109.200   (0.02)     312044.400   (-0.00)    318102.200   (1.94)     316239.600   (1.34)     192002.600   (-38.47)
    parsec3/ferret          99792.200    99641.800    (-0.15)    99044.800    (-0.75)    102041.800   (2.25)     100854.000   (1.06)     83628.200    (-16.20)
    parsec3/fluidanimate    530735.400   530759.000   (0.00)     530865.200   (0.02)     532440.800   (0.32)     522778.600   (-1.50)    433547.400   (-18.31)
    parsec3/freqmine        552951.000   552788.000   (-0.03)    552761.800   (-0.03)    556004.400   (0.55)     554001.200   (0.19)     47881.200    (-91.34)
    parsec3/raytrace        883966.600   880061.400   (-0.44)    883144.800   (-0.09)    871786.400   (-1.38)    881000.200   (-0.34)    267210.800   (-69.77)
    parsec3/streamcluster   110901.600   110863.400   (-0.03)    110893.600   (-0.01)    115612.600   (4.25)     114976.800   (3.67)     109728.600   (-1.06)
    parsec3/swaptions       5708.800     5712.400     (0.06)     5681.400     (-0.48)    5720.400     (0.20)     5726.000     (0.30)     -100.000     (0.00)
    parsec3/vips            32272.200    32427.400    (0.48)     31959.800    (-0.97)    34177.800    (5.90)     33306.400    (3.20)     28869.000    (-10.55)
    parsec3/x264            81878.000    81914.200    (0.04)     81823.600    (-0.07)    83579.400    (2.08)     83236.800    (1.66)     81220.667    (-0.80)
    splash2x/barnes         1211917.400  1211328.200  (-0.05)    1212450.400  (0.04)     1221951.000  (0.83)     1218924.600  (0.58)     489430.333   (-59.62)
    splash2x/fft            9874359.000  9934912.400  (0.61)     9843789.600  (-0.31)    10204484.600 (3.34)     9980640.400  (1.08)     7003881.000  (-29.07)
    splash2x/lu_cb          509066.200   509222.600   (0.03)     509059.600   (-0.00)    509594.600   (0.10)     509479.000   (0.08)     315538.667   (-38.02)
    splash2x/lu_ncb         509192.200   508437.000   (-0.15)    509331.000   (0.03)     509606.000   (0.08)     509578.200   (0.08)     412065.667   (-19.07)
    splash2x/ocean_cp       3380283.800  3380301.000  (0.00)     3377617.200  (-0.08)    3416531.200  (1.07)     3389845.200  (0.28)     2398084.000  (-29.06)
    splash2x/ocean_ncp      3917913.600  3924529.200  (0.17)     3934911.800  (0.43)     7123907.400  (81.83)    4703623.600  (20.05)    2428288.000  (-38.02)
    splash2x/radiosity      1467978.600  1468655.400  (0.05)     1467534.000  (-0.03)    1477722.600  (0.66)     1471036.000  (0.21)     148573.333   (-89.88)
    splash2x/radix          2413933.400  2408367.600  (-0.23)    2381122.400  (-1.36)    2480169.400  (2.74)     2367118.800  (-1.94)    1848857.000  (-23.41)
    splash2x/raytrace       23280.000    23272.800    (-0.03)    23259.000    (-0.09)    28715.600    (23.35)    28354.400    (21.80)    13302.333    (-42.86)
    splash2x/volrend        44079.400    44091.600    (0.03)     44022.200    (-0.13)    44547.200    (1.06)     44615.600    (1.22)     29833.000    (-32.32)
    splash2x/water_nsquared 29392.800    29425.600    (0.11)     29422.400    (0.10)     30317.800    (3.15)     30602.200    (4.11)     21769.000    (-25.94)
    splash2x/water_spatial  658604.400   660276.800   (0.25)     660334.000   (0.26)     660491.000   (0.29)     660636.400   (0.31)     304246.667   (-53.80)
    total                   29292400.000 29350400.000 (0.20)     29224634.000 (-0.23)    32985491.000 (12.61)    30157300.000 (2.95)     18340700.000 (-37.39)

In total, 37.39% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 91.34% of residential sets and 25.59% of
system memory usage while incurring only 1.58% runtime overhead.
