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

Setup
-----

On my personal QEMU/KVM based virtual machine on an Intel i7 host machine
running Ubuntu 18.04, I measure runtime and consumed system memory while
running various realistic workloads with several configurations.  I use 13 and
12 workloads in PARSEC3[3] and SPLASH-2X[4] benchmark suites, respectively.  I
personally use another wrapper scripts[5] for setup and run of the workloads.
On top of this patchset, we also applied the DAMON-based operation schemes
patchset[6] for this evaluation.

Measurement
~~~~~~~~~~~

For the measurement of the amount of consumed memory in system global scope, I
drop caches before starting each of the workloads and monitor 'MemFree' in the
'/proc/meminfo' file.  To make results more stable, I repeat the runs 5 times
and average results.  You can get stdev, min, and max of the numbers among the
repeated runs in appendix below.

Configurations
~~~~~~~~~~~~~~

The configurations I use are as below.

orig: Linux v5.5 with 'madvise' THP policy
rec: 'orig' plus DAMON running with record feature
thp: same with 'orig', but use 'always' THP policy
ethp: 'orig' plus a DAMON operation scheme[6], 'efficient THP'
prcl: 'orig' plus a DAMON operation scheme, 'proactive reclaim[7]'

I use 'rec' for measurement of DAMON overheads to target workloads and system
memory.  The remaining configs including 'thp', 'ethp', and 'prcl' are for
measurement of DAMON monitoring accuracy.

'ethp' and 'prcl' is simple DAMON-based operation schemes developed for
proof of concepts of DAMON.  'ethp' reduces memory space waste of THP by using
DAMON for decision of promotions and demotion for huge pages, while 'prcl' is
as similar as the original work.  Those are implemented as below:

# format: <min/max size> <min/max frequency (0-100)> <min/max age> <action>
# ethp: Use huge pages if a region >2MB shows >5% access rate, use regular
# pages if a region >2MB shows <5% access rate for >1 second
2M null    5 null    null null    hugepage
2M null    null 5    1s null      nohugepage

# prcl: If a region >4KB shows <5% access rate for >5 seconds, page out.
4K null    null 5    5s null      pageout

Note that both 'ethp' and 'prcl' are designed with my only straightforward
intuition, because those are for only proof of concepts and monitoring accuracy
of DAMON.  In other words, those are not for production.  For production use,
those should be tuned more.


[1] "Redis latency problems troubleshooting", https://redis.io/topics/latency
[2] "Disable Transparent Huge Pages (THP)",
    https://docs.mongodb.com/manual/tutorial/transparent-huge-pages/
[3] "The PARSEC Becnhmark Suite", https://parsec.cs.princeton.edu/index.htm
[4] "SPLASH-2x", https://parsec.cs.princeton.edu/parsec3-doc.htm#splash2x
[5] "parsec3_on_ubuntu", https://github.com/sjp38/parsec3_on_ubuntu
[6] "[RFC v4 0/7] Implement Data Access Monitoring-based Memory Operation
    Schemes",
    https://lore.kernel.org/linux-mm/20200303121406.20954-1-sjpark@amazon.com/
[7] "Proactively reclaiming idle memory", https://lwn.net/Articles/787611/


Results
-------

Below two tables show the measurement results.  The runtimes are in seconds
while the memory usages are in KiB.  Each configurations except 'orig' shows
its overhead relative to 'orig' in percent within parenthesises.

runtime                 orig     rec      (overhead) thp      (overhead) ethp     (overhead) prcl     (overhead)
parsec3/blackscholes    107.594  107.956  (0.34)     106.750  (-0.78)    107.672  (0.07)     111.916  (4.02)    
parsec3/bodytrack       79.230   79.368   (0.17)     78.908   (-0.41)    79.705   (0.60)     80.423   (1.50)    
parsec3/canneal         142.831  143.810  (0.69)     123.530  (-13.51)   133.778  (-6.34)    144.998  (1.52)    
parsec3/dedup           11.986   11.959   (-0.23)    11.762   (-1.87)    12.028   (0.35)     13.313   (11.07)   
parsec3/facesim         210.125  209.007  (-0.53)    205.226  (-2.33)    207.766  (-1.12)    209.815  (-0.15)   
parsec3/ferret          191.601  191.177  (-0.22)    190.420  (-0.62)    191.775  (0.09)     192.638  (0.54)    
parsec3/fluidanimate    212.735  212.970  (0.11)     209.151  (-1.68)    211.904  (-0.39)    218.573  (2.74)    
parsec3/freqmine        291.225  290.873  (-0.12)    289.258  (-0.68)    289.884  (-0.46)    298.373  (2.45)    
parsec3/raytrace        118.289  119.586  (1.10)     119.045  (0.64)     119.064  (0.66)     137.919  (16.60)   
parsec3/streamcluster   323.565  328.168  (1.42)     279.565  (-13.60)   287.452  (-11.16)   333.244  (2.99)    
parsec3/swaptions       155.140  155.473  (0.21)     153.816  (-0.85)    156.423  (0.83)     156.237  (0.71)    
parsec3/vips            58.979   59.311   (0.56)     58.733   (-0.42)    59.005   (0.04)     61.062   (3.53)    
parsec3/x264            70.539   68.413   (-3.01)    64.760   (-8.19)    67.180   (-4.76)    68.103   (-3.45)   
splash2x/barnes         80.414   81.751   (1.66)     73.585   (-8.49)    80.232   (-0.23)    115.753  (43.95)   
splash2x/fft            33.902   34.111   (0.62)     24.228   (-28.53)   29.926   (-11.73)   44.438   (31.08)   
splash2x/lu_cb          85.556   86.001   (0.52)     84.538   (-1.19)    86.000   (0.52)     91.447   (6.89)    
splash2x/lu_ncb         93.399   93.652   (0.27)     90.463   (-3.14)    94.008   (0.65)     93.901   (0.54)    
splash2x/ocean_cp       45.253   45.191   (-0.14)    43.049   (-4.87)    44.022   (-2.72)    46.588   (2.95)    
splash2x/ocean_ncp      86.927   87.065   (0.16)     50.747   (-41.62)   86.855   (-0.08)    199.553  (129.57)  
splash2x/radiosity      91.433   91.511   (0.09)     90.626   (-0.88)    91.865   (0.47)     104.524  (14.32)   
splash2x/radix          31.923   32.023   (0.31)     25.194   (-21.08)   32.035   (0.35)     39.231   (22.89)   
splash2x/raytrace       84.367   84.677   (0.37)     82.417   (-2.31)    83.505   (-1.02)    84.857   (0.58)    
splash2x/volrend        87.499   87.495   (-0.00)    86.775   (-0.83)    87.311   (-0.21)    87.511   (0.01)    
splash2x/water_nsquared 236.397  236.759  (0.15)     219.902  (-6.98)    224.228  (-5.15)    238.562  (0.92)    
splash2x/water_spatial  89.646   89.767   (0.14)     89.735   (0.10)     90.347   (0.78)     103.585  (15.55)   
total                   3020.570 3028.080 (0.25)     2852.190 (-5.57)    2953.960 (-2.21)    3276.550 (8.47)    


