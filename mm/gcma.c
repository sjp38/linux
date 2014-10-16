/*
 * gcma.c - Guaranteed Contiguous Memory Allocator
 *
 * GCMA aims for successful allocation within predictable time
 *
 * Copyright (C) 2014  Minchan Kim <minchan@kernel.org>
 *                     SeongJae Park <sj38.park@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>

struct gcma {
	spinlock_t lock;	/* protect bitmap */
	unsigned long *bitmap;
	unsigned long base_pfn;
	unsigned long size;
	struct list_head list;
};

struct gcma_info {
	spinlock_t lock;	/* protect list */
	struct list_head head;
};

static struct gcma_info ginfo = {
	.head = LIST_HEAD_INIT(ginfo.head),
	.lock = __SPIN_LOCK_UNLOCKED(ginfo.lock),
};

/** gcma_activate_area: activates a contiguous memory area as guaranteed
 *
 * @pfn		start pfn of contiguous memory area
 * @size	size of the contiguous memory area
 *
 * Returns 0 if activation success, negative error code if fail
 */
int gcma_activate_area(unsigned long pfn, unsigned long size)
{
	int bitmap_size = BITS_TO_LONGS(size) * sizeof(long);
	struct gcma *gcma;

	gcma = kmalloc(sizeof(*gcma), GFP_KERNEL);
	if (!gcma)
		goto out; /* TODO: caller should handle it */

	gcma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!gcma->bitmap)
		goto free_cma;

	gcma->size = size;
	gcma->base_pfn = pfn;
	spin_lock_init(&gcma->lock);

	spin_lock(&ginfo.lock);
	list_add(&gcma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);

	pr_info("gcma activate area [%lu, %lu]\n", pfn, pfn + size);
	return 0;

free_cma:
	kfree(gcma);
out:
	return -ENOMEM;
}

static struct gcma *find_gcma(unsigned long pfn)
{
	struct gcma *cma;

	/* TODO:
	 * If we maintain cma list as base_pfn ordered list,
	 * we can optimize this loop.
	 */
	spin_lock(&ginfo.lock);
	list_for_each_entry(cma, &ginfo.head, list) {
		if (pfn >= cma->base_pfn && pfn < cma->base_pfn + cma->size) {
			spin_unlock(&ginfo.lock);
			return cma;
		}
	}

	spin_unlock(&ginfo.lock);
	return NULL;
}

int alloc_contig_range_gcma(unsigned long start, unsigned long end)
{
	struct gcma *gcma;
	unsigned long offset;
	unsigned long *bitmap;
	unsigned long pfn = start;

	gcma = find_gcma(pfn);
	BUG_ON(!gcma);
	for (pfn = start; pfn < end; pfn++) {
		spin_lock(&gcma->lock);

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;

		if (!test_bit(offset % BITS_PER_LONG, bitmap)) {
			bitmap_set(gcma->bitmap, offset, 1);
			spin_unlock(&gcma->lock);
			continue;
		} else {
			return -ENOMEM;
		}
	}
	return 0;
}

void free_contig_range_gcma(unsigned long pfn, unsigned long nr_pages)
{
	struct gcma *gcma;
	unsigned long i;
	unsigned long offset;
	unsigned long *bitmap;

	gcma = find_gcma(pfn);
	BUG_ON(!gcma);
	for (i = pfn; i < pfn + nr_pages; i++) {
		spin_lock(&gcma->lock);

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;

		bitmap_clear(gcma->bitmap, offset, 1);
		spin_unlock(&gcma->lock);
	}
}

static int __init init_gcma(void)
{
	pr_info("loading gcma\n");

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
