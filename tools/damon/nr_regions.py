#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"Print out distribution of the number of regions in the given record"

import argparse
import os
import struct
import subprocess
import sys
import tempfile

def patterns(f):
    wss = 0
    nr_regions = struct.unpack('I', f.read(4))[0]

    patterns = []
    for r in range(nr_regions):
        saddr = struct.unpack('L', f.read(8))[0]
        eaddr = struct.unpack('L', f.read(8))[0]
        nr_accesses = struct.unpack('I', f.read(4))[0]
        patterns.append([eaddr - saddr, nr_accesses])
    return patterns

def plot_dist(data_file, output_file, xlabel):
    terminal = output_file.split('.')[-1]
    if not terminal in ['pdf', 'jpeg', 'png', 'svg']:
        os.remove(data_file)
        print("Unsupported plot output type.")
        exit(-1)

    gnuplot_cmd = """
    set term %s;
    set output '%s';
    set key off;
    set ylabel 'number of sampling regions';
    set xlabel '%s';
    plot '%s' with linespoints;""" % (terminal, output_file, xlabel, data_file)
    subprocess.call(['gnuplot', '-e', gnuplot_cmd])
    os.remove(data_file)

def set_argparser(parser):
    parser.add_argument('--input', '-i', type=str, metavar='<file>',
            default='damon.data', help='input file name')
    parser.add_argument('--range', '-r', type=int, nargs=3,
            metavar=('<start>', '<stop>', '<step>'),
            help='range of wss percentiles to print')
    parser.add_argument('--sortby', '-s', choices=['time', 'size'],
            help='the metric to be used for sorting the number of regions')
    parser.add_argument('--plot', '-p', type=str, metavar='<file>',
            help='plot the distribution to an image file')

def main(args=None):
    if not args:
        parser = argparse.ArgumentParser()
        set_argparser(parser)
        args = parser.parse_args()

    percentiles = [0, 25, 50, 75, 100]

    file_path = args.input
    if args.range:
        percentiles = range(args.range[0], args.range[1], args.range[2])
    wss_sort = True
    if args.sortby == 'time':
        wss_sort = False

    pid_pattern_map = {}
    with open(file_path, 'rb') as f:
        start_time = None
        while True:
            timebin = f.read(16)
            if len(timebin) != 16:
                break
            nr_tasks = struct.unpack('I', f.read(4))[0]
            for t in range(nr_tasks):
                pid = struct.unpack('L', f.read(8))[0]
                if not pid_pattern_map:
                    pid_pattern_map[pid] = []
                pid_pattern_map[pid].append(patterns(f))

    orig_stdout = sys.stdout
    if args.plot:
        tmp_path = tempfile.mkstemp()[1]
        tmp_file = open(tmp_path, 'w')
        sys.stdout = tmp_file

    print('# <percentile> <# regions>')
    for pid in pid_pattern_map.keys():
        snapshots = pid_pattern_map[pid][20:]
        nr_regions_dist = []
        for snapshot in snapshots:
            nr_regions_dist.append(len(snapshot))
        if wss_sort:
            nr_regions_dist.sort(reverse=False)

        print('# pid\t%s' % pid)
        print('# avr:\t%d' % (sum(nr_regions_dist) / len(nr_regions_dist)))
        for percentile in percentiles:
            thres_idx = int(percentile / 100.0 * len(nr_regions_dist))
            if thres_idx == len(nr_regions_dist):
                thres_idx -= 1
            threshold = nr_regions_dist[thres_idx]
            print('%d\t%d' % (percentile, nr_regions_dist[thres_idx]))

    if args.plot:
        sys.stdout = orig_stdout
        tmp_file.flush()
        tmp_file.close()
        xlabel = 'runtime (percent)'
        if wss_sort:
            xlabel = 'percentile'
        plot_dist(tmp_path, args.plot, xlabel)

if __name__ == '__main__':
    main()