memused.avg             orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    1785916.600  1834201.400  (2.70)     1826249.200  (2.26)     1828079.200  (2.36)     1712210.600  (-4.13)   
parsec3/bodytrack       1415049.400  1434317.600  (1.36)     1423715.000  (0.61)     1430392.600  (1.08)     1435136.000  (1.42)    
parsec3/canneal         1043489.800  1058617.600  (1.45)     1040484.600  (-0.29)    1048664.800  (0.50)     1050280.000  (0.65)    
parsec3/dedup           2414453.200  2458493.200  (1.82)     2411379.400  (-0.13)    2400516.000  (-0.58)    2461120.800  (1.93)    
parsec3/facesim         541597.200   550097.400   (1.57)     544364.600   (0.51)     553240.000   (2.15)     552316.400   (1.98)    
parsec3/ferret          317986.600   332346.000   (4.52)     320218.000   (0.70)     331085.000   (4.12)     330895.200   (4.06)    
parsec3/fluidanimate    576183.400   585442.000   (1.61)     577780.200   (0.28)     587703.400   (2.00)     506501.000   (-12.09)  
parsec3/freqmine        990869.200   997817.000   (0.70)     990350.400   (-0.05)    997669.000   (0.69)     763325.800   (-22.96)  
parsec3/raytrace        1748370.800  1757109.200  (0.50)     1746153.800  (-0.13)    1757830.400  (0.54)     1581455.800  (-9.55)   
parsec3/streamcluster   121521.800   140452.400   (15.58)    129725.400   (6.75)     132266.000   (8.84)     130558.200   (7.44)    
parsec3/swaptions       15592.400    29018.800    (86.11)    14765.800    (-5.30)    27260.200    (74.83)    26631.600    (70.80)   
parsec3/vips            2957567.600  2967993.800  (0.35)     2956623.200  (-0.03)    2973062.600  (0.52)     2951402.000  (-0.21)   
parsec3/x264            3169012.400  3175048.800  (0.19)     3190345.400  (0.67)     3189353.000  (0.64)     3172924.200  (0.12)    
splash2x/barnes         1209066.000  1213125.400  (0.34)     1217261.400  (0.68)     1209661.600  (0.05)     921041.800   (-23.82)  
splash2x/fft            9359313.200  9195213.000  (-1.75)    9377562.400  (0.19)     9050957.600  (-3.29)    9517977.000  (1.70)    
splash2x/lu_cb          514966.200   522939.400   (1.55)     520870.400   (1.15)     522635.000   (1.49)     329933.600   (-35.93)  
splash2x/lu_ncb         514180.400   525974.800   (2.29)     521420.200   (1.41)     521063.600   (1.34)     523557.000   (1.82)    
splash2x/ocean_cp       3346493.400  3288078.000  (-1.75)    3382253.800  (1.07)     3289477.600  (-1.70)    3260810.400  (-2.56)   
splash2x/ocean_ncp      3909966.400  3882968.800  (-0.69)    7037196.000  (79.98)    4046363.400  (3.49)     3471452.400  (-11.22)  
splash2x/radiosity      1471119.400  1470626.800  (-0.03)    1482604.200  (0.78)     1472718.400  (0.11)     546893.600   (-62.82)  
splash2x/radix          1748360.800  1729163.400  (-1.10)    1371463.200  (-21.56)   1701993.600  (-2.65)    1817519.600  (3.96)    
splash2x/raytrace       46670.000    60172.200    (28.93)    51901.600    (11.21)    60782.600    (30.24)    52644.800    (12.80)   
splash2x/volrend        150666.600   167444.200   (11.14)    151335.200   (0.44)     163345.000   (8.41)     162760.000   (8.03)    
splash2x/water_nsquared 45720.200    59422.400    (29.97)    46031.000    (0.68)     61801.400    (35.17)    62627.000    (36.98)   
splash2x/water_spatial  663052.200   672855.800   (1.48)     665787.600   (0.41)     674696.200   (1.76)     471052.600   (-28.96)  
total                   40077300.000 40108900.000 (0.08)     42997900.000 (7.29)     40032700.000 (-0.11)    37813000.000 (-5.65)   


