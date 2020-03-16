Implement Data Access Monitoring-based Memory Operation Schemes

DAMON[1] can be used as a primitive for data access awared memory management
optimizations.  That said, users who want such optimizations should run DAMON,
read the monitoring results, analyze it, plan a new memory management scheme,
and apply the new scheme by themselves.  Such efforts will be inevitable for
some complicated optimizations.

However, in many other cases, the users could simply want the system to apply a
memory management action to a memory region of a specific size having a
specific access frequency for a specific time.  For example, "page out a memory
region larger than 100 MiB keeping only rare accesses more than 2 minutes", or
"Do not use THP for a memory region larger than 2 MiB rarely accessed for more
than 1 seconds".

This RFC patchset makes DAMON to handle such data access monitoring-based
operation schemes.  With this change, users can do the data access awared
optimizations by simply specifying their schemes to DAMON.


Evaluations
===========

Efficient THP
-------------

Transparent Huge Pages (THP) subsystem could waste memory space in some cases
because it aggressively promotes regular pages to huge pages.  For the reason,
use of THP is prohivited by a number of memory intensive programs such as
Redis[1] and MongoDB[2].

Below two simple data access monitoring-based operation schemes might be
helpful for the problem:

    # format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>

    # If a memory region larger than 2 MiB is showing access rate higher than
    # 5%, apply MADV_HUGEPAGE to the region.
    2M	null	5	null	null	null	hugepage

    # If a memory region larger than 2 MiB is showing access rate lower than 5%
    # for more than 1 second, apply MADV_NOHUGEPAGE to the region.
    2M	null	null	5	1s	null	nohugepage

We can expect the schmes would reduce the memory space overhead but preserve
some of the performance benefit of THP.  I call this schemes Efficient THP
(ETHP).

Please note that these schemes are neither highly tuned nor for general
usecases.  These are made with my straightforward instinction for only a
demonstration of DAMOS.


Setup
-----

On my personal QEMU/KVM based virtual machine on an Intel i7 host machine
running Ubuntu 18.04, I measure runtime and consumed memory space of various
realistic workloads with several configurations.  I use 13 and 12 workloads in
PARSEC3[3] and SPLASH-2X[4] benchmark suites, respectively.  I personally use
another wrapper scripts[5] for setup and run of the workloads.

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.

The configurations I use are as below:

    orig: Linux v5.5 with 'madvise' THP policy
    thp: Linux v5.5 with 'always' THP policy
    ethp: Linux v5.5 applying the above schemes

To minimize the measurement errors, I repeat the run 5 times and average
results.  You can get stdev, min, and max of the numbers among the repeated
runs in appendix below.


[1] "Redis latency problems troubleshooting", https://redis.io/topics/latency
[2] "Disable Transparent Huge Pages (THP)",
    https://docs.mongodb.com/manual/tutorial/transparent-huge-pages/
[3] "The PARSEC Becnhmark Suite", https://parsec.cs.princeton.edu/index.htm
[4] "SPLASH-2x", https://parsec.cs.princeton.edu/parsec3-doc.htm#splash2x
[5] "parsec3_on_ubuntu", https://github.com/sjp38/parsec3_on_ubuntu


Results
-------

TL;DR: 'ethp' removes 97.61% of 'thp' memory space overhead while preserving
25.40% (up to 88.36%) of 'thp' performance improvement in total.

Following sections show the results of the measurements with raw numbers and
'orig'-relative overheads (percent) of each configuration.


Memory Space Overheads
~~~~~~~~~~~~~~~~~~~~~~

