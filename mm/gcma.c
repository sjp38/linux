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
#include <linux/highmem.h>
#include <linux/gcma.h>

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

/** gcma_init: initializes a contiguous memory area as guaranteed
 *
 * @pfn		start pfn of contiguous memory area
 * @size	size of the contiguous memory area
 * @res_gcma	pointer to store the created gcma region
 *
 * Returns 0 if activation success, negative error code if fail
 */
int gcma_init(unsigned long pfn, unsigned long size, struct gcma **res_gcma)
{
	int bitmap_size = BITS_TO_LONGS(size) * sizeof(long);
	struct gcma *gcma;

	gcma = kmalloc(sizeof(*gcma), GFP_KERNEL);
	if (!gcma)
		goto out;

	gcma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!gcma->bitmap)
		goto free_cma;

	gcma->size = size;
	gcma->base_pfn = pfn;
	spin_lock_init(&gcma->lock);

	spin_lock(&ginfo.lock);
	list_add(&gcma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);

	*res_gcma = gcma;
	pr_info("gcma activate area [%lu, %lu]\n", pfn, pfn + size);
	return 0;

free_cma:
	kfree(gcma);
out:
	return -ENOMEM;
}

/* Allocate a page from a gcma and return it */
static struct page *__alloc_reclaimable(struct gcma *gcma)
{
	unsigned long bit;
	unsigned long *bitmap = gcma->bitmap;
	struct page *page = NULL;

	spin_lock(&gcma->lock);
	/* TODO: should be optimized */
	bit = bitmap_find_next_zero_area(bitmap, gcma->size, 0, 1, 0);

	if (bit >= gcma->size) {
		spin_unlock(&gcma->lock);
		goto out;
	}

	bitmap_set(bitmap, bit, 1);
	page = pfn_to_page(gcma->base_pfn + bit);
	spin_unlock(&gcma->lock);

out:
	return page;
}

/* Allocate a page from entire gcma and return it */
__attribute__((unused))
static struct page *alloc_reclaimable(void)
{
	struct page *page;
	struct gcma *gcma;

	spin_lock(&ginfo.lock);
	gcma = list_first_entry(&ginfo.head, struct gcma, list);
	/* Do roundrobin */
	list_move_tail(&gcma->list, &ginfo.head);

	/* Find empty slot in all gcma areas */
	list_for_each_entry(gcma, &ginfo.head, list) {
		page = __alloc_reclaimable(gcma);
		if (page)
			goto got;
	}

got:
	spin_unlock(&ginfo.lock);
	return page;
}

/* Free a page back to gcma */
__attribute__((unused))
static void gcma_free(struct gcma *gcma, struct page *page)
{
	unsigned long pfn, offset;

	pfn = page_to_pfn(page);

	spin_lock(&gcma->lock);
	offset = pfn - gcma->base_pfn;

	bitmap_clear(gcma->bitmap, offset, 1);
	spin_unlock(&gcma->lock);
}

/** gcma_alloc_contig: allocates contiguous pages
 *
 * @pfn		start pfn of requiring contiguous memory area
 * @size	size of the requiring contiguous memory area
 *
 * Returns 0 if activation success, negative error code if fail
 */
int gcma_alloc_contig(struct gcma *gcma, unsigned long start, unsigned long end)
{
	unsigned long offset;
	unsigned long nr_pages;

	nr_pages = end - start;

	spin_lock(&gcma->lock);
	offset = start - gcma->base_pfn;

	if (bitmap_find_next_zero_area(gcma->bitmap, gcma->size, offset,
				nr_pages, 0) != 0) {
		spin_unlock(&gcma->lock);
		return -EINVAL;
	}

	bitmap_set(gcma->bitmap, offset, end - start);
	spin_unlock(&gcma->lock);

	return 0;
}

/** gcma_free_contig: free allocated contiguous pages
 *
 * @pfn		start pfn of requiring contiguous memory area
 * @size	size of the requiring contiguous memory area
 */
void gcma_free_contig(struct gcma *gcma,
		      unsigned long pfn, unsigned long nr_pages)
{
	unsigned long offset;

	spin_lock(&gcma->lock);
	offset = pfn - gcma->base_pfn;
	bitmap_clear(gcma->bitmap, offset, nr_pages);

	spin_unlock(&gcma->lock);
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
