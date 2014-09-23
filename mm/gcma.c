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
	spinlock_t lock;
	unsigned long *bitmap;
	unsigned long base_pfn;
	unsigned long size;
	struct list_head list;
};

struct gcma_info {
	spinlock_t lock; /* TODO: We may use RCU */
	struct list_head head;
};

static struct gcma_info ginfo = {
	.head = LIST_HEAD_INIT(ginfo.head),
	.lock = __SPIN_LOCK_UNLOCKED(ginfo.lock),
};

struct swap_slot_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	struct page *page;
	atomic_t refcount;
};

struct frontswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static LIST_HEAD(swap_lru_list);
static spinlock_t swap_lru_lock;
static struct frontswap_tree *gcma_swap_trees[MAX_SWAPFILES];
static struct kmem_cache *swap_slot_entry_cache;

static atomic_t gcma_stored_pages = ATOMIC_INIT(0);
static atomic_t gcma_loaded_pages = ATOMIC_INIT(0);
static atomic_t gcma_evicted_pages = ATOMIC_INIT(0);

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool gcma_enabled __read_mostly;
module_param_named(enabled, gcma_enabled, bool, 0);

static unsigned long evict_frontswap_pages(unsigned long nr_pages);

static void *root_of(struct page *page)
{
	return (void *)page->mapping;
}

static void set_root(struct page *page, void *root)
{
	page->mapping = (struct address_space *)root;
}

static void *entry_of(struct page *page)
{
	return (void *)page->index;
}

static void set_entry(struct page *page, void *entry)
{
	page->index = (pgoff_t)entry;
}

/* Protected by swap_lru_lock */
static void set_page_reclaim(struct page *page)
{
	page->private = 1;
}

static int page_reclaim(struct page *page)
{
	return page->private;
}

static void clear_page_reclaim(struct page *page)
{
	page->private = 0;
}
/**
 * gcma_reserve - Reserve contiguous memory area
 * @pfn: base pfn of the reserved area
 * @size: size of the reserved area
 *
 */
int gcma_reserve(unsigned long pfn, unsigned long size)
{
	struct gcma *cma;
	unsigned long cnt;

	cma = kmalloc(sizeof(*cma), GFP_KERNEL);
	if (!cma)
		goto out; /* TODO: caller should handle it */

	cma->size = size;
	cma->base_pfn = pfn;
	spin_lock_init(&cma->lock);

	cnt = cma->size / sizeof(*cma->bitmap);
	if (cma->size % sizeof(*cma->bitmap))
		cnt += 1;
	cma->bitmap = kzalloc(cnt, GFP_KERNEL);
	if (!cma->bitmap)
		goto free_cma;

	spin_lock(&ginfo.lock);
	list_add(&cma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);

	pr_info("gcma reserve [%lu, %lu]\n", pfn, pfn + size);
	return 0;

free_cma:
	kfree(cma);
out:
	return -ENOMEM;
}

static struct page *__gcma_alloc(struct gcma *cma)
{
	unsigned long bit;
	unsigned long *bitmap = cma->bitmap;
	struct page *page = NULL;

	spin_lock(&cma->lock);
	/* TODO: should be optimized */
	bit = bitmap_find_next_zero_area(bitmap, cma->size, 0, 1, 0);

	if (bit >= cma->size) {
		spin_unlock(&cma->lock);
		goto out;
	}

	bitmap_set(bitmap, bit, 1);
	spin_unlock(&cma->lock);

	page = pfn_to_page(cma->base_pfn + bit);
	clear_page_reclaim(page);
out:
	return page;
}

static struct page *gcma_alloc(void)
{
	struct page *page;
	struct gcma *cma;

retry:
	spin_lock(&ginfo.lock);
	cma = list_first_entry(&ginfo.head, struct gcma, list);
	/* Do roundrobin */
	list_move_tail(&cma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);

	/* CMA area shouldn't destroy once it is activated */
	page = __gcma_alloc(cma);
	if (page)
		goto got;

	/* Find empty slot in all cma areas */
	spin_lock(&ginfo.lock);
	list_for_each_entry(cma, &ginfo.head, list) {
		page = __gcma_alloc(cma);
		if (page)
			break;
	}
	spin_unlock(&ginfo.lock);

