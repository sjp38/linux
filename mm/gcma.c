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
#include <linux/memblock.h>
#include <linux/frontswap.h>
#include <linux/highmem.h>

/* TODO: Need to think about this magic value. Maybe knob? */
#define NR_EVICT_BATCH	32

struct gcma {
	spinlock_t lock;	/* protect bitmap */
	unsigned long *bitmap;	/* represents reclaimable pages in cma */
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

#define SWAP_SLOT_SANITY 0xdeaddead

struct swap_slot_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	struct page *page;
	atomic_t refcount;
	int sanity;
};

struct frontswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static LIST_HEAD(swap_lru_list);
static spinlock_t swap_lru_lock;
static struct frontswap_tree *gcma_swap_trees[MAX_SWAPFILES];
static struct kmem_cache *swap_slot_entry_cache;

static unsigned long evict_frontswap_pages(unsigned long nr_pages);

/* frontswap_tree */
static void *root_of(struct page *page)
{
	return (void *)page->mapping;
}

static void set_root(struct page *page, void *root)
{
	page->mapping = (struct address_space *)root;
}

/* swap_slot_entry */
static void *entry_of(struct page *page)
{
	return (void *)page->index;
}

static void set_entry(struct page *page, void *entry)
{
	page->index = (pgoff_t)entry;
}

/* protected by swap_lru_lock */
#define SWAP_LRU 0x1	/* the page is in LRU */
#define RECLAIMING 0x2	/* going on the reclaiming process */
/* protected by gcma->lock */
#define ISOLATED 0x4	/* page is isolated */

/* Protected by swap_lru_lock for SWAP_LRU and RECLAIMING, gcma->lock for
 * ISOLATED */
static int page_flag(struct page *page, int flag)
{
	return page->private & flag;
}

static void set_page_flag(struct page *page, int flag)
{
	page->private |= flag;
}

static void clear_page_flag(struct page *page, int flag)
{
	page->private &= ~flag;
}

static void clear_page_flagall(struct page *page)
{
	page->private = 0;
}

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

/* Allocate a page from a gcma and return it */
static struct page *__gcma_alloc(struct gcma *gcma)
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
	INIT_LIST_HEAD(&page->lru);
	spin_unlock(&gcma->lock);
	clear_page_flagall(page);

out:
	return page;
}

/* Allocate a page from entire gcma and return it */
static struct page *gcma_alloc(void)
{
	struct page *page;
	struct gcma *gcma;

retry:
	spin_lock(&ginfo.lock);
	gcma = list_first_entry(&ginfo.head, struct gcma, list);
	/* Do roundrobin */
	list_move_tail(&gcma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);

	/* CMA area shouldn't be destroyed once it is activated */
	page = __gcma_alloc(gcma);
	if (page)
		goto got;

	/* Find empty slot in all gcma areas */
	spin_lock(&ginfo.lock);
	list_for_each_entry(gcma, &ginfo.head, list) {
		page = __gcma_alloc(gcma);
		if (page) {
			spin_unlock(&ginfo.lock);
			goto got;
		}
	}
	spin_unlock(&ginfo.lock);

