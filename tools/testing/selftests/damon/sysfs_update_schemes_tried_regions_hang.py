#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon

def main():
    proc = subprocess.Popen(['sleep', '2'])
    kdamonds = _damon.Kdamonds([_damon.Kdamond(
            contexts=[_damon.DamonCtx(
                ops='vaddr',
                targets=[_damon.DamonTarget(pid=proc.pid)],
                schemes=[_damon.Damos(
                    access_pattern=_damon.DamosAccessPattern(
                        nr_accesses=[200, 200]))] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err != None:
        print('kdmaond start failed: %s' % err)
        exit(1)

    while proc.poll() == None:
        err = kdamonds.kdamonds[0].update_schemes_tried_bytes()
        if err != None:
            print('tried bytes update failed: %s' % err)
            exit(1)

if __name__ == '__main__':
    main()
