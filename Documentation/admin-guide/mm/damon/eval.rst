.. SPDX-License-Identifier: GPL-2.0

==========
Evaluation
==========

DAMON is lightweight.  It increases system memory usage by only -0.25% and
consumes less than 1% CPU time in most case.  It slows target workloads down by
only 0.94%.

DAMON is accurate and useful for memory management optimizations.  An
experimental DAMON-based operation scheme for THP, 'ethp', removes 31.29% of
THP memory overheads while preserving 60.64% of THP speedup.  Another
experimental DAMON-based 'proactive reclamation' implementation, 'prcl',
reduces 87.95% of residential sets and 29.52% of system memory footprint while
incurring only 2.15% runtime overhead in the best case (parsec3/freqmine).

Setup
=====

On a QEMU/KVM based virtual machine utilizing 20GB of RAM and hosted by an
Intel i7 machine that running a kernel that v16 DAMON patchset is applied, I
measure runtime and consumed system memory while running various realistic
workloads with several configurations.  I use 13 and 12 workloads in PARSEC3
[3]_ and SPLASH-2X [4]_ benchmark suites, respectively.  I use another wrapper
scripts [5]_ for convenient setup and run of the workloads.

Measurement
-----------

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.  To make results more stable, I repeat the runs 5 times
and average results.

Configurations
--------------

The configurations I use are as below.