Below shows measured memory space overheads.  Raw numbers are in KiB, and the
overheads in parentheses are in percent.  For example, 'parsec3/blackscholes'
consumes about 1.819 GiB and 1.824 GiB with 'orig' and 'thp' configuration,
respectively.  The overhead of 'thp' compared to 'orig' for the workload is
0.3%.

              workloads  orig         thp (overhead)        ethp (overhead)
   parsec3/blackscholes  1819486.000  1824921.400 (  0.30)  1829070.600 (  0.53)
      parsec3/bodytrack  1417885.800  1417077.600 ( -0.06)  1427560.800 (  0.68)
        parsec3/canneal  1043876.800  1039773.000 ( -0.39)  1048445.200 (  0.44)
          parsec3/dedup  2400000.400  2434625.600 (  1.44)  2417374.400 (  0.72)
        parsec3/facesim  540206.400   542422.400 (  0.41)   551485.400 (  2.09)
         parsec3/ferret  320480.200   320157.000 ( -0.10)   331470.400 (  3.43)
   parsec3/fluidanimate  573961.400   572329.600 ( -0.28)   581836.000 (  1.37)
       parsec3/freqmine  983981.200   994839.600 (  1.10)   996124.600 (  1.23)
       parsec3/raytrace  1745175.200  1742756.400 ( -0.14)  1751706.000 (  0.37)
  parsec3/streamcluster  120558.800   120309.800 ( -0.21)   131997.800 (  9.49)
      parsec3/swaptions  14820.400    23388.800 ( 57.81)    24698.000 ( 66.65)
           parsec3/vips  2956319.200  2955803.600 ( -0.02)  2977506.200 (  0.72)
           parsec3/x264  3187699.000  3184944.000 ( -0.09)  3198462.800 (  0.34)
        splash2x/barnes  1212774.800  1221892.400 (  0.75)  1212100.800 ( -0.06)
           splash2x/fft  9364725.000  9267074.000 ( -1.04)  8997901.200 ( -3.92)
         splash2x/lu_cb  515242.400   519881.400 (  0.90)   526621.600 (  2.21)
        splash2x/lu_ncb  517308.000   520396.400 (  0.60)   521732.400 (  0.86)
      splash2x/ocean_cp  3348189.400  3380799.400 (  0.97)  3328473.400 ( -0.59)
     splash2x/ocean_ncp  3908599.800  7072076.800 ( 80.94)  4449410.400 ( 13.84)
     splash2x/radiosity  1469087.800  1482244.400 (  0.90)  1471781.000 (  0.18)
         splash2x/radix  1712487.400  1385972.800 (-19.07)  1420461.800 (-17.05)
      splash2x/raytrace  45030.600    50946.600 ( 13.14)    58586.200 ( 30.10)
       splash2x/volrend  151037.800   151188.000 (  0.10)   163213.600 (  8.06)
splash2x/water_nsquared  47442.400    47257.000 ( -0.39)    59285.800 ( 24.96)
 splash2x/water_spatial  667355.200   666824.400 ( -0.08)   673274.400 (  0.89)
                  total  40083800.000 42939900.000 (  7.13) 40150600.000 (  0.17)

In total, 'thp' shows 7.13% memory space overhead while 'ethp' shows only 0.17%
overhead.  In other words, 'ethp' removed 97.61% of 'thp' memory space
overhead.

For almost every workload, 'ethp' constantly show about 10-15 MiB memory space
overhead, mainly due to its python wrapper I used for convenient test runs.
Using DAMON's raw interface would further remove this overhead.

In case of 'parsec3/swaptions' and 'splash2x/raytrace', 'ethp' shows even
higher memory space overhead.  This is mainly due to the small size of the
workloads and the constant memory overhead of 'ethp', which came from the
python wrapper.  The workloads consumes only about 14 MiB and 45 MiB each.
Because the constant memory consumption from the python wrapper of 'ethp'
(about 10-15 MiB) is relatively huge to the small working set, the relative
overhead becomes high.  Nonetheless, such small workloads are not appropriate
target of the 'ethp' and the overhead can be removed by avoiding use of the
wrapper.


Runtime Overheads
~~~~~~~~~~~~~~~~~

Below shows measured runtime in similar way.  The raw numbers are in seconds
and the overheads are in percent.  Minus runtime overheads mean speedup.

                runtime  orig      thp (overhead)     ethp (overhead)
   parsec3/blackscholes  107.003   106.468 ( -0.50)   107.260 (  0.24)
      parsec3/bodytrack  78.854    78.757 ( -0.12)    79.261 (  0.52)
        parsec3/canneal  137.520   120.854 (-12.12)   132.427 ( -3.70)
          parsec3/dedup  11.873    11.665 ( -1.76)    11.883 (  0.09)
        parsec3/facesim  207.895   204.215 ( -1.77)   206.170 ( -0.83)
         parsec3/ferret  190.507   189.972 ( -0.28)   190.818 (  0.16)
   parsec3/fluidanimate  211.064   208.862 ( -1.04)   211.874 (  0.38)
       parsec3/freqmine  290.157   288.831 ( -0.46)   292.495 (  0.81)
       parsec3/raytrace  118.460   118.741 (  0.24)   119.808 (  1.14)
  parsec3/streamcluster  324.524   283.709 (-12.58)   307.209 ( -5.34)
      parsec3/swaptions  154.458   154.894 (  0.28)   155.307 (  0.55)
           parsec3/vips  58.588    58.622 (  0.06)    59.037 (  0.77)
           parsec3/x264  66.493    66.604 (  0.17)    67.051 (  0.84)
        splash2x/barnes  79.769    73.886 ( -7.38)    78.737 ( -1.29)
           splash2x/fft  32.857    22.960 (-30.12)    25.808 (-21.45)
         splash2x/lu_cb  85.113    84.939 ( -0.20)    85.344 (  0.27)
        splash2x/lu_ncb  92.408    90.103 ( -2.49)    93.585 (  1.27)
      splash2x/ocean_cp  44.374    42.876 ( -3.37)    43.613 ( -1.71)
     splash2x/ocean_ncp  80.710    51.831 (-35.78)    71.498 (-11.41)
     splash2x/radiosity  90.626    90.398 ( -0.25)    91.238 (  0.68)
         splash2x/radix  30.875    25.226 (-18.30)    25.882 (-16.17)
      splash2x/raytrace  84.114    82.602 ( -1.80)    85.124 (  1.20)
       splash2x/volrend  86.796    86.347 ( -0.52)    88.223 (  1.64)
