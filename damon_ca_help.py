#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

'''
Helps understanding cache spec
'''

import argparse

import platform

def format_nr_positive(nr, machine_friendly):
    raw_string = '%d' % nr
    if machine_friendly:
        return raw_string
    fields = []
    for i in range(0, len(raw_string), 3):
        start_idx = max(0, len(raw_string) - i - 3)
        end_idx = len(raw_string) - i
        fields = [raw_string[start_idx:end_idx]] + fields
    return ','.join(fields)

def format_nr(nr, machine_friendly):
    if nr < 0:
        return '-' + format_nr_positive(nr * -1, machine_friendly)
    return format_nr_positive(nr, machine_friendly)

def format_sz(sz_bytes, machine_friendly):
    if machine_friendly:
        return '%d' % sz_bytes

    sz_bytes = float(sz_bytes)
    if sz_bytes == float(ulong_max):
        return 'max'
    if sz_bytes > 1<<60:
        return '%.3f EiB' % (sz_bytes / (1<<60))
    if sz_bytes > 1<<50:
        return '%.3f PiB' % (sz_bytes / (1<<50))
    if sz_bytes > 1<<40:
        return '%.3f TiB' % (sz_bytes / (1<<40))
    if sz_bytes > 1<<30:
        return '%.3f GiB' % (sz_bytes / (1<<30))
    if sz_bytes > 1<<20:
        return '%.3f MiB' % (sz_bytes / (1<<20))
    if sz_bytes > 1<<10:
        return '%.3f KiB' % (sz_bytes / (1<<10))
    return '%d B' % sz_bytes

def format_addr_range(start, end, machine_friendly):
    return '[%s, %s) (%s)' % (
            format_nr(start, machine_friendly),
            format_nr(end, machine_friendly),
            format_sz(end - start, machine_friendly))

ns_ns = 1
us_ns = 1000
ms_ns = 1000 * us_ns
sec_ns = 1000 * ms_ns
minute_ns = 60 * sec_ns
hour_ns = 60 * minute_ns
day_ns = 24 * hour_ns

nsecs_to_unit = {1: 'ns', us_ns: 'us', ms_ns: 'ms', sec_ns: 's',
        minute_ns: 'm', hour_ns: 'h', day_ns: 'd'}

def format_time_ns_min_unit(time_ns, min_unit, machine_friendly):
    if machine_friendly:
        return '%d' % time_ns

    if time_ns >= ulong_max:
        return 'max'

    for unit_nsecs in sorted(nsecs_to_unit.keys(), reverse=True):
        if time_ns < unit_nsecs:
            continue
        if unit_nsecs == min_unit:
            if time_ns % unit_nsecs:
                return '%.3f %s' % (time_ns / unit_nsecs,
                        nsecs_to_unit[unit_nsecs])
            else:
                return '%d %s' % (time_ns / unit_nsecs,
                        nsecs_to_unit[unit_nsecs])

        unit_nr = int(time_ns / unit_nsecs)
        unit_str = '%s %s' % (
                format_nr(unit_nr, False), nsecs_to_unit[unit_nsecs])

        less_unit_ns = time_ns - unit_nr * unit_nsecs
        if less_unit_ns == 0:
            return unit_str
        else:
            return '%s %s' % (unit_str, format_time_ns_min_unit(less_unit_ns,
                min_unit, False))
    return '0 ns'

def format_time_ns_exact(time_ns, machine_friendly):
    return format_time_ns_min_unit(time_ns, ns_ns, machine_friendly)

def format_time_us_exact(time_us, machine_friendly):
    return format_time_ns_exact(time_us * us_ns, machine_friendly)

def format_time_ms_exact(time_ms, machine_friendly):
    return format_time_ns_exact(time_ms * ms_ns, machine_friendly)

def format_time_ns(time_ns, machine_friendly):
    if machine_friendly:
        return '%d' % time_ns

    time_ns = float(time_ns)
    if time_ns >= ulong_max:
        return 'max'

    if time_ns >= hour_ns:
        hour = int(time_ns / hour_ns)
        hour_str = '%d h' % hour

        less_hour_ns = time_ns - (hour * hour_ns)
        if less_hour_ns == 0:
            return hour_str
        return '%s %s' % (hour_str, format_time_ns(less_hour_ns, False))
    if time_ns >= minute_ns:
        if time_ns % minute_ns == 0:
            return '%d m' % (time_ns / minute_ns)
        if time_ns % sec_ns == 0:
            return '%d m %d s' % (time_ns / minute_ns,
                    (time_ns % minute_ns) / sec_ns)
        return '%d m %.3f s' % (time_ns / minute_ns,
                (time_ns % minute_ns) / sec_ns)
    if time_ns >= sec_ns:
        if time_ns % sec_ns == 0:
            return '%d s' % (time_ns / sec_ns)
        return '%.3f s' % (time_ns / sec_ns)
    if time_ns >= ms_ns:
        if time_ns % ms_ns == 0:
            return '%d ms' % (time_ns / ms_ns)
        return '%.3f ms' % (time_ns / ms_ns)
    if time_ns >= us_ns:
        if time_ns % us_ns == 0:
            return '%d us' % (time_ns / us_ns)
        return '%.3f us' % (time_ns / us_ns)
    return '%d ns' % time_ns