- orig: Linux v5.7 with 'madvise' THP policy
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
    # pages if a region >=2MB shows <5% access rate for >=13 seconds
    null    null    5       null    null    null    hugepage
    2M      null    null    null    13s     null    nohugepage

    # prcl: If a region >=4KB shows <=5% access rate for >=7 seconds, page out.
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
    parsec3/blackscholes    107.228  107.859  (0.59)     108.110  (0.82)     107.381  (0.14)     106.811  (-0.39)    114.766  (7.03)
    parsec3/bodytrack       79.292   79.609   (0.40)     79.777   (0.61)     79.313   (0.03)     78.892   (-0.50)    80.398   (1.40)
    parsec3/canneal         148.887  150.878  (1.34)     153.337  (2.99)     127.873  (-14.11)   132.272  (-11.16)   167.631  (12.59)
    parsec3/dedup           11.970   11.975   (0.04)     12.024   (0.45)     11.752   (-1.82)    11.921   (-0.41)    13.244   (10.64)
    parsec3/facesim         212.800  215.927  (1.47)     215.004  (1.04)     205.117  (-3.61)    207.401  (-2.54)    220.834  (3.78)
    parsec3/ferret          190.646  192.560  (1.00)     192.414  (0.93)     190.662  (0.01)     192.309  (0.87)     193.497  (1.50)
    parsec3/fluidanimate    213.951  216.459  (1.17)     217.578  (1.70)     209.500  (-2.08)    211.826  (-0.99)    218.299  (2.03)
    parsec3/freqmine        291.050  292.117  (0.37)     293.279  (0.77)     289.553  (-0.51)    291.768  (0.25)     297.309  (2.15)
    parsec3/raytrace        118.645  119.734  (0.92)     119.521  (0.74)     117.715  (-0.78)    118.844  (0.17)     134.045  (12.98)
    parsec3/streamcluster   332.843  336.997  (1.25)     337.049  (1.26)     279.716  (-15.96)   290.985  (-12.58)   346.646  (4.15)
    parsec3/swaptions       155.437  157.174  (1.12)     156.159  (0.46)     155.017  (-0.27)    154.955  (-0.31)    156.555  (0.72)
    parsec3/vips            59.215   59.426   (0.36)     59.156   (-0.10)    59.243   (0.05)     58.858   (-0.60)    60.184   (1.64)
    parsec3/x264            67.445   71.400   (5.86)     71.122   (5.45)     64.078   (-4.99)    66.027   (-2.10)    71.489   (6.00)
    splash2x/barnes         81.826   81.800   (-0.03)    82.648   (1.00)     74.343   (-9.15)    79.063   (-3.38)    103.785  (26.84)
    splash2x/fft            33.850   34.148   (0.88)     33.912   (0.18)     23.493   (-30.60)   32.684   (-3.44)    48.303   (42.70)
    splash2x/lu_cb          86.404   86.333   (-0.08)    86.988   (0.68)     85.720   (-0.79)    85.944   (-0.53)    89.338   (3.40)
    splash2x/lu_ncb         94.908   98.021   (3.28)     96.041   (1.19)     90.304   (-4.85)    93.279   (-1.72)    97.270   (2.49)
    splash2x/ocean_cp       47.122   47.391   (0.57)     47.902   (1.65)     43.227   (-8.26)    44.609   (-5.33)    51.410   (9.10)
    splash2x/ocean_ncp      93.147   92.911   (-0.25)    93.886   (0.79)     51.451   (-44.76)   71.107   (-23.66)   112.554  (20.83)
    splash2x/radiosity      92.150   92.604   (0.49)     93.339   (1.29)     90.802   (-1.46)    91.824   (-0.35)    104.439  (13.34)
    splash2x/radix          31.961   32.113   (0.48)     32.066   (0.33)     25.184   (-21.20)   30.412   (-4.84)    49.989   (56.41)
    splash2x/raytrace       84.781   85.278   (0.59)     84.763   (-0.02)    83.192   (-1.87)    83.970   (-0.96)    85.382   (0.71)
    splash2x/volrend        87.401   87.978   (0.66)     87.977   (0.66)     86.636   (-0.88)    87.169   (-0.26)    88.043   (0.73)
    splash2x/water_nsquared 239.140  239.570  (0.18)     240.901  (0.74)     221.323  (-7.45)    224.670  (-6.05)    244.492  (2.24)
    splash2x/water_spatial  89.538   89.978   (0.49)     90.171   (0.71)     89.729   (0.21)     89.238   (-0.34)    99.331   (10.94)
    total                   3051.620 3080.230 (0.94)     3085.130 (1.10)     2862.320 (-6.20)    2936.830 (-3.76)    3249.240 (6.48)


    memused.avg             orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    1676679.200  1683789.200  (0.42)     1680281.200  (0.21)     1613817.400  (-3.75)    1835229.200  (9.46)     1407952.800  (-16.03)
    parsec3/bodytrack       1295736.000  1308412.600  (0.98)     1311988.000  (1.25)     1243417.400  (-4.04)    1435410.600  (10.78)    1255566.400  (-3.10)
    parsec3/canneal         1004062.000  1008823.800  (0.47)     1000100.200  (-0.39)    983976.000   (-2.00)    1051719.600  (4.75)     993055.800   (-1.10)
    parsec3/dedup           2389765.800  2393381.000  (0.15)     2366668.200  (-0.97)    2412948.600  (0.97)     2435885.600  (1.93)     2380172.800  (-0.40)
    parsec3/facesim         488927.200   498228.000   (1.90)     496683.800   (1.59)     476327.800   (-2.58)    552890.000   (13.08)    449143.600   (-8.14)
    parsec3/ferret          280324.600   282032.400   (0.61)     282284.400   (0.70)     258211.000   (-7.89)    331493.800   (18.25)    265850.400   (-5.16)
    parsec3/fluidanimate    560636.200   569038.200   (1.50)     565067.400   (0.79)     556923.600   (-0.66)    588021.200   (4.88)     512901.600   (-8.51)
    parsec3/freqmine        883286.000   904960.200   (2.45)     886105.200   (0.32)     849347.400   (-3.84)    998358.000   (13.03)    622542.800   (-29.52)
    parsec3/raytrace        1639370.200  1642318.200  (0.18)     1626673.200  (-0.77)    1591284.200  (-2.93)    1755088.400  (7.06)     1410261.600  (-13.98)
    parsec3/streamcluster   116955.600   127251.400   (8.80)     121441.000   (3.84)     113853.800   (-2.65)    139659.400   (19.41)    120335.200   (2.89)
    parsec3/swaptions       8342.400     18555.600    (122.43)   16581.200    (98.76)    6745.800     (-19.14)   27487.200    (229.49)   14275.600    (71.12)
    parsec3/vips            2776417.600  2784989.400  (0.31)     2820564.600  (1.59)     2694060.800  (-2.97)    2968650.000  (6.92)     2713590.000  (-2.26)
    parsec3/x264            2912885.000  2936474.600  (0.81)     2936775.800  (0.82)     2799599.200  (-3.89)    3168695.000  (8.78)     2829085.800  (-2.88)
    splash2x/barnes         1206459.600  1204145.600  (-0.19)    1177390.000  (-2.41)    1210556.800  (0.34)     1214978.800  (0.71)     907737.000   (-24.76)
    splash2x/fft            9384156.400  9258749.600  (-1.34)    8560377.800  (-8.78)    9337563.000  (-0.50)    9228873.600  (-1.65)    9823394.400  (4.68)
    splash2x/lu_cb          510210.800   514052.800   (0.75)     502735.200   (-1.47)    514459.800   (0.83)     523884.200   (2.68)     367563.200   (-27.96)
    splash2x/lu_ncb         510091.200   516046.800   (1.17)     505327.600   (-0.93)    512568.200   (0.49)     524178.400   (2.76)     427981.800   (-16.10)
    splash2x/ocean_cp       3342260.200  3294531.200  (-1.43)    3171236.000  (-5.12)    3379693.600  (1.12)     3314896.600  (-0.82)    3252406.000  (-2.69)
    splash2x/ocean_ncp      3900447.200  3881682.600  (-0.48)    3816493.200  (-2.15)    7065506.200  (81.15)    4449224.400  (14.07)    3829931.200  (-1.81)
    splash2x/radiosity      1466372.000  1463840.200  (-0.17)    1438554.000  (-1.90)    1475151.600  (0.60)     1474828.800  (0.58)     496636.000   (-66.13)
    splash2x/radix          1760056.600  1691719.000  (-3.88)    1613057.400  (-8.35)    1384416.400  (-21.34)   1632274.400  (-7.26)    2141640.200  (21.68)
    splash2x/raytrace       38794.000    48187.400    (24.21)    46728.400    (20.45)    41323.400    (6.52)     61499.800    (58.53)    68455.200    (76.46)
    splash2x/volrend        138107.400   148197.000   (7.31)     146223.400   (5.88)     128076.400   (-7.26)    164593.800   (19.18)    140885.200   (2.01)
    splash2x/water_nsquared 39072.000    49889.200    (27.69)    47548.400    (21.69)    37546.400    (-3.90)    57195.400    (46.38)    42994.200    (10.04)
    splash2x/water_spatial  662099.800   665964.800   (0.58)     651017.000   (-1.67)    659808.400   (-0.35)    674475.600   (1.87)     519677.600   (-21.51)
    total                   38991500.000 38895300.000 (-0.25)    37787817.000 (-3.09)    41347200.000 (6.04)     40609600.000 (4.15)     36994100.000 (-5.12)


