#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon

def main():
    # access three 10 MiB memory regions, 1 second per each
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory', '3', '%d' % sz_region, '1000'])
    kdamonds = _damon.Kdamonds([_damon.Kdamond(
            contexts=[_damon.DamonCtx(
                ops='vaddr',
                targets=[_damon.DamonTarget(pid=proc.pid)],
                schemes=[_damon.Damos(
                    access_pattern=_damon.DamosAccessPattern(
                        nr_accesses=[1, 20]))] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err != None:
        print('kdmaond start failed: %s' % err)
        exit(1)

    wss_collected = []
    while proc.poll() == None:
        time.sleep(0.1)
        err = kdamonds.kdamonds[0].update_schemes_tried_bytes()
        if err != None:
            print('tried bytes update failed: %s' % err)
            exit(1)

        wss_collected.append(
                kdamonds.kdamonds[0].contexts[0].schemes[0].tried_bytes)

    wss_collected.sort()
    for percentile in [25, 75]:
        sample = wss_collected[int(len(wss_collected) * percentile / 100)]
        error_rate = abs(sample - sz_region) / sz_region
        print('%d-th percentile error %f' % (percentile, error_rate))
        if error_rate > 0.5:
            exit(1)

if __name__ == '__main__':
    main()