DAMON Overheads
~~~~~~~~~~~~~~~

In total, DAMON recording feature incurs 0.25% runtime overhead (up to 1.66% in
worst case with 'splash2x/barnes') and 0.08% memory space overhead.

For convenience test run of 'rec', I use a Python wrapper.  The wrapper
constantly consumes about 10-15MB of memory.  This becomes high memory overhead
if the target workload has small memory footprint.  In detail, 16%, 86%, 29%,
11%, and 30% overheads shown for parsec3/streamcluster (125 MiB),
parsec3/swaptions (15 MiB), splash2x/raytrace (45 MiB), splash2x/volrend (151
MiB), and splash2x/water_nsquared (46 MiB)).  Nonetheless, the overheads are
not from DAMON, but from the wrapper, and thus should be ignored.  This fake
memory overhead continues in 'ethp' and 'prcl', as those configurations are
also using the Python wrapper.


Efficient THP
~~~~~~~~~~~~~

THP 'always' enabled policy achieves 5.57% speedup but incurs 7.29% memory
overhead.  It achieves 41.62% speedup in best case, but 79.98% memory overhead
in worst case.  Interestingly, both the best and worst case are with
'splash2x/ocean_ncp').

The 2-lines implementation of data access monitoring based THP version ('ethp')
shows 2.21% speedup and -0.11% memory overhead.  In other words, 'ethp' removes
100% of THP memory waste while preserving 39.67% of THP speedup in total.


Proactive Reclamation
~~~~~~~~~~~~~~~~~~~~

As same to the original work, I use 'zram' swap device for this configuration.

In total, our 1 line implementation of Proactive Reclamation, 'prcl', incurred
8.47% runtime overhead in total while achieving 5.65% system memory usage
reduction.

Nonetheless, as the memory usage is calculated with 'MemFree' in
'/proc/meminfo', it contains the SwapCached pages.  As the swapcached pages can
be easily evicted, I also measured the residential set size of the workloads:

rss.avg                 orig         rec          (overhead) thp          (overhead) ethp         (overhead) prcl         (overhead)
parsec3/blackscholes    592502.000   589764.400   (-0.46)    592132.600   (-0.06)    593702.000   (0.20)     406639.400   (-31.37)  
parsec3/bodytrack       32365.400    32195.000    (-0.53)    32210.800    (-0.48)    32114.600    (-0.77)    21537.600    (-33.45)  
parsec3/canneal         839904.200   840292.200   (0.05)     836866.400   (-0.36)    838263.200   (-0.20)    837895.800   (-0.24)   
parsec3/dedup           1208337.200  1218465.600  (0.84)     1233278.600  (2.06)     1200490.200  (-0.65)    882911.400   (-26.93)  
parsec3/facesim         311380.800   311363.600   (-0.01)    315642.600   (1.37)     312573.400   (0.38)     310257.400   (-0.36)   
parsec3/ferret          99514.800    99542.000    (0.03)     100454.200   (0.94)     99879.800    (0.37)     89679.200    (-9.88)   
parsec3/fluidanimate    531760.800   531735.200   (-0.00)    531865.400   (0.02)     531940.800   (0.03)     440781.000   (-17.11)  
parsec3/freqmine        552455.400   552882.600   (0.08)     555793.600   (0.60)     553019.800   (0.10)     58067.000    (-89.49)  
parsec3/raytrace        894798.400   894953.400   (0.02)     892223.400   (-0.29)    893012.400   (-0.20)    315259.800   (-64.77)  
parsec3/streamcluster   110780.400   110856.800   (0.07)     110954.000   (0.16)     111310.800   (0.48)     108066.800   (-2.45)   
parsec3/swaptions       5614.600     5645.600     (0.55)     5553.200     (-1.09)    5552.600     (-1.10)    3251.800     (-42.08)  
parsec3/vips            31942.200    31752.800    (-0.59)    32042.600    (0.31)     32226.600    (0.89)     29012.200    (-9.17)   
parsec3/x264            81770.800    81609.200    (-0.20)    82800.800    (1.26)     82612.200    (1.03)     81805.800    (0.04)    
splash2x/barnes         1216515.600  1217113.800  (0.05)     1225605.600  (0.75)     1217325.000  (0.07)     540108.400   (-55.60)  
splash2x/fft            9668660.600  9751350.800  (0.86)     9773806.400  (1.09)     9613555.400  (-0.57)    7951241.800  (-17.76)  
splash2x/lu_cb          510368.800   510095.800   (-0.05)    514350.600   (0.78)     510276.000   (-0.02)    311584.800   (-38.95)  
splash2x/lu_ncb         509904.800   510001.600   (0.02)     513847.000   (0.77)     510073.400   (0.03)     509905.600   (0.00)    
splash2x/ocean_cp       3389550.600  3404466.000  (0.44)     3443363.600  (1.59)     3410388.000  (0.61)     3330608.600  (-1.74)   
splash2x/ocean_ncp      3923723.200  3911148.200  (-0.32)    7175800.400  (82.88)    4104482.400  (4.61)     2030525.000  (-48.25)  
splash2x/radiosity      1472994.600  1475946.400  (0.20)     1485636.800  (0.86)     1476193.000  (0.22)     262161.400   (-82.20)  
splash2x/radix          1750329.800  1765697.000  (0.88)     1413304.000  (-19.25)   1754154.400  (0.22)     1516142.600  (-13.38)  
splash2x/raytrace       23149.600    23208.000    (0.25)     28574.400    (23.43)    26694.600    (15.31)    16257.800    (-29.77)  
splash2x/volrend        43968.800    43919.000    (-0.11)    44087.600    (0.27)     44224.000    (0.58)     32484.400    (-26.12)  
splash2x/water_nsquared 29348.000    29338.400    (-0.03)    29604.600    (0.87)     29779.400    (1.47)     23644.800    (-19.43)  
splash2x/water_spatial  655263.600   655097.800   (-0.03)    655199.200   (-0.01)    656282.400   (0.16)     379816.800   (-42.04)  
total                   28486900.000 28598400.000 (0.39)     31625000.000 (11.02)    28640100.000 (0.54)     20489600.000 (-28.07)  