	if (evict_frontswap_pages(NR_EVICT_BATCH))
		goto retry;
got:
	return page;
}

static struct gcma *find_gcma(unsigned long pfn)
{
	struct gcma *cma = NULL;

	spin_lock(&ginfo.lock);
	list_for_each_entry(cma, &ginfo.head, list) {
		if (pfn >= cma->base_pfn && pfn < cma->base_pfn + cma->size)
			break;
	}
	spin_unlock(&ginfo.lock);
	return cma;
}

static void gcma_free(struct page *page)
{
	struct gcma *cma;
	unsigned long pfn, offset;
	bool reclaim = page_reclaim(page);

	pfn = page_to_pfn(page);
	cma = find_gcma(pfn);
	if (!cma) {
		WARN_ON(1);
		return;
	}

	offset = pfn - cma->base_pfn;

	spin_lock(&cma->lock);
	if (likely(!reclaim)) {
		bitmap_clear(cma->bitmap, offset, 1);
	} else {
		/* It will prevent further allocation */
		bitmap_set(cma->bitmap, offset, 1);
		clear_page_reclaim(page);
	}
	spin_unlock(&cma->lock);
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
	gcma_free(entry->page);
	kmem_cache_free(swap_slot_entry_cache, entry);
}

/*
 * Caller should hold frontswap tree spinlock
 */
static void swap_slot_entry_get(struct swap_slot_entry *entry)
{
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
		list_del(&page->lru);
		if (!locked)
			spin_unlock(&swap_lru_lock);
		frontswap_free_entry(entry);
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

		list_move(&page->lru, &free_pages);
		if (++evicted >= nr_pages)
			break;
	}
	spin_unlock(&swap_lru_lock);

	list_for_each_entry_safe(page, n, &free_pages, lru) {
		tree = (struct frontswap_tree *)root_of(page);
		entry = (struct swap_slot_entry *)entry_of(page);

		spin_lock(&tree->lock);
		/* drop refcount increased by above loop */
		swap_slot_entry_put(tree, entry, 0);
		/* free entry */
		swap_slot_entry_put(tree, entry, 0);
		spin_unlock(&tree->lock);
	}

	atomic_add(evicted, &gcma_evicted_pages);
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
		pr_err("front swap tree for type %d failed\n", type);
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
	struct page *cma_page = NULL;
	struct frontswap_tree *tree = gcma_swap_trees[type];
	u8 *src, *dst;
	int ret;

	if (!tree) {
		WARN(1, "frontswap tree for type %d is not exist\n",
				type);
		return -ENODEV;
	}
retry:
	cma_page = gcma_alloc();
	if (!cma_page)
		return -ENOMEM;

	entry = kmem_cache_alloc(swap_slot_entry_cache, GFP_NOIO);
	if (!entry) {
		gcma_free(cma_page);
		return -ENOMEM;
	}

	entry->page = cma_page;
	entry->offset = offset;
	atomic_set(&entry->refcount, 1);
	RB_CLEAR_NODE(&entry->rbnode);

	set_root(cma_page, (void *)tree);
	set_entry(cma_page, (void *)entry);

	/* copy from orig data to gcma-page */
	src = kmap_atomic(page);
	dst = kmap_atomic(cma_page);
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
	if (likely(!page_reclaim(cma_page))) {
		list_add(&cma_page->lru, &swap_lru_list);
	} else {
		/*
		 * If there is going reclaim on this page,
		 * rollback because owner(ex, device) am about to use
		 * this area soon.
		 */
		frontswap_rb_erase(&tree->rbroot, entry);
		swap_slot_entry_put(tree, entry, 1);
		spin_unlock(&swap_lru_lock);
		spin_unlock(&tree->lock);
		kfree(entry);
		goto retry;
	}
	spin_unlock(&swap_lru_lock);
	spin_unlock(&tree->lock);

	atomic_inc(&gcma_stored_pages);
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

	src = kmap_atomic(entry->page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&tree->lock);
	spin_lock(&swap_lru_lock);
	list_move(&entry->page->lru, &swap_lru_list);
	spin_unlock(&swap_lru_lock);
	swap_slot_entry_put(tree, entry, 0);
	spin_unlock(&tree->lock);

	atomic_inc(&gcma_loaded_pages);
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