splash2x/water_nsquared  230.781   220.667 ( -4.38)   232.664 (  0.82)
 splash2x/water_spatial  88.719    90.187 (  1.65)    89.228 (  0.57)
                  total  2984.530  2854.220 ( -4.37)  2951.540 ( -1.11)

In total, 'thp' shows 4.37% speedup while 'ethp' shows 1.11% speedup.  In other
words, 'ethp' preserves about 25.40% of THP performance benefit.

In the best case (splash2x/raytrace), 'ethp' preserves 88.36% of the benefit.

If we narrow down to workloads showing high THP performance benefits
(splash2x/fft, splash2x/ocean_ncp, and splash2x/radix), 'thp' and 'ethp' shows
30.75% and 14.71% speedup in total, respectively.  In other words, 'ethp'
preserves about 47.83% of the benefit.

Even in the worst case (splash2x/volrend), 'ethp' incurs only 1.64% runtime
overhead, which is similar to that of 'thp' (1.65% for
'splash2x/water_spatial').


Sequence Of Patches
===================

The patches are based on the v5.5 plus v5 DAMON patchset[1] and Minchan's
``madvise()`` factor-out patch[2].  Minchan's patch was necessary for reuse of
``madvise()`` code in DAMON.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v4

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v4


[1] https://lore.kernel.org/linux-mm/20200217103110.30817-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200128001641.5086-2-minchan@kernel.org/

The first patch allows DAMON to reuse ``madvise()`` code for the actions.  The
second patch accounts age of each region.  The third patch implements the
handling of the schemes in DAMON and exports a kernel space programming
interface for it.  The fourth patch implements a debugfs interface for
privileged people and programs.  The fifth and sixth patches each adds
kunittests and selftests for these changes, and finally the seventhe patch
modifies the user space tool for DAMON to support description and applying of
schemes in human freiendly way.


Patch History
=============

