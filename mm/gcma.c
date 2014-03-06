/*
 * gcma.c - guaranteed contiguous memory allocator
 *
 * gcma is a contiguous memory allocator which guarantees success and
 * maximum wait time for allocation request.
 * It secure large amount of memory and let it be allocated to the
 * contiguous memory request while it can be used as backend for
 * frontswap and cleancache concurrently.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/list.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/slab.h>

#define MAX_CMA	16

enum status {
	GCMA_FREE,
	GCMA_ALLOCED,
	GCMA_FRONTSWAP_USE,
	GCMA_CLEANCACHE_USE
};

/* stands for contiguous memory area */
struct cma {
	unsigned long		start_pfn;
	unsigned long		offset;
	unsigned long		size;
	unsigned long		flags;
	enum status		status;
	struct list_head	list;
};

static struct list_head cma_heads[MAX_CMA];
static struct cma cmas[MAX_CMA];
static unsigned cma_count;

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool enabled __read_mostly;
module_param_named(enabled, enabled, bool, 0);

/* Default size of contiguous memory area */
static unsigned long def_cma_size __read_mostly;

static int __init early_gcma(char *p)
{
	pr_debug("received %s as default cma size kern param\n", p);
	def_cma_size = memparse(p, &p);
	return 0;
}
early_param("gcma.def_cma_size", early_gcma);

/*********************************
* contig mem allocations
**********************************/

/**
 * gcma_reserve_cma() - Reserve contiguous memory area
 * @size: Size of the reserved area (in bytes), 0 for default size
 *
 * This function reserves memory from early allocator. It can be called
 * several times.
 *
 * Returns id of reserved contiguous memory area if success,
 * Returns negative number if failed
 */
int __init gcma_reserve_cma(phys_addr_t size)
{
	phys_addr_t addr;
	struct cma *cma;
	struct list_head *head;

	if (cma_count == MAX_CMA) {
		pr_err("cma reserving is limited to %d times\n", MAX_CMA);
		return -1;
	}

	if (size == 0) {
		pr_info("size is 0. use default size, %ld\n", def_cma_size);
		size = def_cma_size;
	}
	pr_info("will allocate %ld bytes of contig memory area\n",
			(unsigned long)size);

	addr = __memblock_alloc_base(size, PAGE_SIZE, 0);
	if (!addr) {
		pr_debug("failed to reserve cma\n");
		return -ENOMEM;
	}
	pr_debug("%ld size cma reserved\n", (unsigned long)size);

	cma = &cmas[cma_count];
	cma->start_pfn = PFN_DOWN(addr);
	cma->offset = 0;
	cma->size = size;
	cma->flags = 0;
	cma->status = GCMA_FREE;
	INIT_LIST_HEAD(&cma->list);

	head = &cma_heads[cma_count];
	INIT_LIST_HEAD(head);
	list_add(&cma->list, head);

	return cma_count++;
}

/**
 * gcma_alloc_from_contiguous() - allocate pages from contiguous area
 * @id:	id to specific cma
 * @count: number of pages requesting
 * @align: alignment of pages requesting
 *
 * Returns NULL if failed to allocate
 */
struct page *gcma_alloc_from_contiguous(int id, int count,
				       unsigned int align)
{
	struct cma *c, *new_free;
	struct list_head *head;

	pr_debug("%s called\n", __func__);
	if (id >= cma_count) {
		pr_err("too big id\n");
		return NULL;
	}
	head = &cma_heads[id];

	pr_debug("will iterate...\n");
	list_for_each_entry(c, head, list) {
		pr_debug("iterate cmas... offset: %ld, size: %ld, stat: %d cnt: %d\n",
				c->offset, c->size, c->status, count);
		if (c->status != GCMA_FREE || c->size < count) {
			pr_debug("not free or size is smaller than count\n");
			continue;
		}
		if (c->size == count) {
			pr_debug("just fit\n");
			c->status = GCMA_ALLOCED;
		} else {
			new_free = kmalloc(sizeof(*new_free),
					GFP_KERNEL);
			new_free->start_pfn = c->start_pfn;
			new_free->offset = c->offset + count;
			new_free->size = c->size - count;
			new_free->flags = 0;
			new_free->status = GCMA_FREE;
			INIT_LIST_HEAD(&new_free->list);

			c->size = count;
			c->status = GCMA_ALLOCED;
			list_add(&new_free->list, &c->list);
		}
		return pfn_to_page(c->start_pfn + c->offset);
	}

	pr_warn("failed to allocate\n");
	return NULL;
}

bool gcma_release_from_contiguous(int id, struct page *pages,
				 int count)
{
	return false;
}


/*********************************
* module init and exit
**********************************/
static int __init init_gcma(void)
{
	pr_debug("init_gcma\n");
	if (!enabled)
		return 0;
	pr_info("gcma loaded\n");

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed contiguous memory allocator");