In total, 28.07% of residential sets were reduced.

With parsec3/freqmine, 'prcl' reduced 22.96% of system memory usage and 89.49%
of residential sets while incurring only 2.45% runtime overhead.


Sequence Of Patches
===================

The patches are based on the v5.6 plus v7 DAMON patchset[1] and Minchan's
``do_madvise()`` patch[2].  Minchan's patch was necessary for reuse of
``madvise()`` code in DAMON.  You can also clone the complete git tree:

    $ git clone git://github.com/sjp38/linux -b damos/rfc/v5

The web is also available:
https://github.com/sjp38/linux/releases/tag/damos/rfc/v5


[1] https://lore.kernel.org/linux-mm/20200318112722.30143-1-sjpark@amazon.com/
[2] https://lore.kernel.org/linux-mm/20200302193630.68771-2-minchan@kernel.org/

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
 - Clean up code (Jonathan Cameron)
 - Update test results
 - Rebase on v5.6 + DAMON v7

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

runtime_avg             orig    rec     thp     ethp    prcl   
parsec3/blackscholes    107.594 107.956 106.750 107.672 111.916
parsec3/bodytrack       79.230  79.368  78.908  79.705  80.423 
parsec3/canneal         142.831 143.810 123.530 133.778 144.998
parsec3/dedup           11.986  11.959  11.762  12.028  13.313 
parsec3/facesim         210.125 209.007 205.226 207.766 209.815
parsec3/ferret          191.601 191.177 190.420 191.775 192.638
parsec3/fluidanimate    212.735 212.970 209.151 211.904 218.573
parsec3/freqmine        291.225 290.873 289.258 289.884 298.373
parsec3/raytrace        118.289 119.586 119.045 119.064 137.919
parsec3/streamcluster   323.565 328.168 279.565 287.452 333.244
parsec3/swaptions       155.140 155.473 153.816 156.423 156.237
parsec3/vips            58.979  59.311  58.733  59.005  61.062 
parsec3/x264            70.539  68.413  64.760  67.180  68.103 
splash2x/barnes         80.414  81.751  73.585  80.232  115.753
splash2x/fft            33.902  34.111  24.228  29.926  44.438 
splash2x/lu_cb          85.556  86.001  84.538  86.000  91.447 
splash2x/lu_ncb         93.399  93.652  90.463  94.008  93.901 
splash2x/ocean_cp       45.253  45.191  43.049  44.022  46.588 
splash2x/ocean_ncp      86.927  87.065  50.747  86.855  199.553
splash2x/radiosity      91.433  91.511  90.626  91.865  104.524
splash2x/radix          31.923  32.023  25.194  32.035  39.231 
splash2x/raytrace       84.367  84.677  82.417  83.505  84.857 
splash2x/volrend        87.499  87.495  86.775  87.311  87.511 
splash2x/water_nsquared 236.397 236.759 219.902 224.228 238.562
splash2x/water_spatial  89.646  89.767  89.735  90.347  103.585


memused.avg_avg         orig        rec         thp         ethp        prcl       
parsec3/blackscholes    1785916.600 1834201.400 1826249.200 1828079.200 1712210.600
parsec3/bodytrack       1415049.400 1434317.600 1423715.000 1430392.600 1435136.000
parsec3/canneal         1043489.800 1058617.600 1040484.600 1048664.800 1050280.000
parsec3/dedup           2414453.200 2458493.200 2411379.400 2400516.000 2461120.800
parsec3/facesim         541597.200  550097.400  544364.600  553240.000  552316.400 
parsec3/ferret          317986.600  332346.000  320218.000  331085.000  330895.200 
parsec3/fluidanimate    576183.400  585442.000  577780.200  587703.400  506501.000 
parsec3/freqmine        990869.200  997817.000  990350.400  997669.000  763325.800 
parsec3/raytrace        1748370.800 1757109.200 1746153.800 1757830.400 1581455.800
parsec3/streamcluster   121521.800  140452.400  129725.400  132266.000  130558.200 
parsec3/swaptions       15592.400   29018.800   14765.800   27260.200   26631.600  
parsec3/vips            2957567.600 2967993.800 2956623.200 2973062.600 2951402.000
parsec3/x264            3169012.400 3175048.800 3190345.400 3189353.000 3172924.200
splash2x/barnes         1209066.000 1213125.400 1217261.400 1209661.600 921041.800 
splash2x/fft            9359313.200 9195213.000 9377562.400 9050957.600 9517977.000
splash2x/lu_cb          514966.200  522939.400  520870.400  522635.000  329933.600 
splash2x/lu_ncb         514180.400  525974.800  521420.200  521063.600  523557.000 
splash2x/ocean_cp       3346493.400 3288078.000 3382253.800 3289477.600 3260810.400
splash2x/ocean_ncp      3909966.400 3882968.800 7037196.000 4046363.400 3471452.400
splash2x/radiosity      1471119.400 1470626.800 1482604.200 1472718.400 546893.600 
splash2x/radix          1748360.800 1729163.400 1371463.200 1701993.600 1817519.600
splash2x/raytrace       46670.000   60172.200   51901.600   60782.600   52644.800  
splash2x/volrend        150666.600  167444.200  151335.200  163345.000  162760.000 
splash2x/water_nsquared 45720.200   59422.400   46031.000   61801.400   62627.000  
splash2x/water_spatial  663052.200  672855.800  665787.600  674696.200  471052.600 