void validate_bitmap(unsigned long start, unsigned long end)
{
	unsigned long offset;
	struct gcma *cma;
	unsigned long *bitmap;
	unsigned long pfn, nr;

	/* TODO : should be enhanced */
	for (pfn = start; pfn < end; pfn++) {
		cma = find_gcma(pfn);

		offset = pfn - cma->base_pfn;
		nr = pfn % sizeof(unsigned long);
		bitmap = cma->bitmap + offset / sizeof(unsigned long);

		if (!test_bit(nr, bitmap))
			BUG_ON(1);
	}
}

int alloc_contig_range_gcma(unsigned long start, unsigned long end)
{
	LIST_HEAD(free_pages);
	struct page *page, *n;
	struct gcma *cma;
	struct swap_slot_entry *entry;
	unsigned long offset, nr;
	unsigned long *bitmap;
	struct frontswap_tree *tree;
	unsigned long pfn;

	for (pfn = start; pfn < end; pfn++) {
		cma = find_gcma(pfn);
		spin_lock(&cma->lock);
		offset = pfn - cma->base_pfn;
		nr = pfn % sizeof(unsigned long);

		bitmap = cma->bitmap + offset / sizeof(unsigned long);
		if (!test_bit(nr, bitmap)) {
			/* set a bit for prevent allocation */
			bitmap_set(cma->bitmap, offset, 1);
			spin_unlock(&cma->lock);
			continue;
		}
		spin_unlock(&cma->lock);

		/* Someone is using the page so it's complicated from now on */
		page = pfn_to_page(pfn);

		spin_lock(&swap_lru_lock);
		/*
		 * If page->lru is empty, it means slot was allocated
		 * but it's not added into swap_lru_list yet so let's
		 * mark it as reclaiming so frontswap_store will be
		 * free it soon.
		 */
		if (unlikely(list_empty(&page->lru))) {
			set_page_reclaim(page);
			spin_unlock(&swap_lru_lock);
			continue;
		}

		entry = (struct swap_slot_entry *)entry_of(page);
		/*
		 * the entry could be free by other thread in the while.
		 * check whether the situation occurred and avoid others to
		 * free it by compare reference count and increase it
		 * atomically.
		 */
		if (!atomic_inc_not_zero(&entry->refcount))
			continue;

		set_page_reclaim(page);
		list_move(&page->lru, &free_pages);
		spin_unlock(&swap_lru_lock);
	}

	list_for_each_entry_safe(page, n, &free_pages, lru) {
		tree = (struct frontswap_tree *)root_of(page);
		entry = (struct swap_slot_entry *)entry_of(page);

		spin_lock(&tree->lock);
		/* drop refcount increased by above loop */
		swap_slot_entry_put(tree, entry, 0);
		/* free entry */
		swap_slot_entry_put(tree, entry, 0);
		spin_unlock(&tree->lock);
	}

	validate_bitmap(start, end);
	return 0;
}

void free_contig_range_gcma(unsigned long start, unsigned long nr_pages)
{
	unsigned long free, offset;
	struct gcma *cma;
	unsigned long pfn = start;

	while (nr_pages) {
		cma = find_gcma(pfn);
		offset = pfn - cma->base_pfn;
		free = min(nr_pages, cma->size);

		spin_lock(&cma->lock);
		bitmap_clear(cma->bitmap, offset, free);
		spin_unlock(&cma->lock);

		nr_pages -= free;
		pfn += free;
	}
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *gcma_debugfs_root;

static int __init gcma_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	gcma_debugfs_root = debugfs_create_dir("gcma", NULL);
	if (!gcma_debugfs_root)
		return -ENOMEM;

	debugfs_create_atomic_t("stored_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_stored_pages);
	debugfs_create_atomic_t("loaded_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_loaded_pages);
	debugfs_create_atomic_t("evicted_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_evicted_pages);
	return 0;
}
#else
static int __init gcma_debugfs_init(void)
{
	return 0;
}
#endif

static int __init init_gcma(void)
{
	if (!gcma_enabled)
		return 0;

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

	gcma_debugfs_init();
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
