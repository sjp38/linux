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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/frontswap.h>
#include <linux/highmem.h>

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool gcma_enabled __read_mostly;
module_param_named(enabled, gcma_enabled, bool, 0);

/* Default size of contiguous memory area */
static unsigned long def_cma_bytes __read_mostly = 10000000;

static int __init early_gcma(char *p)
{
	pr_debug("set %s as default cma size\n", p);
	def_cma_bytes = memparse(p, &p);
	return 0;
}
early_param("gcma.def_cma_bytes", early_gcma);

/*********************************
* contiguous memory allocations
**********************************/
#define MAX_CMA	32

/* bitmap of contiguous memory areas.
 * 1 means alloced, 0 means free.
 */
static unsigned long *cma_bitmaps[MAX_CMA];
static unsigned long cma_pfns[MAX_CMA];
static phys_addr_t cma_sizes[MAX_CMA];
static spinlock_t cma_spinlocks[MAX_CMA];
static int nr_reserved_cma;
static atomic_t total_size = ATOMIC_INIT(0);
static atomic_t alloced_size = ATOMIC_INIT(0);

/**
 * gcma_reserve_cma() - Reserve contiguous memory area
 * @size: Size of the reserved area (in bytes), 0 for default size
 *
 * This function reserves memory from early allocator, memblock. It can be
 * called several times(under MAX_CMA) during boot for client code's usage.
 *
 * Returns id of reserved contiguous memory area if success,
 * Returns negative number if failed
 */
int __init gcma_reserve_cma(phys_addr_t size)
{
	phys_addr_t addr;

	pr_debug("%s called with %ld. will alloc %ld bytes as cma\n",
			__func__, (unsigned long)size, def_cma_bytes);
	if (nr_reserved_cma == MAX_CMA) {
		pr_warn("already %d cmas reserved. can't reserve more\n",
				MAX_CMA);
		return -1;
	}
	if (size == 0) {
		pr_debug("size is 0. use default size, %ld bytes\n",
				def_cma_bytes);
		size = def_cma_bytes;
	}

	size = ALIGN(size, PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order));
	pr_debug("aligned size is: %ld bytes\n", (unsigned long)size);

	addr = __memblock_alloc_base(size, PAGE_SIZE,
			MEMBLOCK_ALLOC_ACCESSIBLE);
	if (!addr) {
		pr_debug("failed to reserve cma\n");
		return -ENOMEM;
	}
	pr_debug("%ld bytes cma reserved\n", (unsigned long)size);

	cma_sizes[nr_reserved_cma] = size;
	cma_pfns[nr_reserved_cma] = PFN_DOWN(addr);

	atomic_add(size, &total_size);
	return nr_reserved_cma++;
}

static void cleanup_frontswap(void);

/**
 * gcma_alloc_contig() - allocate pages from contiguous area
 * @id: Identifier of contiguous memory area which the allocation be done.
 * @count: Requested number of pages.
 *
 * Returns NULL if failed to allocate,
 * Returns address of allocated contiguous memory area's start page.
 */
struct page *gcma_alloc_contig(int id, int count)
{
	unsigned long *bitmap, next_zero_area;

	pr_debug("%s called with id %d, count %d\n", __func__, id, count);
	if (id >= nr_reserved_cma) {
		pr_info("too big id for allocation\n");
		return NULL;
	}

	bitmap = cma_bitmaps[id];

	spin_lock(&cma_spinlocks[id]);
	next_zero_area = bitmap_find_next_zero_area(bitmap,
			cma_sizes[id] / PAGE_SIZE, 0, count, 1);
	if (next_zero_area > cma_sizes[id] / PAGE_SIZE) {
		pr_warn("failed to alloc pages. no such contig memory\n");
		spin_unlock(&cma_spinlocks[id]);
		return NULL;
	}

	bitmap_set(bitmap, next_zero_area, count);
	spin_unlock(&cma_spinlocks[id]);

	atomic_add(count * PAGE_SIZE, &alloced_size);
	/* Trigger frontswap cleanup if 50% of contiguous memory is full */
	if (atomic_read(&alloced_size) >= atomic_read(&total_size) / 2) {
		pr_debug("trigger frontswap cleanup\n");
		cleanup_frontswap();
	}

	return pfn_to_page(cma_pfns[id] + next_zero_area);
}
EXPORT_SYMBOL_GPL(gcma_alloc_contig);

/**
 * gcma_release_contig() - release pages from contiguous area
 * @id: Identifier of contiguous memory area which the releasing be done.
 * @count: Requested number of pages.
 *
 * Returns true if release success,
 * Returns false if release failed.
 */