rss.avg_avg             orig        rec         thp         ethp        prcl       
parsec3/blackscholes    592502.000  589764.400  592132.600  593702.000  406639.400 
parsec3/bodytrack       32365.400   32195.000   32210.800   32114.600   21537.600  
parsec3/canneal         839904.200  840292.200  836866.400  838263.200  837895.800 
parsec3/dedup           1208337.200 1218465.600 1233278.600 1200490.200 882911.400 
parsec3/facesim         311380.800  311363.600  315642.600  312573.400  310257.400 
parsec3/ferret          99514.800   99542.000   100454.200  99879.800   89679.200  
parsec3/fluidanimate    531760.800  531735.200  531865.400  531940.800  440781.000 
parsec3/freqmine        552455.400  552882.600  555793.600  553019.800  58067.000  
parsec3/raytrace        894798.400  894953.400  892223.400  893012.400  315259.800 
parsec3/streamcluster   110780.400  110856.800  110954.000  111310.800  108066.800 
parsec3/swaptions       5614.600    5645.600    5553.200    5552.600    3251.800   
parsec3/vips            31942.200   31752.800   32042.600   32226.600   29012.200  
parsec3/x264            81770.800   81609.200   82800.800   82612.200   81805.800  
splash2x/barnes         1216515.600 1217113.800 1225605.600 1217325.000 540108.400 
splash2x/fft            9668660.600 9751350.800 9773806.400 9613555.400 7951241.800
splash2x/lu_cb          510368.800  510095.800  514350.600  510276.000  311584.800 
splash2x/lu_ncb         509904.800  510001.600  513847.000  510073.400  509905.600 
splash2x/ocean_cp       3389550.600 3404466.000 3443363.600 3410388.000 3330608.600
splash2x/ocean_ncp      3923723.200 3911148.200 7175800.400 4104482.400 2030525.000
splash2x/radiosity      1472994.600 1475946.400 1485636.800 1476193.000 262161.400 
splash2x/radix          1750329.800 1765697.000 1413304.000 1754154.400 1516142.600
splash2x/raytrace       23149.600   23208.000   28574.400   26694.600   16257.800  
splash2x/volrend        43968.800   43919.000   44087.600   44224.000   32484.400  
splash2x/water_nsquared 29348.000   29338.400   29604.600   29779.400   23644.800  
splash2x/water_spatial  655263.600  655097.800  655199.200  656282.400  379816.800 


runtime_stdev           orig  rec   thp   ethp  prcl 
parsec3/blackscholes    0.954 1.173 1.344 0.728 3.731
parsec3/bodytrack       0.723 0.463 0.465 0.686 0.266
parsec3/canneal         2.915 1.248 3.627 5.427 1.000
parsec3/dedup           0.047 0.052 0.037 0.062 0.180
parsec3/facesim         2.724 0.890 1.848 2.472 1.137
parsec3/ferret          1.818 0.552 1.288 1.397 0.826
parsec3/fluidanimate    2.157 1.082 1.695 1.456 4.954
parsec3/freqmine        5.016 2.417 2.256 2.066 2.007
parsec3/raytrace        0.246 0.601 0.825 0.522 1.462
parsec3/streamcluster   1.529 1.678 1.069 1.549 1.074
parsec3/swaptions       1.488 0.840 0.509 1.488 1.567
parsec3/vips            0.280 0.496 0.202 0.330 1.153
parsec3/x264            8.605 5.975 4.042 4.928 4.645
splash2x/barnes         0.802 0.741 0.317 0.745 6.725
splash2x/fft            0.440 0.458 0.126 3.501 5.331
splash2x/lu_cb          0.714 0.593 0.159 0.458 1.386
splash2x/lu_ncb         0.715 0.798 0.600 0.854 0.791
splash2x/ocean_cp       0.331 0.288 0.182 0.164 2.340
splash2x/ocean_ncp      0.540 0.893 0.276 3.448 2.089
splash2x/radiosity      0.715 0.665 0.511 0.625 0.986
splash2x/radix          0.240 0.285 0.202 0.135 6.516
splash2x/raytrace       0.343 0.466 0.360 0.757 0.333
splash2x/volrend        0.998 0.403 0.975 1.025 0.314
splash2x/water_nsquared 2.549 1.586 4.201 2.752 0.851
splash2x/water_spatial  0.823 0.116 0.832 0.481 1.289


