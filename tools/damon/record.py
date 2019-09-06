#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Record data access patterns of the given processes.
"""

import argparse
import copy
import os
import signal
import subprocess
import time

DBGFS="/sys/kernel/debug/damon/"
DBGFS_ATTRS = DBGFS + "attrs"
DBGFS_PIDS = DBGFS + "pids"
DBGFS_TRACING_ON = DBGFS + "monitor_on"

orig_attrs = None

def set_target_pid(pid):
    return subprocess.call('echo %s > %s' % (pid, DBGFS_PIDS), shell=True,
            executable='/bin/bash')

def turn_damon(on_off):
    return subprocess.call("echo %s > %s" % (on_off, DBGFS_TRACING_ON),
            shell=True, executable="/bin/bash")

def is_damon_running():
    with open(DBGFS_TRACING_ON, 'r') as f:
        return f.read().strip() == 'on'

def do_trace(target, is_target_pid, attrs, old_attrs):
    if os.path.isfile(attrs.rfile_path):
        os.rename(attrs.rfile_path, attrs.rfile_path + '.old')

    if attrs.apply():
        print('attributes (%s) failed to be applied' % attrs)
        cleanup_exit(old_attrs, -1)
    print('# damon attrs: %s' % attrs)
    if not is_target_pid:
        p = subprocess.Popen(target, shell=True, executable='/bin/bash')
        target = p.pid
    if set_target_pid(target):
        print('pid setting (%s) failed' % target)
        cleanup_exit(old_attrs, -2)
    if turn_damon('on'):
        print('could not turn on damon' % target)
        cleanup_exit(old_attrs, -3)
    if not is_target_pid:
        p.wait()
    while True:
        # damon will turn it off by itself if the target tasks are terminated.
        if not is_damon_running():
            break
        time.sleep(1)

    cleanup_exit(old_attrs, 0)

class Attrs:
    sample_interval = None
    aggr_interval = None
    regions_update_interval = None
    min_nr_regions = None
    max_nr_regions = None
    rfile_path = None

    def __init__(self, s, a, r, n, x, f):
        self.sample_interval = s
        self.aggr_interval = a
        self.regions_update_interval = r
        self.min_nr_regions = n
        self.max_nr_regions = x
        self.rfile_path = f

    def __str__(self):
        return "%s %s %s %s %s %s" % (self.sample_interval, self.aggr_interval,
                self.regions_update_interval, self.min_nr_regions,
                self.max_nr_regions, self.rfile_path)

    def apply(self):
        return subprocess.call('echo %s > %s' % (self, DBGFS_ATTRS),
                shell=True, executable='/bin/bash')

def current_attrs():
    with open(DBGFS_ATTRS, 'r') as f:
        attrs = f.read().split()
    atnrs = [int(x) for x in attrs[0:5]]
    attrs = atnrs + [attrs[5]]
    return Attrs(*attrs)

def cmd_args_to_attrs(args):
    "Generate attributes based on current attributes and command arguments"
    a = current_attrs()
    if args.sample:
        a.sample_interval = args.sample
    if args.aggr:
        a.aggr_interval = args.aggr
    if args.updr:
        a.regions_update_interval = args.updr
    if args.minr:
        a.min_nr_regions = args.minr
    if args.maxr:
        a.max_nr_regions = args.maxr
    if args.out:
        if not os.path.isabs(args.out):
            args.out = os.path.join(os.getcwd(), args.out)
        a.rfile_path = args.out
    return a

def cleanup_exit(orig_attrs, exit_code):
    if is_damon_running():
        if turn_damon('off'):
            print('failed to turn damon off!')
    if orig_attrs:
        if orig_attrs.apply():
            print('original attributes (%s) restoration failed!' % orig_attrs)
    exit(exit_code)

def sighandler(signum, frame):
    print('\nsignal %s received' % signum)
    cleanup_exit(orig_attrs, signum)

def chk_prerequisites():
    if os.geteuid() != 0:
        print("Run as root")
        exit(1)

    if not os.path.isdir(DBGFS):
        print("damon debugfs not exists.")
        exit(1)

    if not os.path.isfile(DBGFS_PIDS):
        print("damon pids file (%s) not exists." % DBGFS_PIDS)
        exit(1)

def set_argparser(parser):
    parser.add_argument('target', type=str, metavar='<target>',
            help='the target command or the pid to record')
    parser.add_argument('-s', '--sample', metavar='<interval>', type=int,
            help='sampling interval')
    parser.add_argument('-a', '--aggr', metavar='<interval>', type=int,
            help='aggregate interval')
    parser.add_argument('-u', '--updr', metavar='<interval>', type=int,
            help='regions update interval')
    parser.add_argument('-n', '--minr', metavar='<# regions>', type=int,
            help='minimal number of regions')
    parser.add_argument('-m', '--maxr', metavar='<# regions>', type=int,
            help='maximum number of regions')
    parser.add_argument('-o', '--out', metavar='<file path>', type=str,
            default='damon.data', help='output file path')

def main(args=None):
    if not args:
        parser = argparse.ArgumentParser()
        set_argparser(parser)
        args = parser.parse_args()

    chk_prerequisites()

    signal.signal(signal.SIGINT, sighandler)
    signal.signal(signal.SIGTERM, sighandler)
    orig_attrs = current_attrs()

    new_attrs = cmd_args_to_attrs(args)
    target = args.target

    target_fields = target.split()
    if not subprocess.call('which %s > /dev/null' % target_fields[0],
            shell=True, executable='/bin/bash'):
        do_trace(target, False, new_attrs, orig_attrs)
    else:
        try:
            pid = int(target)
        except:
            print('target \'%s\' is neither a command, nor a pid' % target)
            exit(1)
        do_trace(target, True, new_attrs, orig_attrs)

if __name__ == '__main__':
    main()