def format_time_us(time_us, machine_friendly):
    if machine_friendly:
        return '%d' % time_us

    return format_time_ns(time_us * 1000, machine_friendly)

def format_time_ms(time_ms, machine_friendly):
    if machine_friendly:
        return '%d' % time_ms

    return format_time_ns(time_ms * 1000000, machine_friendly)

def format_time_sec(time_sec, machine_friendly):
    if machine_friendly:
        return '%d' % time_sec

    return format_time_ns(time_sec * 1000000000, machine_friendly)

def format_ratio(ratio, machine_friendly):
    if machine_friendly:
        return '%f' % ratio

    over_percent = int(ratio * 100)
    over_percent_str = format_nr(over_percent, machine_friendly)

    under_percent = ratio * 100 % 1
    under_percent_str = ('%.7f' % under_percent).rstrip('0')
    # cut '0.' prefix
    under_percent_str = under_percent_str[2:]

    if under_percent_str == '':
        return '%s %%' % over_percent_str
    return '%s.%s %%' % (over_percent_str, under_percent_str)

def format_percent(percent, machine_friendly):
    if machine_friendly:
        return '%f' % percent

    return format_ratio(float(percent) / 100, machine_friendly)

def format_permil(permil, machine_friendly):
    if machine_friendly:
        return '%f' % permil

    return format_ratio(float(permil) / 1000, machine_friendly)

def format_bp(bp, machine_friendly):
    if machine_friendly:
        return '%f' % bp

    return format_ratio(float(bp) / 10000, machine_friendly)

def indent_lines(string, indent_width):
    return '\n'.join([' ' * indent_width + l for l in string.split('\n')])

number_types = [int, float]

try:
    # for python2
    number_types.append(long)
except:
    pass

uint_max = 2**32 - 1
ulong_max = 2**64 - 1
if platform.architecture()[0] != '64bit':
    ulong_max = 2**32 - 1

unit_to_bytes = {'B': 1,
        'K': 1 << 10, 'KB': 1 << 10, 'KiB': 1 << 10,
        'M': 1 << 20, 'MB': 1 << 20, 'MiB': 1 << 20,
        'G': 1 << 30, 'GB': 1 << 30, 'GiB': 1 << 30,
        'T': 1 << 40, 'TB': 1 << 40, 'TiB': 1 << 40,
        'P': 1 << 50, 'PB': 1 << 50, 'PiB': 1 << 50,
        'E': 1 << 60, 'EB': 1 << 60, 'EiB': 1 << 60}

def text_to_nr(txt):
    if type(txt) in number_types:
        return txt

    new_txt = ''.join([c for c in txt if c != ','])
    try:
        return int(new_txt)
    except:
        pass
    return float(new_txt)

def try_common_input(txt, min_val=0, max_val=ulong_max):
    'return success and number'
    if txt == 'min':
        return True, min_val
    if txt == 'max':
        return True, max_val
    try:
        return True, text_to_nr(txt)
    except:
        pass
    return False, None

def text_to_bytes(txt):
    success, number = try_common_input(txt)
    if success:
        return number

    unit = None
    if len(txt) > 3:
        unit = txt[len(txt) - 3:]
        if unit in unit_to_bytes:
            number = text_to_nr(txt[:-3])
        else:
            unit = None

    if unit == None:
        if len(txt) > 2:
            unit = txt[len(txt) - 2:]
            if unit in unit_to_bytes:
                number = text_to_nr(txt[:-2])
            else:
                unit = None

    if unit == None:
        if txt[-1] in unit_to_bytes:
            unit = txt[-1]
            number = text_to_nr(txt[:-1])
        else:
            unit = 'B'
            number  = text_to_nr(txt)

    return min(ulong_max, int(number * unit_to_bytes[unit]))

unit_to_nsecs = {'ns': ns_ns, 'us': us_ns, 'ms': ms_ns, 's': sec_ns,
        'm': minute_ns, 'h': hour_ns, 'd': day_ns}

def text_to_ns(txt):
    success, number = try_common_input(txt)
    if success:
        return number

    fields = txt.split()
    if len(fields) > 1:
        result_us = 0
        for i in range(0, len(fields), 2):
            result_us += text_to_ns(''.join(fields[i: i + 2]))
        return result_us

    if not txt[-2:] in unit_to_nsecs and not txt[-1] in unit_to_nsecs:
        return float(txt)

    unit = txt[-2:]
    if unit in ['ns', 'us', 'ms']:
        number = text_to_nr(txt[:-2])
    else:
        unit = txt[-1]
        number = text_to_nr(txt[:-1])
    return number * unit_to_nsecs[unit]

