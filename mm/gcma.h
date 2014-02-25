/*
 * gcma.h
 *
 * Guaranteed contiguous memory allocator
 *
 * Copyright (C) 2014  Minchan Kim <minchan@kernel.org>,
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

#ifndef _GCMA_H_
#define _GCMA_H_

struct page *gcma_alloc(int count);
int gcma_free(struct page *page);

#endif /* _GCMA_H_ */
