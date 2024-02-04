#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def main():
    # access two 10 MiB memory regions, 2 second per each
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory', '2', '%d' % sz_region, '2000'])

    # Set quota up to 1 MiB per 100 ms
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[
                    _damon_sysfs.Damos(
                        access_pattern=_damon_sysfs.DamosAccessPattern(
                            # >= 25% access rate, >= 200ms age
                            nr_accesses=[5, 20], age=[2, 2**64 - 1]),
                        # aggregation interval (100 ms) is used
                        apply_interval_us=0),
                    # use 10ms apply interval
                    _damon_sysfs.Damos(
                        access_pattern=_damon_sysfs.DamosAccessPattern(
                            # >= 25% access rate, >= 200ms age
                            nr_accesses=[5, 20], age=[2, 2**64 - 1]),
                        # explicitly set 10 ms apply interval
                        apply_interval_us=10 * 1000)
                    ] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err != None:
        print('kdamond start failed: %s' % err)
        exit(1)

    wss_collected = []
    nr_quota_exceeds = 0
    while proc.poll() == None:
        time.sleep(0.1)
        err = kdamonds.kdamonds[0].update_schemes_stats()
        if err != None:
            print('stats update failed: %s' % err)
            exit(1)
    schemes = kdamonds.kdamonds[0].contexts[0].schemes
    stat_1 = schemes[0].stats
    stat_2 = schemes[1].stats
    if stat_1.nr_tried == 0 or stat_2.nr_tried == 0:
        print('schemes are not tried')
        exit(1)
    proportion = stat_2.nr_tried / stat_1.nr_tried
    if proportion < 9:
        print('apply interval is not applied as expected')
        print('%d / %d: %f' % (stat_2.nr_tried, stat_1.nr_tried, proportion))
        exit(1)
    print('%d / %d: %f' % (stat_2.nr_tried, stat_1.nr_tried, proportion))

if __name__ == '__main__':
    main()
