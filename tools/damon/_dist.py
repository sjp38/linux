#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import os
import struct
import subprocess

def access_patterns(f):
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
    set ylabel 'working set size (bytes)';
    set xlabel '%s';
    plot '%s' with linespoints;""" % (terminal, output_file, xlabel, data_file)
    subprocess.call(['gnuplot', '-e', gnuplot_cmd])
    os.remove(data_file)