bool gcma_release_contig(int id, struct page *pages, int count)
{
	unsigned long pfn, offset;
	unsigned long *bitmap;
	unsigned long next_zero_bit;

	pr_debug("%s called with id %d, count %d\n, pages %p\n",
			__func__, id, count, pages);
	if (id >= nr_reserved_cma) {
		pr_info("too big id for allocation\n");
		return NULL;
	}

	pfn = page_to_pfn(pages);
	offset = pfn - cma_pfns[id];
	pr_debug("pfn: %ld, cma start: %ld, offset: %ld\n",
			pfn, cma_pfns[id], offset);

	bitmap = cma_bitmaps[id];
	spin_lock(&cma_spinlocks[id]);
	next_zero_bit = find_next_zero_bit(bitmap, offset + count, offset);
	if (next_zero_bit < offset + count - 1) {
		pr_err("freeing free area. free page: %ld\n", next_zero_bit);
		spin_unlock(&cma_spinlocks[id]);
		return false;
	}

	bitmap_clear(bitmap, offset, count);
	spin_unlock(&cma_spinlocks[id]);
	atomic_sub(count * PAGE_SIZE, &alloced_size);
	return true;
}
EXPORT_SYMBOL_GPL(gcma_release_contig);

/**********************************
* frontswap backend
***********************************/

struct frontswap_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	unsigned int cma_index;
	struct page *page;
	int refcount;
};

struct frontswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static struct frontswap_tree *frontswap_trees[MAX_SWAPFILES];
static struct kmem_cache *frontswap_entry_cache;
static spinlock_t cleanup_lock;

/*
 * Stolen from zswap.
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST
 */
