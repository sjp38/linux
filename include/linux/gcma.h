/*
 * gcma.h - Guaranteed Contiguous Memory Allocator
 *
 * gcma aims for contiguous memory allocation with success and fast
 * latency guarantee.
 * It reserve large amount of memory and let it be allocated to the
 * contiguous memory request and frontswap backend.
 *
 * Copyright (C) 2014  Minchan Kim <minchan@kernel.org>
 *                     SeongJae Park <sj38.park@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_GCMA_H
#define _LINUX_GCMA_H

struct gcma;

#ifndef CONFIG_GCMA

inline int gcma_init(unsigned long start_pfn, unsigned long size,
		     struct gcma **res_gcma)
{
	return 0;
}

inline int gcma_alloc_contig(struct gcma *gcma,
			     unsigned long start, unsigned long end)
{
	return 0;
}

void gcma_free_contig(struct gcma *gcma,
		      unsigned long pfn, unsigned long nr_pages) { }

#else

int gcma_init(unsigned long start_pfn, unsigned long size,
	      struct gcma **res_gcma);
int gcma_alloc_contig(struct gcma *gcma,
		      unsigned long start_pfn, unsigned long size);
void gcma_free_contig(struct gcma *gcma,
		      unsigned long start_pfn, unsigned long size);

#endif

#endif /* _LINUX_GCMA_H */