	/* Failed to alloc a page from entire gcma. Evict adequate LRU
	 * frontswap slots and try allocation again */
	if (evict_frontswap_pages(NR_EVICT_BATCH))
		goto retry;
got:
	return page;
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

/* Free a page back to gcma */
static void gcma_free(struct page *page)
{
	struct gcma *gcma;
	unsigned long pfn, offset;

	pfn = page_to_pfn(page);
	gcma = find_gcma(pfn);

	spin_lock(&gcma->lock);
	offset = pfn - gcma->base_pfn;

	if (likely(!page_flag(page, RECLAIMING))) {
		bitmap_clear(gcma->bitmap, offset, 1);
	} else {
		/* It will prevent further allocation */
		bitmap_set(gcma->bitmap, offset, 1);
		set_page_flag(page, ISOLATED);
	}
	spin_unlock(&gcma->lock);
}

/*
 * Stolen from zswap.
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST
 */
static int frontswap_rb_insert(struct rb_root *root,
		struct swap_slot_entry *entry,
		struct swap_slot_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct swap_slot_entry *myentry;

	while (*link) {
		parent = *link;
		myentry = rb_entry(parent, struct swap_slot_entry, rbnode);
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
		struct swap_slot_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

/*
 * Stolen from zswap.
 */
static struct swap_slot_entry *frontswap_rb_search(struct rb_root *root,
		pgoff_t offset)
{
	struct rb_node *node = root->rb_node;
	struct swap_slot_entry *entry;

	while (node) {
		entry = rb_entry(node, struct swap_slot_entry, rbnode);
		if (entry->offset > offset)
			node = node->rb_left;
		else if (entry->offset < offset)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static void frontswap_free_entry(struct swap_slot_entry *entry)
{
	BUG_ON(entry->sanity != SWAP_SLOT_SANITY);
	entry->sanity = 0;
	gcma_free(entry->page);
	kmem_cache_free(swap_slot_entry_cache, entry);
}

/*
 * Caller should hold frontswap tree spinlock
 */
static void swap_slot_entry_get(struct swap_slot_entry *entry)
{
	BUG_ON(entry->sanity != SWAP_SLOT_SANITY);
	atomic_inc(&entry->refcount);
}

/*
 * Stolen from zswap.
 * Caller should hold frontswap tree spinlock
 * remove from the tree and free it, if nobody reference the entry
 */
static void swap_slot_entry_put(struct frontswap_tree *tree,
		struct swap_slot_entry *entry, bool locked)
{
	int refcount = atomic_dec_return(&entry->refcount);

	BUG_ON(refcount < 0);

	if (refcount == 0) {
		struct page *page = entry->page;

		frontswap_rb_erase(&tree->rbroot, entry);
		if (!locked)
			spin_lock(&swap_lru_lock);
		if (page_flag(page, SWAP_LRU))
			list_del_init(&page->lru);

		frontswap_free_entry(entry);
		if (!locked)
			spin_unlock(&swap_lru_lock);

	}
}

static unsigned long evict_frontswap_pages(unsigned long nr_pages)
{
	struct frontswap_tree *tree;
	struct swap_slot_entry *entry;
	struct page *page, *n;
	unsigned long evicted = 0;
	LIST_HEAD(free_pages);

	spin_lock(&swap_lru_lock);
	list_for_each_entry_safe_reverse(page, n, &swap_lru_list, lru) {
		entry = (struct swap_slot_entry *)entry_of(page);

		/*
		 * the entry could be free by other thread in the while.
		 * check whether the situation occurred and avoid others to
		 * free it by compare reference count and increase it
		 * atomically.
		 */
		if (!atomic_inc_not_zero(&entry->refcount))
			continue;

		clear_page_flag(page, SWAP_LRU);
		list_move(&page->lru, &free_pages);
		if (++evicted >= nr_pages)
			break;
	}
	spin_unlock(&swap_lru_lock);

	list_for_each_entry_safe(page, n, &free_pages, lru) {
		tree = (struct frontswap_tree *)root_of(page);
		entry = (struct swap_slot_entry *)entry_of(page);

		list_del(&page->lru);
		spin_lock(&tree->lock);
		/* drop refcount increased by above loop */
		swap_slot_entry_put(tree, entry, 0);
		/* free entry if the entry is still in tree */
		if (frontswap_rb_search(&tree->rbroot, entry->offset))
			swap_slot_entry_put(tree, entry, 0);
		spin_unlock(&tree->lock);
	}

	BUG_ON(!list_empty(&free_pages));
	return evicted;
}

/*
 * Caller should hold frontswap tree spinlock
 */
static struct swap_slot_entry *frontswap_find_get(struct frontswap_tree *tree,
						pgoff_t offset)
{
	struct swap_slot_entry *entry;
	struct rb_root *root = &tree->rbroot;

	assert_spin_locked(&tree->lock);
	entry = frontswap_rb_search(root, offset);
	if (entry)
		swap_slot_entry_get(entry);

	return entry;
}

void gcma_frontswap_init(unsigned type)
{
	struct frontswap_tree *tree;

	tree = kzalloc(sizeof(struct frontswap_tree), GFP_KERNEL);
	if (!tree) {
		pr_warn("front swap tree for type %d failed to alloc\n", type);
		return;
	}

	tree->rbroot = RB_ROOT;
	spin_lock_init(&tree->lock);
	gcma_swap_trees[type] = tree;
}

int gcma_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct swap_slot_entry *entry, *dupentry;
	struct page *gcma_page = NULL;
	struct frontswap_tree *tree = gcma_swap_trees[type];
	u8 *src, *dst;
	int ret;

	if (!tree) {
		WARN(1, "frontswap tree for type %d is not exist\n",
				type);
		return -ENODEV;
	}

	gcma_page = gcma_alloc();
	if (!gcma_page)
		return -ENOMEM;

	entry = kmem_cache_alloc(swap_slot_entry_cache, GFP_NOIO);
	if (!entry) {
		spin_lock(&swap_lru_lock);
		gcma_free(gcma_page);
		spin_unlock(&swap_lru_lock);
		return -ENOMEM;
	}

	entry->page = gcma_page;
	entry->offset = offset;
	atomic_set(&entry->refcount, 1);
	entry->sanity = SWAP_SLOT_SANITY;
	RB_CLEAR_NODE(&entry->rbnode);

	set_root(gcma_page, (void *)tree);
	set_entry(gcma_page, (void *)entry);

	/* copy from orig data to gcma-page */
	src = kmap_atomic(page);
	dst = kmap_atomic(gcma_page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&tree->lock);
	do {
		/*
		 * Though this duplication scenario may happen rarely by
		 * race of swap layer, we handle this case here rather
		 * than fix swap layer because handling the possibility of
		 * duplicates is part of the tmem ABI.
		 */
		ret = frontswap_rb_insert(&tree->rbroot, entry, &dupentry);
		if (ret == -EEXIST) {
			frontswap_rb_erase(&tree->rbroot, dupentry);
			swap_slot_entry_put(tree, dupentry, 0);
		}
	} while (ret == -EEXIST);

	spin_lock(&swap_lru_lock);
	set_page_flag(gcma_page, SWAP_LRU);
	list_add(&gcma_page->lru, &swap_lru_list);
	spin_unlock(&swap_lru_lock);
	spin_unlock(&tree->lock);

	return ret;
}

/*
 * returns 0 if success
 * returns non-zero if failed
 */
int gcma_frontswap_load(unsigned type, pgoff_t offset,
			       struct page *page)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
	struct swap_slot_entry *entry;
	struct page *gcma_page;
	u8 *src, *dst;

	if (!tree) {
		WARN(1, "tree for type %d not exist\n", type);
		return -1;
	}

	spin_lock(&tree->lock);
	entry = frontswap_find_get(tree, offset);
	spin_unlock(&tree->lock);
	if (!entry)
		return -1;

	gcma_page = entry->page;
	src = kmap_atomic(gcma_page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&tree->lock);
	spin_lock(&swap_lru_lock);
	if (likely(page_flag(gcma_page, SWAP_LRU)))
		list_move(&gcma_page->lru, &swap_lru_list);
	swap_slot_entry_put(tree, entry, 1);
	spin_unlock(&swap_lru_lock);
	spin_unlock(&tree->lock);

	return 0;
}

void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
	struct swap_slot_entry *entry;

	spin_lock(&tree->lock);
	entry = frontswap_rb_search(&tree->rbroot, offset);
	if (!entry) {
		spin_unlock(&tree->lock);
		return;
	}

	swap_slot_entry_put(tree, entry, 0);
	spin_unlock(&tree->lock);
}

void gcma_frontswap_invalidate_area(unsigned type)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
	struct swap_slot_entry *entry, *n;

	if (!tree)
		return;

	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(entry, n, &tree->rbroot, rbnode) {

		/* We could optimize this frequent locking in future */
		frontswap_rb_erase(&tree->rbroot, entry);
		swap_slot_entry_put(tree, entry, 0);
	}
	tree->rbroot = RB_ROOT;
	spin_unlock(&tree->lock);

	kfree(tree);
	gcma_swap_trees[type] = NULL;
}

static struct frontswap_ops gcma_frontswap_ops = {
	.init = gcma_frontswap_init,
	.store = gcma_frontswap_store,
	.load = gcma_frontswap_load,
	.invalidate_page = gcma_frontswap_invalidate_page,
	.invalidate_area = gcma_frontswap_invalidate_area
};

/*
 * Return 0 if [start, pfn] is isolated. Otherwise, return unisolated pfn
 */
static unsigned long complete_isolation(unsigned long start, unsigned long end)
{
	unsigned long offset;
	struct gcma *gcma;
	unsigned long *bitmap;
	unsigned long pfn, ret = 0;
	struct page *page;

	gcma = find_gcma(start);
	spin_lock(&gcma->lock);

	for (pfn = start; pfn < end; pfn++) {
		int set;

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;

		set = test_bit(pfn % BITS_PER_LONG, bitmap);
		if (!set) {
			ret = pfn;
			break;
		}

		page = pfn_to_page(pfn);
		if (!page_flag(page, ISOLATED)) {
			ret = pfn;
			break;
		}

	}
	spin_unlock(&gcma->lock);
	return ret;
}

int alloc_contig_range_gcma(unsigned long start, unsigned long end)
{
	LIST_HEAD(free_pages);
	struct page *page, *n;
	struct gcma *gcma;
	struct swap_slot_entry *entry;
	unsigned long offset;
	unsigned long *bitmap;
	struct frontswap_tree *tree;
	unsigned long pfn;
	unsigned long orig_start = start;

retry:
	for (pfn = start; pfn < end; pfn++) {
		gcma = find_gcma(pfn);
		BUG_ON(!gcma);

		spin_lock(&gcma->lock);

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;
		page = pfn_to_page(pfn);

		if (!test_bit(offset % BITS_PER_LONG, bitmap)) {
			/* set a bit for prevent allocation for frontswap */
			bitmap_set(gcma->bitmap, offset, 1);
			set_page_flag(page, ISOLATED);
			spin_unlock(&gcma->lock);
			continue;
		}

		/* Someone is using the page so it's complicated :( */
		spin_unlock(&gcma->lock);
		spin_lock(&swap_lru_lock);
		/*
		 * If the page is in LRU, we can get swap_slot_entry from
		 * the page with no problem.
		 */
		if (page_flag(page, SWAP_LRU)) {

			BUG_ON(page_flag(page, RECLAIMING));

			entry = (struct swap_slot_entry *)entry_of(page);
			if (atomic_inc_not_zero(&entry->refcount)) {
				BUG_ON(entry->sanity != SWAP_SLOT_SANITY);
				clear_page_flag(page, SWAP_LRU);
				set_page_flag(page, RECLAIMING);
				list_move(&page->lru, &free_pages);
				spin_unlock(&swap_lru_lock);
				continue;
			}
		}

		/*
		 * Someone is allocating the page but it's not yet in LRU
		 * in case of frontswap_load or it was deleted from LRU
		 * but not yet from gcma's bitmap in case of
		 * frontswap_invalidate. Anycase, the race is small so retry
		 * after a while will see success. Below complete_isolation
		 * handles it.
		 */
		spin_lock(&gcma->lock);
		if (!test_bit(offset % BITS_PER_LONG, bitmap)) {
			bitmap_set(gcma->bitmap, offset, 1);
			set_page_flag(page, ISOLATED);
		} else {
			set_page_flag(page, RECLAIMING);
		}
		spin_unlock(&gcma->lock);
		spin_unlock(&swap_lru_lock);
	}

	/*
	 * Since we increase refcount of the page above, we can access
	 * swap_slot_entry with safe
	 */
	list_for_each_entry_safe(page, n, &free_pages, lru) {
		tree = (struct frontswap_tree *)root_of(page);
		entry = (struct swap_slot_entry *)entry_of(page);

		spin_lock(&tree->lock);
		list_del_init(&page->lru);
		/* drop refcount increased by above loop */
		swap_slot_entry_put(tree, entry, 0);
		/* free entry if the entry is still in tree */
		if (frontswap_rb_search(&tree->rbroot, entry->offset))
			swap_slot_entry_put(tree, entry, 0);
		spin_unlock(&tree->lock);
	}

	start = complete_isolation(orig_start, end);
	if (start)
		goto retry;

	BUG_ON(!list_empty(&free_pages));
	return 0;
}

void free_contig_range_gcma(unsigned long pfn, unsigned long nr_pages)
{
	unsigned long i;

	for (i = 0; i < nr_pages; i++)
		gcma_free(pfn_to_page(pfn + i));
}

static int __init init_gcma(void)
{
	pr_info("loading gcma\n");

	spin_lock_init(&swap_lru_lock);
	swap_slot_entry_cache = KMEM_CACHE(swap_slot_entry, 0);
	if (swap_slot_entry_cache == NULL)
		return -ENOMEM;

	/*
	 * By writethough mode, GCMA could discard all of pages in an instant
	 * instead of slow writing pages out to the swap device.
	 */
	frontswap_writethrough(true);
	frontswap_register_ops(&gcma_frontswap_ops);

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