def text_to_us(txt):
    success, number = try_common_input(txt)
    if success:
        return number

    return text_to_ns(txt) / us_ns

def text_to_ms(txt):
    success, number = try_common_input(txt)
    if success:
        return number

    return text_to_us(txt) / 1000

def text_to_sec(txt):
    success, number = try_common_input(txt)
    if success:
        return number

    return text_to_ms(txt) / 1000

def text_to_ratio(txt):
    success, number = try_common_input(txt, 0.0)
    if success:
        return number

    is_percent = False
    if txt[-1] == '%':
        is_percent = True
        txt = txt[:-1]
    ratio = text_to_nr(txt)
    if is_percent:
        ratio /= 100.0
    return ratio

def text_to_bp(txt):
    success, number = try_common_input(txt, 0)
    if success:
        return number

    return text_to_ratio(txt) * 10000

def text_to_permil(txt):
    success, number = try_common_input(txt, 0)
    if success:
        return number

    return text_to_ratio(txt) * 1000

def text_to_percent(txt):
    success, number = try_common_input(txt, 0)
    if success:
        return number

    return text_to_ratio(txt) * 100

def text_to_nr_unit(txt):
    fields = txt.split()
    if len(fields) != 2:
        raise Exception('text_to_nr_unit requires two fields')
    return text_to_nr(fields[0]), fields[1]

def text_to_bool(txt):
    if type(txt) == bool:
        return txt

    true_txts = ['y', 'yes', 'true']
    false_txts = ['n', 'no', 'false']

    txt = txt.lower()
    if txt in true_txts:
        return True
    elif txt in false_txts:
        return False
    else:
        raise Exception('txt should be one of %s but %s' %
                (' '.join(true_txts + false_txts), txt))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('cache_size', help='size of the cache')
    parser.add_argument('cache_line_size', help='size of each cache line')
    parser.add_argument('cache_ways', help='cache associativity')
    parser.add_argument('min_paddr', help='minimum physical address')
    parser.add_argument('max_paddr', help='maximum physical address')
    parser.add_argument('--translate_addr', metavar='<physical address>',
                        help='show cache group/set that will contain the content of the address')
    parser.add_argument('--translate_group', metavar='<cache group index>',
                        help='show physical address ranges that will backup the content of the cache group')

    args = parser.parse_args()

    cache_size = text_to_bytes(args.cache_size)
    cache_line_size = text_to_bytes(args.cache_line_size)
    cache_ways = text_to_nr(args.cache_ways)
    min_paddr = text_to_bytes(args.min_paddr)
    max_paddr = text_to_bytes(args.max_paddr)

    print('cache size: %s' % format_sz(cache_size, False))
    print('cache line size: %s' % format_sz(cache_line_size, False))
    print('cache associativity ways: %s' % format_nr(cache_ways, False))

    sz_cache_set = cache_line_size * cache_ways
    print('cache set size: %s' % format_sz(sz_cache_set, False))

    nr_cache_sets = cache_size / sz_cache_set
    print('number of cache sets: %s' % format_nr(nr_cache_sets, False))

    sz_page = 4096
    print('page size: %s' % format_sz(sz_page, False))
    # group is a group of cache set that can contain contents of a page
    sets_per_group = sz_page / cache_line_size
    print('number of cache sets per group: %s' %
          format_nr(sets_per_group, False))

    nr_groups = nr_cache_sets / sets_per_group
    print('number of groups in the cache: %s' % format_nr(nr_groups, False))

    print('min physical address: %s' % format_sz(min_paddr, False))
    print('max physical address: %s' % format_sz(max_paddr, False))

    sz_avail_mem = max_paddr - min_paddr
    print('total configurable memory: %s' % format_sz(sz_avail_mem, False))
    nr_avail_pages = sz_avail_mem / sz_page
    print('total configurable pages: %s' % format_nr(nr_avail_pages, False))

    coverage_page = nr_avail_pages / nr_groups
    print('coverage of each cache group: %s' %
          format_sz(coverage_page * sz_page, False))

    coverage_per_group = sz_avail_mem / nr_groups

    if args.translate_addr is not None:
        paddr = text_to_bytes(args.translate_addr)
        cache_set_index = paddr / cache_line_size % nr_cache_sets
        cache_group_index = cache_set_index / sets_per_group
        print('content of physical address %s will be cached at' %
              format_nr(paddr, False))
        print('    %s-th cache set,'%
              format_nr(cache_set_index, False))
        print('    which is in %s-th cache group' %
              format_nr(cache_group_index, False))

    if args.translate_group is not None:
        cache_group_idx = text_to_nr(args.translate_group)
        start_addr = cache_group_idx * sz_page
        while start_addr < max_paddr:
            if start_addr > min_paddr:
                print('[%s, %s)' % (format_nr(start_addr, False),
                                    format_nr(start_addr + sz_page, False)))
            start_addr += nr_groups * sz_page

if __name__ == '__main__':
    main()