static int frontswap_rb_insert(struct rb_root *root,
		struct frontswap_entry *entry,
		struct frontswap_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct frontswap_entry *myentry;

	while (*link) {
		parent = *link;
		myentry = rb_entry(parent, struct frontswap_entry, rbnode);
		if (myentry->offset > entry->offset)
			link = &(*link)->rb_left;
		else if (myentry->offset < entry->offset)
			link = &(*link)->rb_right;
		else {
			*dupentry = myentry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, root);
	return 0;
}

/*
 * Stolen from zswap.
 */
static void frontswap_rb_erase(struct rb_root *root,
		struct frontswap_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

/*
 * Stolen from zswap.
 */
static struct frontswap_entry *frontswap_rb_search(struct rb_root *root,
		pgoff_t offset)
{
	struct rb_node *node = root->rb_node;
	struct frontswap_entry *entry;

	while (node) {
		entry = rb_entry(node, struct frontswap_entry, rbnode);
		if (entry->offset > offset)
			node = node->rb_left;
		else if (entry->offset < offset)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static void frontswap_free_entry(struct frontswap_entry *entry)
{
	gcma_release_contig(entry->cma_index, entry->page, 1);
	kmem_cache_free(frontswap_entry_cache, entry);
}

/*
 * Caller should hold frontswap tree spinlock
 */
static void frontswap_entry_get(struct frontswap_entry *entry)
{
	entry->refcount++;
}

/*
 * Stolen from zswap.
 * Caller should hold frontswap tree spinlock
 * remove from the tree and free it, if nobody reference the entry
 */
static void frontswap_entry_put(struct frontswap_tree *tree,
		struct frontswap_entry *entry)
{
	int refcount = --entry->refcount;

	BUG_ON(refcount < 0);
	if (refcount == 0) {
		frontswap_rb_erase(&tree->rbroot, entry);
		frontswap_free_entry(entry);
	}
}

/*
 * Caller should hold frontswap tree spinlock
 */
static struct frontswap_entry *frontswap_rb_find_get(struct rb_root *root,
			pgoff_t offset)
{
	struct frontswap_entry *entry = frontswap_rb_search(root, offset);
	if (entry)
		frontswap_entry_get(entry);
	return entry;
}

#define TEST_FRONTSWAP 1

#if !TEST_FRONTSWAP
static
#endif
void gcma_frontswap_init(unsigned type)
{
	struct frontswap_tree *tree;

	tree = kzalloc(sizeof(struct frontswap_tree), GFP_KERNEL);
	if (!tree) {
		pr_err("front swap tree for type %d failed\n", type);
		return;
	}
	tree->rbroot = RB_ROOT;
	spin_lock_init(&tree->lock);
	frontswap_trees[type] = tree;
	return;
}

#if !TEST_FRONTSWAP
static
#endif
int gcma_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct frontswap_entry *entry, *dupentry;
	struct page *cma_page = NULL;
	struct frontswap_tree *tree = frontswap_trees[type];
	u8 *src, *dst;
	int i, ret;

	if (!tree) {
		pr_warn("frontswap tree for type %d is not exist\n",
				type);
		return -ENODEV;
	}

	for (i = 0; i < nr_reserved_cma; i++) {
		cma_page = gcma_alloc_contig(i, 1);
		if (cma_page != NULL)
			break;
	}
	if (cma_page == NULL) {
		pr_warn("failed to get 1 page from gcma\n");
		return -ENOMEM;
	}

	entry = kmem_cache_alloc(frontswap_entry_cache, GFP_KERNEL);
	if (!entry) {
		pr_warn("failed to get frontswap entry from cache\n");
		return -ENOMEM;
	}
	entry->page = cma_page;
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);

	src = kmap_atomic(page);
	dst = kmap_atomic(cma_page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	entry->offset = offset;
	entry->cma_index = i;

	spin_lock(&tree->lock);
	do {
		ret = frontswap_rb_insert(&tree->rbroot, entry, &dupentry);
		if (ret == -EEXIST)
			frontswap_rb_erase(&tree->rbroot, dupentry);
	} while (ret == -EEXIST);
	spin_unlock(&tree->lock);

	return ret;
}

/*
 * returns 0 if success
 * returns non-zero if failed
 */
#if !TEST_FRONTSWAP
static
#endif
int gcma_frontswap_load(unsigned type, pgoff_t offset,
			       struct page *page)
{
	struct frontswap_tree *tree = frontswap_trees[type];
	struct frontswap_entry *entry;
	u8 *src, *dst;

	if (!tree) {
		pr_warn("tree for type %d not exist\n", type);
		return -1;
	}
	spin_lock(&tree->lock);
	entry = frontswap_rb_find_get(&tree->rbroot, offset);
	spin_unlock(&tree->lock);
	if (!entry) {
		pr_warn("couldn't find the page(type %d, offset %ld) from frontswap tree\n",
				type, offset);
		return -1;
	}

	src = kmap_atomic(entry->page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&tree->lock);
	frontswap_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	return 0;
}

#if !TEST_FRONTSWAP
static
#endif
void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct frontswap_tree *tree = frontswap_trees[type];
	struct frontswap_entry *entry;

	spin_lock(&tree->lock);
	entry = frontswap_rb_search(&tree->rbroot, offset);
	if (!entry) {
		pr_warn("failed to get entry\n");
		spin_unlock(&tree->lock);
		return;
	}
	frontswap_rb_erase(&tree->rbroot, entry);
	frontswap_entry_put(tree, entry);

	spin_unlock(&tree->lock);
}

#if !TEST_FRONTSWAP
static
#endif
void gcma_frontswap_invalidate_area(unsigned type)
{
	struct frontswap_tree *tree = frontswap_trees[type];
	struct frontswap_entry *entry, *n;

	if (!tree) {
		pr_warn("failed to get frontswap tree for type %d\n", type);
		return;
	}

	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(entry, n, &tree->rbroot, rbnode)
		frontswap_free_entry(entry);
	tree->rbroot = RB_ROOT;
	spin_unlock(&tree->lock);

	kfree(tree);
	frontswap_trees[type] = NULL;
}

static void cleanup_frontswap(void)
{
	int i = 0;

	spin_lock(&cleanup_lock);
	for (i = 0; i < MAX_SWAPFILES; i++) {
		if (frontswap_trees[i]) {
			pr_debug("cleanup %d type\n", i);
			gcma_frontswap_invalidate_area(i);
		}
	}
	spin_unlock(&cleanup_lock);
}

static struct frontswap_ops gcma_frontswap_ops = {
	.init = gcma_frontswap_init,
	.store = gcma_frontswap_store,
	.load = gcma_frontswap_load,
	.invalidate_page = gcma_frontswap_invalidate_page,
	.invalidate_area = gcma_frontswap_invalidate_area
};


/*********************************
* module init and exit
**********************************/
static int __init init_gcma(void)
{
	int i, bitmap_bytes;

	if (!gcma_enabled)
		return 0;
	pr_info("loading gcma\n");

	for (i = 0; i < nr_reserved_cma; i++) {
		spin_lock_init(&cma_spinlocks[i]);

		bitmap_bytes = cma_sizes[i] / PAGE_SIZE / sizeof(*cma_bitmaps);
		if ((cma_sizes[i] / PAGE_SIZE) % sizeof(*cma_bitmaps))
			bitmap_bytes += 1;
		cma_bitmaps[i] = kzalloc(bitmap_bytes, GFP_KERNEL);
		if (!cma_bitmaps[i]) {
			pr_debug("failed to alloc bitmap\n");
			return -ENOMEM;
		}
	}
	spin_lock_init(&cleanup_lock);
	frontswap_entry_cache = KMEM_CACHE(frontswap_entry, 0);
	if (frontswap_entry_cache == NULL) {
		pr_warn("failed to create frontswap cache\n");
		return -ENOMEM;
	}

	frontswap_writethrough(true);
	frontswap_register_ops(&gcma_frontswap_ops);
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed contiguous memory allocator");