DAMON Overheads
---------------

In total, DAMON virtual memory access recording feature ('rec') incurs 0.94%
runtime overhead and -0.25% memory space overhead.  Even though the size of the
monitoring target region becomes much larger with the physical memory access
recording ('prec'), it still shows only modest amount of overhead (1.10% for
runtime and -3.09% for memory footprint).

For a convenience test run of 'rec' and 'prec', I use a Python wrapper.  The
wrapper constantly consumes about 10-15MB of memory.  This becomes a high
memory overhead if the target workload has a small memory footprint.
Nonetheless, the overheads are not from DAMON, but from the wrapper, and thus
should be ignored.  This fake memory overhead continues in 'ethp' and 'prcl',
as those configurations are also using the Python wrapper.


Efficient THP
-------------

THP 'always' enabled policy achieves 6.20% speedup but incurs 6.04% memory
overhead.  It achieves 44.76% speedup in the best case, but 81.15% memory
overhead in the worst case.  Interestingly, both the best and worst-case are
with 'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 3.76% speedup and 4.15% memory overhead.  In other words, 'ethp' removes
31.29% of THP memory waste while preserving 60.64% of THP speedup in total.  In
the case of the 'splash2x/ocean_ncp', 'ethp' removes 82.66% of THP memory waste
while preserving 52.85% of THP speedup.


Proactive Reclamation
---------------------