Changes from RFC v4
(https://lore.kernel.org/linux-mm/20200303121406.20954-1-sjpark@amazon.com/)
 - Handle CONFIG_ADVISE_SYSCALL

Changes from RFC v3
(https://lore.kernel.org/linux-mm/20200225102300.23895-1-sjpark@amazon.com/)
 - Add Reviewed-by from Brendan Higgins
 - Code cleanup: Modularize madvise() call
 - Fix a trivial bug in the wrapper python script
 - Add more stable and detailed evaluation results with updated ETHP scheme

Changes from RFC v2
(https://lore.kernel.org/linux-mm/20200218085309.18346-1-sjpark@amazon.com/)
 - Fix aging mechanism for more better 'old region' selection
 - Add more kunittests and kselftests for this patchset
 - Support more human friedly description and application of 'schemes'

Changes from RFC v1
(https://lore.kernel.org/linux-mm/20200210150921.32482-1-sjpark@amazon.com/)
 - Properly adjust age accounting related properties after splitting, merging,
   and action applying

==================================== >8 =======================================

Appendix: Stdev / min / max numbers among the repeated runs
===========================================================

Below are stdev/min/max of each number in the 5 repeated runs.

runtime_stdev	orig	thp	ethp
parsec3/blackscholes	0.884	0.932	0.693
parsec3/bodytrack	0.672	0.501	0.470
parsec3/canneal	3.434	1.278	4.112
parsec3/dedup	0.074	0.032	0.070
parsec3/facesim	1.079	0.572	0.688
parsec3/ferret	1.674	0.498	0.801
parsec3/fluidanimate	1.422	1.804	1.273
parsec3/freqmine	2.285	2.735	3.852
parsec3/raytrace	1.240	0.821	1.407
parsec3/streamcluster	2.226	2.221	2.778
parsec3/swaptions	1.760	2.164	1.650
parsec3/vips	0.071	0.113	0.433
parsec3/x264	4.972	4.732	5.464
splash2x/barnes	0.149	0.434	0.944
splash2x/fft	0.186	0.074	2.053
splash2x/lu_cb	0.358	0.674	0.054
splash2x/lu_ncb	0.694	0.586	0.301
splash2x/ocean_cp	0.214	0.181	0.163
splash2x/ocean_ncp	0.738	0.574	5.860
splash2x/radiosity	0.447	0.786	0.493
splash2x/radix	0.183	0.195	0.250
splash2x/raytrace	0.869	1.248	1.071
splash2x/volrend	0.896	0.801	0.759
splash2x/water_nsquared	3.050	3.032	1.750
splash2x/water_spatial	0.497	1.607	0.665


memused.avg_stdev	orig	thp	ethp
parsec3/blackscholes	6837.158	4942.183	5531.310
parsec3/bodytrack	5591.783	5771.259	3959.415
parsec3/canneal	4034.250	5205.223	3294.782
parsec3/dedup	56582.594	12462.196	49390.950
parsec3/facesim	1879.070	3572.512	2407.374
parsec3/ferret	1686.811	4110.648	3050.263
parsec3/fluidanimate	5252.273	3550.694	3577.428
parsec3/freqmine	2634.481	12225.383	2220.963
parsec3/raytrace	5652.660	5615.677	4645.947
parsec3/streamcluster	2296.864	1906.081	2189.578
parsec3/swaptions	1100.155	18202.456	1689.923
parsec3/vips	5260.607	9104.494	2508.632
parsec3/x264	14892.433	18097.263	16853.532
splash2x/barnes	3055.563	2552.379	3749.773
splash2x/fft	115636.847	18058.645	193864.925
splash2x/lu_cb	2266.989	2495.620	9615.377
splash2x/lu_ncb	4816.990	3106.290	3406.873
splash2x/ocean_cp	5597.264	2189.592	40420.686
splash2x/ocean_ncp	6962.524	5038.039	352254.041
splash2x/radiosity	6151.433	1561.840	6976.647
splash2x/radix	12938.174	4141.470	64272.890
splash2x/raytrace	912.177	1473.169	1812.460
splash2x/volrend	1866.708	1527.107	1881.400
splash2x/water_nsquared	2126.581	4481.707	2471.129
splash2x/water_spatial	1495.886	3564.505	3182.864


runtime_min	orig	thp	ethp
parsec3/blackscholes	106.073	105.724	106.799
parsec3/bodytrack	78.361	78.327	78.994
parsec3/canneal	130.735	118.456	125.902
parsec3/dedup	11.816	11.631	11.781
parsec3/facesim	206.358	203.462	205.526
parsec3/ferret	189.118	189.461	190.130
parsec3/fluidanimate	209.879	207.381	210.656
parsec3/freqmine	287.349	285.988	288.519
parsec3/raytrace	117.320	118.014	118.021
parsec3/streamcluster	322.404	280.907	304.489
parsec3/swaptions	153.017	153.133	154.307
parsec3/vips	58.480	58.518	58.496
parsec3/x264	61.569	61.987	62.333
splash2x/barnes	79.595	73.170	77.782
splash2x/fft	32.588	22.838	24.391
splash2x/lu_cb	84.897	84.229	85.300
splash2x/lu_ncb	91.640	89.480	93.192
splash2x/ocean_cp	44.216	42.661	43.403
splash2x/ocean_ncp	79.912	50.717	63.298
splash2x/radiosity	90.332	89.911	90.786
splash2x/radix	30.617	25.012	25.569
splash2x/raytrace	82.972	81.291	83.608
splash2x/volrend	86.205	85.414	86.772
splash2x/water_nsquared	228.749	216.488	230.019
splash2x/water_spatial	88.326	88.636	88.469


memused.avg_min	orig	thp	ethp
parsec3/blackscholes	1809578.000	1815893.000	1821555.000
parsec3/bodytrack	1407270.000	1408774.000	1422950.000
parsec3/canneal	1037996.000	1029491.000	1042278.000
parsec3/dedup	2290578.000	2419128.000	2322004.000
parsec3/facesim	536908.000	539368.000	548194.000
parsec3/ferret	317173.000	313275.000	325452.000
parsec3/fluidanimate	566148.000	566925.000	578031.000
parsec3/freqmine	979565.000	985279.000	992844.000
parsec3/raytrace	1737270.000	1735498.000	1745751.000
parsec3/streamcluster	117213.000	118264.000	127825.000
parsec3/swaptions	13012.000	10753.000	21858.000
parsec3/vips	2946474.000	2941690.000	2975157.000
parsec3/x264	3171581.000	3170872.000	3184577.000
splash2x/barnes	1208476.000	1218535.000	1205510.000
splash2x/fft	9160132.000	9250818.000	8835513.000
splash2x/lu_cb	511850.000	515668.000	519205.000
splash2x/lu_ncb	512127.000	514471.000	518500.000
splash2x/ocean_cp	3342506.000	3377932.000	3290066.000
splash2x/ocean_ncp	3901749.000	7063386.000	3962171.000
splash2x/radiosity	1457419.000	1479232.000	1467156.000
splash2x/radix	1690840.000	1380921.000	1344838.000
splash2x/raytrace	43518.000	48571.000	55468.000
splash2x/volrend	147356.000	148650.000	159562.000
splash2x/water_nsquared	43685.000	38495.000	54409.000
splash2x/water_spatial	665912.000	660742.000	669843.000


runtime_max	orig	thp	ethp
parsec3/blackscholes	108.322	108.141	108.641
parsec3/bodytrack	80.166	79.687	80.200
parsec3/canneal	140.219	122.073	137.615
parsec3/dedup	12.014	11.723	12.000
parsec3/facesim	209.291	205.234	207.192
parsec3/ferret	193.589	190.830	192.235
parsec3/fluidanimate	213.730	212.390	213.867
parsec3/freqmine	293.634	292.283	299.323
parsec3/raytrace	120.096	120.346	121.437
parsec3/streamcluster	327.827	287.094	311.657
parsec3/swaptions	157.661	158.341	158.589
parsec3/vips	58.648	58.815	59.611
parsec3/x264	73.389	73.856	75.369
splash2x/barnes	79.975	74.413	80.244
splash2x/fft	33.168	23.043	29.852
splash2x/lu_cb	85.825	85.914	85.446
splash2x/lu_ncb	93.717	91.074	93.902
splash2x/ocean_cp	44.789	43.190	43.882
splash2x/ocean_ncp	81.981	52.296	80.782
splash2x/radiosity	91.509	91.966	92.180
splash2x/radix	31.130	25.546	26.299
splash2x/raytrace	85.347	84.163	86.881
splash2x/volrend	88.575	87.389	88.957
splash2x/water_nsquared	236.851	224.982	235.537
splash2x/water_spatial	89.689	92.978	90.276


memused.avg_max	orig	thp	ethp
parsec3/blackscholes	1827350.000	1830922.000	1836584.000
parsec3/bodytrack	1423070.000	1422588.000	1434832.000
parsec3/canneal	1048155.000	1043151.000	1051713.000
parsec3/dedup	2446661.000	2452237.000	2459532.000
parsec3/facesim	542340.000	547457.000	554321.000
parsec3/ferret	321678.000	325083.000	333474.000
parsec3/fluidanimate	579067.000	576389.000	587029.000
parsec3/freqmine	986759.000	1018980.000	998800.000
parsec3/raytrace	1750980.000	1749291.000	1757761.000
parsec3/streamcluster	123761.000	122647.000	133602.000
parsec3/swaptions	16305.000	59605.000	26835.000
parsec3/vips	2961299.000	2964746.000	2982101.000
parsec3/x264	3209871.000	3219818.000	3230036.000
splash2x/barnes	1217047.000	1224832.000	1215995.000
splash2x/fft	9505048.000	9302095.000	9378025.000
splash2x/lu_cb	518393.000	522739.000	545540.000
splash2x/lu_ncb	526380.000	522996.000	528341.000
splash2x/ocean_cp	3358820.000	3384581.000	3383533.000
splash2x/ocean_ncp	3920669.000	7079011.000	4937246.000
splash2x/radiosity	1474991.000	1483739.000	1485635.000
splash2x/radix	1731625.000	1393183.000	1498907.000
splash2x/raytrace	46122.000	52292.000	61116.000
splash2x/volrend	152488.000	153180.000	164793.000
splash2x/water_nsquared	49449.000	50555.000	60859.000
splash2x/water_spatial	669943.000	669815.000	679012.000