memused.avg_stdev       orig       rec       thp       ethp       prcl      
parsec3/blackscholes    79952.135  4432.811  1789.970  5626.223   84879.091 
parsec3/bodytrack       3175.309   4849.375  1831.190  6191.900   3540.004  
parsec3/canneal         4619.856   2412.794  3416.723  3025.273   3987.361  
parsec3/dedup           68506.421  21577.238 45877.701 66721.358  9016.778  
parsec3/facesim         1307.756   2381.476  2262.856  1393.396   1937.146  
parsec3/ferret          3084.143   2264.331  2634.383  2864.003   2768.815  
parsec3/fluidanimate    4193.918   3755.019  709.357   1354.922   36248.397 
parsec3/freqmine        7624.951   2670.721  1056.584  1956.573   3489.113  
parsec3/raytrace        3040.764   3314.882  3858.548  1242.293   14261.074 
parsec3/streamcluster   1785.329   3278.962  12647.075 1323.469   2359.912  
parsec3/swaptions       1427.746   1651.129  2211.373  2154.608   1513.124  
parsec3/vips            5348.300   28619.077 3698.960  11801.584  31044.042 
parsec3/x264            70352.471  44934.346 35477.481 38664.730  34684.496 
splash2x/barnes         6327.141   5998.215  7956.925  3549.169   27271.518 
splash2x/fft            112761.299 40965.232 23288.890 133070.766 299831.272
splash2x/lu_cb          1959.648   1071.290  1661.184  2213.133   7278.891  
splash2x/lu_ncb         2737.766   1908.123  2225.306  1866.099   2965.527  
splash2x/ocean_cp       5714.993   3305.800  4475.152  9238.080   46523.579 
splash2x/ocean_ncp      5369.359   12261.740 47468.551 331852.556 14039.346 
splash2x/radiosity      6635.861   1847.946  2504.261  4999.619   85263.423 
splash2x/radix          26109.082  30050.707 23734.126 29847.749  97323.261 
splash2x/raytrace       1244.431   1634.057  431.736   2477.034   1469.019  
splash2x/volrend        1695.845   1276.892  2141.189  1853.123   2398.085  
splash2x/water_nsquared 4092.775   4390.012  3535.825  685.058    16884.309 
splash2x/water_spatial  3320.770   1779.806  2895.820  3684.654   31197.246 


rss.avg_stdev           orig       rec       thp       ethp       prcl      
parsec3/blackscholes    2099.400   1825.929  1783.531  141.050    129348.367
parsec3/bodytrack       84.540     101.865   91.202    152.135    230.240   
parsec3/canneal         1236.533   245.109   1079.198  1395.976   634.364   
parsec3/dedup           24015.016  33732.398 15235.190 36140.568  134488.735
parsec3/facesim         443.203    284.799   1199.896  681.779    2127.226  
parsec3/ferret          197.466    176.918   1035.756  233.280    2282.755  
parsec3/fluidanimate    79.776     48.093    83.121    296.200    43330.391 
parsec3/freqmine        942.668    701.068   1078.358  1413.236   7531.130  
parsec3/raytrace        944.799    1250.995  769.676   1469.261   18296.783 
parsec3/streamcluster   102.866    53.790    470.192   548.025    57.252    
parsec3/swaptions       75.298     98.636    79.550    62.021     413.196   
parsec3/vips            106.916    220.912   479.852   263.794    732.441   
parsec3/x264            688.836    542.557   691.062   586.514    252.573   
splash2x/barnes         1196.302   4882.971  2004.136  1382.603   73413.752 
splash2x/fft            130840.897 11731.094 34936.745 139407.193 731486.599
splash2x/lu_cb          328.545    827.521   30.618    689.734    8641.051  
splash2x/lu_ncb         466.633    375.314   8.695     413.171    789.276   
splash2x/ocean_cp       32004.956  4180.580  4846.877  11128.713  148750.827
splash2x/ocean_ncp      15405.607  20291.836 3580.843  343615.906 57957.024 
splash2x/radiosity      7370.873   1947.166  403.421   1640.537   109300.546
splash2x/radix          36493.663  26592.717 48293.202 28593.957  210257.478
splash2x/raytrace       52.861     101.382   440.840   799.744    331.045   
splash2x/volrend        88.635     287.634   58.387    219.078    28.542    
splash2x/water_nsquared 70.063     29.350    455.723   549.829    507.052   
splash2x/water_spatial  965.860    584.120   862.899   625.247    36590.592 


runtime_min             orig    rec     thp     ethp    prcl   
parsec3/blackscholes    106.493 107.131 105.819 106.780 107.954
parsec3/bodytrack       78.504  78.835  78.445  79.124  80.031 
parsec3/canneal         137.573 142.360 118.803 129.343 143.642
parsec3/dedup           11.930  11.880  11.720  11.972  13.075 
parsec3/facesim         207.181 208.001 203.330 204.972 208.496
parsec3/ferret          189.513 190.432 188.831 190.556 191.685
parsec3/fluidanimate    210.622 211.693 207.265 210.418 212.564
parsec3/freqmine        287.914 288.292 286.726 287.937 295.679
parsec3/raytrace        118.068 118.799 118.176 118.610 135.509
parsec3/streamcluster   321.708 325.773 278.506 286.412 331.865
parsec3/swaptions       153.486 154.619 153.313 154.787 154.585
parsec3/vips            58.593  58.580  58.559  58.581  59.696 
parsec3/x264            53.934  63.179  61.104  61.830  61.840 
splash2x/barnes         79.666  80.331  73.083  79.423  106.500
splash2x/fft            33.458  33.545  24.069  25.341  39.888 
splash2x/lu_cb          84.779  85.469  84.314  85.569  89.915 
splash2x/lu_ncb         92.453  92.986  89.793  93.039  92.983 
splash2x/ocean_cp       44.894  44.891  42.812  43.827  45.304 
splash2x/ocean_ncp      85.951  85.799  50.511  80.033  197.438
splash2x/radiosity      90.698  90.951  90.108  91.064  102.641
splash2x/radix          31.600  31.773  24.938  31.876  31.988 
splash2x/raytrace       83.977  84.248  81.770  82.501  84.260 
splash2x/volrend        85.998  87.010  85.410  86.104  87.028 
splash2x/water_nsquared 234.910 235.154 214.699 220.915 237.450
splash2x/water_spatial  88.707  89.583  88.665  89.728  101.680