As similar to the original work, I use 4G 'zram' swap device for this
configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
6.48% runtime overhead in total while achieving 5.12% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads::

    rss.avg                 orig         rec          (overhead) prec         (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
    parsec3/blackscholes    590412.200   589991.400   (-0.07)    591716.400   (0.22)     591131.000   (0.12)     591055.200   (0.11)     274623.600   (-53.49)
    parsec3/bodytrack       32202.200    32297.400    (0.30)     32301.400    (0.31)     32328.000    (0.39)     32169.800    (-0.10)    25311.200    (-21.40)
    parsec3/canneal         840063.600   839145.200   (-0.11)    839506.200   (-0.07)    835102.600   (-0.59)    839766.000   (-0.04)    833091.800   (-0.83)
    parsec3/dedup           1185493.200  1202688.800  (1.45)     1204597.000  (1.61)     1238071.400  (4.44)     1201689.400  (1.37)     920688.600   (-22.34)
    parsec3/facesim         311570.400   311542.000   (-0.01)    311665.000   (0.03)     316106.400   (1.46)     312003.400   (0.14)     252646.000   (-18.91)
    parsec3/ferret          99783.200    99330.000    (-0.45)    99735.000    (-0.05)    102000.600   (2.22)     99927.400    (0.14)     90967.400    (-8.83)
    parsec3/fluidanimate    531780.800   531800.800   (0.00)     531754.600   (-0.00)    532009.600   (0.04)     531822.400   (0.01)     479116.000   (-9.90)
    parsec3/freqmine        551787.600   551550.600   (-0.04)    551950.000   (0.03)     556030.000   (0.77)     553720.400   (0.35)     66480.000    (-87.95)
    parsec3/raytrace        895247.000   895240.200   (-0.00)    895770.400   (0.06)     895880.200   (0.07)     893516.600   (-0.19)    327339.600   (-63.44)
    parsec3/streamcluster   110862.200   110840.400   (-0.02)    110878.600   (0.01)     112067.200   (1.09)     112010.800   (1.04)     109763.600   (-0.99)
    parsec3/swaptions       5630.000     5580.800     (-0.87)    5599.600     (-0.54)    5624.200     (-0.10)    5697.400     (1.20)     3792.400     (-32.64)
    parsec3/vips            31677.200    31881.800    (0.65)     31785.800    (0.34)     32177.000    (1.58)     32456.800    (2.46)     29692.000    (-6.27)
    parsec3/x264            81796.400    81918.600    (0.15)     81827.600    (0.04)     82734.800    (1.15)     82854.000    (1.29)     81478.200    (-0.39)
    splash2x/barnes         1216014.600  1215462.000  (-0.05)    1218535.200  (0.21)     1227689.400  (0.96)     1219022.000  (0.25)     650771.000   (-46.48)
    splash2x/fft            9622775.200  9511973.400  (-1.15)    9688178.600  (0.68)     9733868.400  (1.15)     9651488.000  (0.30)     7567077.400  (-21.36)
    splash2x/lu_cb          511102.400   509911.600   (-0.23)    511123.800   (0.00)     514466.800   (0.66)     510462.800   (-0.13)    361014.000   (-29.37)
    splash2x/lu_ncb         510569.800   510724.600   (0.03)     510888.800   (0.06)     513951.600   (0.66)     509474.400   (-0.21)    424030.400   (-16.95)
    splash2x/ocean_cp       3413563.600  3413721.800  (0.00)     3398399.600  (-0.44)    3446878.000  (0.98)     3404799.200  (-0.26)    3244787.400  (-4.94)
    splash2x/ocean_ncp      3927797.400  3936294.400  (0.22)     3917698.800  (-0.26)    7181781.200  (82.85)    4525783.600  (15.22)    3693747.800  (-5.96)
    splash2x/radiosity      1477264.800  1477569.200  (0.02)     1476954.200  (-0.02)    1485724.800  (0.57)     1474684.800  (-0.17)    230128.000   (-84.42)
    splash2x/radix          1773025.000  1754424.200  (-1.05)    1743194.400  (-1.68)    1445575.200  (-18.47)   1694855.200  (-4.41)    1769750.000  (-0.18)
    splash2x/raytrace       23292.000    23284.000    (-0.03)    23292.800    (0.00)     28704.800    (23.24)    26489.600    (13.73)    15753.000    (-32.37)
    splash2x/volrend        44095.800    44068.200    (-0.06)    44107.600    (0.03)     44114.600    (0.04)     44054.000    (-0.09)    31616.000    (-28.30)
    splash2x/water_nsquared 29416.800    29403.200    (-0.05)    29406.400    (-0.04)    30103.200    (2.33)     29433.600    (0.06)     24927.400    (-15.26)
    splash2x/water_spatial  657791.000   657840.400   (0.01)     657826.600   (0.01)     657595.800   (-0.03)    656617.800   (-0.18)    481334.800   (-26.83)
    total                   28475091.000 28368400.000 (-0.37)    28508700.000 (0.12)     31641800.000 (11.12)    29036000.000 (1.97)     21989800.000 (-22.78)

In total, 22.78% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 87.95% of residential sets and 29.52% of
system memory usage while incurring only 2.15% runtime overhead.