memused.avg_min         orig        rec         thp         ethp        prcl       
parsec3/blackscholes    1626034.000 1826394.000 1823462.000 1819386.000 1609356.000
parsec3/bodytrack       1410568.000 1426374.000 1420538.000 1418411.000 1428290.000
parsec3/canneal         1037619.000 1055270.000 1036476.000 1043511.000 1043598.000
parsec3/dedup           2278984.000 2425195.000 2321960.000 2305712.000 2450959.000
parsec3/facesim         539439.000  546496.000  541226.000  551830.000  549381.000 
parsec3/ferret          313549.000  329464.000  315377.000  325668.000  327287.000 
parsec3/fluidanimate    567802.000  580600.000  576843.000  585364.000  472514.000 
parsec3/freqmine        985728.000  994180.000  988480.000  995520.000  758166.000 
parsec3/raytrace        1743676.000 1751094.000 1739729.000 1756620.000 1565002.000
parsec3/streamcluster   118300.000  134336.000  118264.000  130212.000  128156.000 
parsec3/swaptions       13349.000   26645.000   11214.000   23299.000   24083.000  
parsec3/vips            2949617.000 2913746.000 2951520.000 2962613.000 2909249.000
parsec3/x264            3028977.000 3117420.000 3155154.000 3115667.000 3116631.000
splash2x/barnes         1198278.000 1207195.000 1202163.000 1204844.000 894250.000 
splash2x/fft            9186591.000 9121129.000 9353880.000 8836885.000 9169777.000
splash2x/lu_cb          512473.000  521889.000  517808.000  519858.000  321310.000 
splash2x/lu_ncb         511032.000  523169.000  517393.000  519181.000  520978.000 
splash2x/ocean_cp       3336477.000 3284399.000 3378526.000 3279741.000 3168018.000
splash2x/ocean_ncp      3904752.000 3867722.000 6942630.000 3877713.000 3456496.000
splash2x/radiosity      1458102.000 1467236.000 1478526.000 1469471.000 478366.000 
splash2x/radix          1707691.000 1709799.000 1325458.000 1653979.000 1712997.000
splash2x/raytrace       44544.000   57325.000   51175.000   56467.000   49993.000  
splash2x/volrend        147380.000  165395.000  148418.000  160706.000  158839.000 
splash2x/water_nsquared 40565.000   50695.000   40621.000   60591.000   52073.000  
splash2x/water_spatial  658452.000  669534.000  661689.000  672001.000  429351.000 


rss.avg_min             orig        rec         thp         ethp        prcl       
parsec3/blackscholes    588688.000  588768.000  590760.000  593537.000  253301.000 
parsec3/bodytrack       32271.000   32030.000   32077.000   31813.000   21384.000  
parsec3/canneal         838622.000  839941.000  835341.000  836376.000  836723.000 
parsec3/dedup           1171348.000 1151005.000 1202905.000 1141935.000 769036.000 
parsec3/facesim         310763.000  310815.000  314095.000  311852.000  306036.000 
parsec3/ferret          99122.000   99205.000   99157.000   99626.000   86395.000  
parsec3/fluidanimate    531668.000  531676.000  531799.000  531732.000  403331.000 
parsec3/freqmine        551454.000  551564.000  554663.000  551718.000  46064.000  
parsec3/raytrace        893632.000  893410.000  891552.000  890752.000  292042.000 
parsec3/streamcluster   110692.000  110788.000  110265.000  110879.000  107960.000 
parsec3/swaptions       5553.000    5490.000    5483.000    5490.000    2448.000   
parsec3/vips            31774.000   31392.000   31642.000   31741.000   28075.000  
parsec3/x264            80396.000   80622.000   81852.000   81985.000   81338.000  
splash2x/barnes         1214960.000 1211801.000 1223343.000 1215511.000 468692.000 
splash2x/fft            9409274.000 9734749.000 9744377.000 9396273.000 6808707.000
splash2x/lu_cb          509818.000  508734.000  514300.000  508933.000  299910.000 
splash2x/lu_ncb         509198.000  509626.000  513836.000  509460.000  508424.000 
splash2x/ocean_cp       3326117.000 3398321.000 3435367.000 3397484.000 3033199.000
splash2x/ocean_ncp      3893515.000 3883868.000 7170363.000 3930558.000 1947105.000
splash2x/radiosity      1458297.000 1472266.000 1484928.000 1473122.000 174362.000 
splash2x/radix          1678377.000 1742057.000 1317705.000 1701828.000 1292568.000
splash2x/raytrace       23072.000   23100.000   28164.000   25113.000   15614.000  
splash2x/volrend        43853.000   43376.000   44038.000   43864.000   32451.000  
splash2x/water_nsquared 29280.000   29292.000   29212.000   29292.000   23105.000  
splash2x/water_spatial  654275.000  654178.000  653748.000  655154.000  329977.000 


runtime_max             orig    rec     thp     ethp    prcl   
parsec3/blackscholes    108.836 110.267 109.402 108.690 116.322
parsec3/bodytrack       80.281  80.127  79.771  80.604  80.763 
parsec3/canneal         146.168 145.781 127.499 144.079 146.620
parsec3/dedup           12.060  12.023  11.821  12.137  13.534 
parsec3/facesim         214.575 210.365 208.368 211.301 211.897
parsec3/ferret          194.344 192.015 192.115 194.198 193.653
parsec3/fluidanimate    216.783 214.909 211.344 214.586 226.676
parsec3/freqmine        301.163 295.427 293.462 293.899 301.802
parsec3/raytrace        118.739 120.519 120.106 120.069 139.163
parsec3/streamcluster   326.266 330.810 281.617 290.491 335.005
parsec3/swaptions       157.631 156.817 154.746 158.719 158.727
parsec3/vips            59.446  59.966  59.119  59.528  62.585 
parsec3/x264            78.949  77.977  72.571  73.702  73.746 
splash2x/barnes         81.961  82.439  73.990  81.263  122.656
splash2x/fft            34.651  34.855  24.361  33.808  54.323 
splash2x/lu_cb          86.882  87.071  84.763  86.854  93.114 
splash2x/lu_ncb         94.552  95.203  91.275  95.261  95.371 
splash2x/ocean_cp       45.866  45.708  43.286  44.321  51.266 
splash2x/ocean_ncp      87.486  88.455  51.209  89.471  202.513
splash2x/radiosity      92.630  92.815  91.356  92.697  105.534
splash2x/radix          32.343  32.558  25.553  32.258  46.881 
splash2x/raytrace       84.810  85.564  82.870  84.682  85.177 
splash2x/volrend        89.089  88.052  88.042  89.216  88.009 
splash2x/water_nsquared 241.486 239.589 225.392 228.507 240.056
splash2x/water_spatial  90.689  89.915  90.754  90.884  105.631


memused.avg_max         orig        rec         thp         ethp        prcl        
parsec3/blackscholes    1827611.000 1839623.000 1827981.000 1834325.000 1832457.000 
parsec3/bodytrack       1419378.000 1441113.000 1425765.000 1436107.000 1437923.000 
parsec3/canneal         1048886.000 1061549.000 1045699.000 1051540.000 1053781.000 
parsec3/dedup           2461273.000 2481847.000 2450571.000 2475857.000 2475652.000 
parsec3/facesim         543151.000  553591.000  546997.000  555622.000  554682.000  
parsec3/ferret          321345.000  334826.000  322860.000  333540.000  333881.000  
parsec3/fluidanimate    578475.000  588724.000  578768.000  589231.000  569241.000  
parsec3/freqmine        1006016.000 1002293.000 991407.000  1000760.000 767840.000  
parsec3/raytrace        1753028.000 1760656.000 1749529.000 1759651.000 1602907.000 
parsec3/streamcluster   123413.000  143752.000  146342.000  133802.000  133524.000  
parsec3/swaptions       17087.000   30836.000   17974.000   29534.000   27933.000   
parsec3/vips            2965565.000 2998231.000 2962478.000 2994743.000 2978585.000 
parsec3/x264            3217103.000 3239797.000 3254244.000 3220854.000 3213514.000 
splash2x/barnes         1215088.000 1221159.000 1225814.000 1213774.000 963246.000  
splash2x/fft            9496743.000 9235225.000 9419146.000 9227770.000 10048865.000
splash2x/lu_cb          517110.000  524777.000  522489.000  525367.000  338903.000  
splash2x/lu_ncb         517671.000  528083.000  523740.000  523987.000  529324.000  
splash2x/ocean_cp       3352652.000 3293007.000 3390730.000 3305424.000 3288700.000 
splash2x/ocean_ncp      3916516.000 3905207.000 7066994.000 4710056.000 3494469.000 
splash2x/radiosity      1476725.000 1471964.000 1485721.000 1482665.000 711743.000  
splash2x/radix          1779129.000 1789044.000 1393082.000 1730226.000 1950825.000 
splash2x/raytrace       48293.000   62020.000   52400.000   63424.000   54173.000   
splash2x/volrend        151966.000  169113.000  154117.000  165760.000  165202.000  
splash2x/water_nsquared 49559.000   62399.000   49236.000   62566.000   96087.000   
splash2x/water_spatial  667558.000  674501.000  669156.000  681962.000  519628.000  


rss.avg_max             orig        rec         thp         ethp        prcl       
parsec3/blackscholes    594058.000  593414.000  595593.000  593873.000  582550.000 
parsec3/bodytrack       32476.000   32296.000   32333.000   32226.000   21985.000  
parsec3/canneal         841973.000  840566.000  838038.000  840251.000  838550.000 
parsec3/dedup           1234781.000 1235660.000 1242284.000 1238262.000 1132789.000
parsec3/facesim         311962.000  311641.000  317545.000  313614.000  311617.000 
parsec3/ferret          99652.000   99679.000   101709.000  100216.000  93520.000  
parsec3/fluidanimate    531892.000  531804.000  532019.000  532497.000  521361.000 
parsec3/freqmine        553834.000  553611.000  557455.000  555494.000  67824.000  
parsec3/raytrace        896365.000  897219.000  893722.000  894511.000  338623.000 
parsec3/streamcluster   110974.000  110946.000  111724.000  112376.000  108123.000 
parsec3/swaptions       5760.000    5798.000    5703.000    5665.000    3534.000   
parsec3/vips            32109.000   32002.000   32980.000   32501.000   30093.000  
parsec3/x264            82184.000   82123.000   83702.000   83695.000   82039.000  
splash2x/barnes         1218190.000 1223450.000 1228802.000 1219696.000 637849.000 
splash2x/fft            9764225.000 9766120.000 9840720.000 9791957.000 8733143.000
splash2x/lu_cb          510807.000  511101.000  514377.000  510828.000  324164.000 
splash2x/lu_ncb         510422.000  510503.000  513859.000  510581.000  510579.000 
splash2x/ocean_cp       3409746.000 3410829.000 3448338.000 3427647.000 3410785.000
splash2x/ocean_ncp      3937063.000 3934331.000 7180056.000 4791695.000 2089488.000
splash2x/radiosity      1477543.000 1477505.000 1485980.000 1477653.000 472920.000 
splash2x/radix          1777745.000 1814084.000 1447943.000 1789252.000 1837798.000
splash2x/raytrace       23236.000   23380.000   29380.000   27273.000   16503.000  
splash2x/volrend        44124.000   44217.000   44194.000   44472.000   32528.000  
splash2x/water_nsquared 29476.000   29372.000   30499.000   30511.000   24440.000  
splash2x/water_spatial  657080.000  655855.000  656402.000  657002.000  427395.000 
