/*
 * gcma.c - Guaranteed Contiguous Memory Allocator
 *
 * GCMA aims for contiguous memory allocation with success and fast
 * latency guarantee.
 * It reserves large amount of memory and let it be allocated to the
 * contiguous memory request and utilize as swap cache using frontswap.
 *
 * Copyright (C) 2014  LG Electronics Inc.,
 * Copyright (C) 2014  Minchan Kim <minchan@kernel.org>
 * Copyright (C) 2014  SeongJae Park <sj38.park@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cleancache.h>
#include <linux/frontswap.h>
#include <linux/highmem.h>
#include <linux/gcma.h>

/* XXX: What's the ideal? */
#define NR_EVICT_BATCH	32
#define NR_CC_EVICT_BATCH	32

#define MAX_CLEANCACHE_FS	16
#define FILEKEY_LEN		sizeof(struct cleancache_filekey)

struct gcma {
	spinlock_t lock;
	unsigned long *bitmap;
	unsigned long base_pfn, size;
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

struct swap_slot_entry {
	struct gcma *gcma;
	struct rb_node rbnode;
	pgoff_t offset;
	struct page *page;
	atomic_t refcount;
};

struct frontswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

struct inode_entry {
	struct rb_node rbnode;
	char filekey[FILEKEY_LEN];
	struct rb_root pages_root;
	atomic_t refcount;
	spinlock_t pages_lock;
};

struct page_entry {
	struct gcma *gcma;
	struct rb_node rbnode;
	pgoff_t offset;
	struct page *page;
	atomic_t refcount;
};

struct cleancache_tree {
	struct rb_root inodes_root;
	spinlock_t lock;
};

static LIST_HEAD(slru_list);	/* LRU list of swap cache */
static spinlock_t slru_lock;	/* protect slru_list */
static struct frontswap_tree *gcma_swap_trees[MAX_SWAPFILES];
static struct kmem_cache *swap_slot_entry_cache;

static LIST_HEAD(clru_list);	/* LRU list of cleancache */
static spinlock_t clru_lock;	/* protect clru_list */
static struct cleancache_tree *cleancache_trees[MAX_CLEANCACHE_FS];
static atomic_t nr_cleancache_tree = ATOMIC_INIT(0);
static struct kmem_cache *inode_entry_cache;
static struct kmem_cache *page_entry_cache;

/* For statistics */
static atomic_t gcma_stored_pages = ATOMIC_INIT(0);
static atomic_t gcma_loaded_pages = ATOMIC_INIT(0);
static atomic_t gcma_evicted_pages = ATOMIC_INIT(0);
static atomic_t gcma_reclaimed_pages = ATOMIC_INIT(0);

static atomic_t gcma_cc_init_fses = ATOMIC_INIT(0);
static atomic_t gcma_cc_put_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_put_failed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_get_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_get_failed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_failed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_inodes = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_failed_inodes = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_fses = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidate_failed_fses = ATOMIC_INIT(0);

static unsigned long evict_frontswap_pages(unsigned long nr_pages);

static struct frontswap_tree *swap_tree(struct page *page)
{
	return (struct frontswap_tree *)page->mapping;
}

static void set_swap_tree(struct page *page, struct frontswap_tree *tree)
{
	page->mapping = (struct address_space *)tree;
}

static struct swap_slot_entry *swap_slot(struct page *page)
{
	return (struct swap_slot_entry *)page->index;
}

static void set_swap_slot(struct page *page, struct swap_slot_entry *slot)
{
	page->index = (pgoff_t)slot;
}

/*
 * Flags for status of a page in gcma
 *
 * GF_SWAP_LRU
 * The page is being used for frontswap and hang on frontswap LRU list.
 * It can be drained for contiguous memory allocation anytime.
 * Protected by slru_lock.
 *
 * GF_CC_LRU
 * The page is being used for cleancache and hang on cleancache LRU list.
 * It can be drained for contiguous memory allocation anytime.
 * Protected by clru_lock.
 *
 * GF_RECLAIMING
 * The page is being draining for contiguous memory allocation.
 * Frontswap guests should not use it.
 * Protected by slru_lock.
 *
 * GF_ISOLATED
 * The page is isolated for contiguous memory allocation.
 * GCMA guests can use the page safely while frontswap guests should not.
 * Protected by gcma->lock.
 */
enum gpage_flags {
	GF_SWAP_LRU = 0x1,
	GF_CC_LRU = 0x2,
	GF_RECLAIMING = 0x4,
	GF_ISOLATED = 0x8,
};

static int gpage_flag(struct page *page, int flag)
{
	return page->private & flag;
}

static void set_gpage_flag(struct page *page, int flag)
{
	page->private |= flag;
}

static void clear_gpage_flag(struct page *page, int flag)
{
	page->private &= ~flag;
}

static void clear_gpage_flagall(struct page *page)
{
	page->private = 0;
}

/*
 * gcma_init - initializes a contiguous memory area
 *
 * @start_pfn	start pfn of contiguous memory area
 * @size	number of pages in the contiguous memory area
 * @res_gcma	pointer to store the created gcma region
 *
 * Returns 0 on success, error code on failure.
 */
int gcma_init(unsigned long start_pfn, unsigned long size,
		struct gcma **res_gcma)
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
	gcma->base_pfn = start_pfn;
	spin_lock_init(&gcma->lock);

	spin_lock(&ginfo.lock);
	list_add(&gcma->list, &ginfo.head);
	spin_unlock(&ginfo.lock);

	*res_gcma = gcma;
	pr_info("initialized gcma area [%lu, %lu]\n",
			start_pfn, start_pfn + size);
	return 0;

free_cma:
	kfree(gcma);
out:
	return -ENOMEM;
}

static struct page *gcma_alloc_page(struct gcma *gcma)
{
	unsigned long bit;
	unsigned long *bitmap = gcma->bitmap;
	struct page *page = NULL;

	spin_lock(&gcma->lock);
	bit = bitmap_find_next_zero_area(bitmap, gcma->size, 0, 1, 0);
	if (bit >= gcma->size) {
		spin_unlock(&gcma->lock);
		goto out;
	}

	bitmap_set(bitmap, bit, 1);
	page = pfn_to_page(gcma->base_pfn + bit);
	spin_unlock(&gcma->lock);
	clear_gpage_flagall(page);

out:
	return page;
}

/* Caller should hold slru_lock */
static void gcma_free_page(struct gcma *gcma, struct page *page)
{
	unsigned long pfn, offset;

	pfn = page_to_pfn(page);

	spin_lock(&gcma->lock);
	offset = pfn - gcma->base_pfn;

	if (likely(!gpage_flag(page, GF_RECLAIMING))) {
		bitmap_clear(gcma->bitmap, offset, 1);
	} else {
		/*
		 * The page should be safe to be used for a thread which
		 * reclaimed the page.
		 * To prevent further allocation from other thread,
		 * set bitmap and mark the page as isolated.
		 */
		bitmap_set(gcma->bitmap, offset, 1);
		set_gpage_flag(page, GF_ISOLATED);
	}
	spin_unlock(&gcma->lock);
}

/*
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST.
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

static void frontswap_rb_erase(struct rb_root *root,
		struct swap_slot_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

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

/* Allocates a page from gcma areas using round-robin way */
static struct page *frontswap_alloc_page(struct gcma **res_gcma)
{
	struct page *page;
	struct gcma *gcma;

retry:
	spin_lock(&ginfo.lock);
	gcma = list_first_entry(&ginfo.head, struct gcma, list);
	list_move_tail(&gcma->list, &ginfo.head);

	list_for_each_entry(gcma, &ginfo.head, list) {
		page = gcma_alloc_page(gcma);
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
	*res_gcma = gcma;
	return page;
}

static void frontswap_free_entry(struct swap_slot_entry *entry)
{
	gcma_free_page(entry->gcma, entry->page);
	kmem_cache_free(swap_slot_entry_cache, entry);
}

/* Caller should hold frontswap tree spinlock */
static void swap_slot_entry_get(struct swap_slot_entry *entry)
{
	atomic_inc(&entry->refcount);
}

/*
 * Caller should hold frontswap tree spinlock and slru_lock.
 * Remove from the tree and free it, if nobody reference the entry.
 */
static void swap_slot_entry_put(struct frontswap_tree *tree,
		struct swap_slot_entry *entry)
{
	int refcount = atomic_dec_return(&entry->refcount);

	BUG_ON(refcount < 0);

	if (refcount == 0) {
		struct page *page = entry->page;

		frontswap_rb_erase(&tree->rbroot, entry);
		list_del(&page->lru);

		frontswap_free_entry(entry);
	}
}

/*
 * evict_frontswap_pages - evict @nr_pages LRU frontswap backed pages
 *
 * @nr_pages	number of LRU pages to be evicted
 *
 * Returns number of successfully evicted pages
 */
static unsigned long evict_frontswap_pages(unsigned long nr_pages)
{
	struct frontswap_tree *tree;
	struct swap_slot_entry *entry;
	struct page *page, *n;
	unsigned long evicted = 0;
	pgoff_t slot_offset;
	LIST_HEAD(free_pages);

	spin_lock(&slru_lock);
	list_for_each_entry_safe_reverse(page, n, &slru_list, lru) {
		entry = swap_slot(page);

		/*
		 * the entry could be free by other thread in the while.
		 * check whether the situation occurred and avoid others to
		 * free it by compare reference count and increase it
		 * atomically.
		 */
		if (!atomic_inc_not_zero(&entry->refcount))
			continue;

		clear_gpage_flag(page, GF_SWAP_LRU);
		list_move(&page->lru, &free_pages);
		if (++evicted >= nr_pages)
			break;
	}
	spin_unlock(&slru_lock);

	list_for_each_entry_safe(page, n, &free_pages, lru) {
		tree = swap_tree(page);
		entry = swap_slot(page);

		spin_lock(&tree->lock);
		spin_lock(&slru_lock);
		/* drop refcount increased by above loop */
		slot_offset = entry->offset;
		swap_slot_entry_put(tree, entry);
		/* free entry if the entry is still in tree */
		if (frontswap_rb_search(&tree->rbroot, slot_offset))
			swap_slot_entry_put(tree, entry);
		spin_unlock(&slru_lock);
		spin_unlock(&tree->lock);
	}

	atomic_add(evicted, &gcma_evicted_pages);
	return evicted;
}

/* Caller should hold frontswap tree spinlock */
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
	struct gcma *gcma;
	struct page *gcma_page = NULL;
	struct frontswap_tree *tree = gcma_swap_trees[type];
	u8 *src, *dst;
	int ret;

	if (!tree) {
		WARN(1, "frontswap tree for type %d is not exist\n",
				type);
		return -ENODEV;
	}

	gcma_page = frontswap_alloc_page(&gcma);
	if (!gcma_page)
		return -ENOMEM;

	entry = kmem_cache_alloc(swap_slot_entry_cache, GFP_NOIO);
	if (!entry) {
		spin_lock(&slru_lock);
		gcma_free_page(gcma, gcma_page);
		spin_unlock(&slru_lock);
		return -ENOMEM;
	}

	entry->gcma = gcma;
	entry->page = gcma_page;
	entry->offset = offset;
	atomic_set(&entry->refcount, 1);
	RB_CLEAR_NODE(&entry->rbnode);

	set_swap_tree(gcma_page, tree);
	set_swap_slot(gcma_page, entry);

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
			spin_lock(&slru_lock);
			swap_slot_entry_put(tree, dupentry);
			spin_unlock(&slru_lock);
		}
	} while (ret == -EEXIST);

	spin_lock(&slru_lock);
	set_gpage_flag(gcma_page, GF_SWAP_LRU);
	list_add(&gcma_page->lru, &slru_list);
	spin_unlock(&slru_lock);
	spin_unlock(&tree->lock);

	atomic_inc(&gcma_stored_pages);
	return ret;
}

/*
 * Returns 0 if success,
 * Returns non-zero if failed.
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
	spin_lock(&slru_lock);
	if (likely(gpage_flag(gcma_page, GF_SWAP_LRU)))
		list_move(&gcma_page->lru, &slru_list);
	swap_slot_entry_put(tree, entry);
	spin_unlock(&slru_lock);
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

	spin_lock(&slru_lock);
	swap_slot_entry_put(tree, entry);
	spin_unlock(&slru_lock);
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
		frontswap_rb_erase(&tree->rbroot, entry);
		spin_lock(&slru_lock);
		swap_slot_entry_put(tree, entry);
		spin_unlock(&slru_lock);
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

static unsigned long evict_cleancache_pages(unsigned long nr_pages);

static int compare_filekey(void *l, void *r)
{
	return memcmp(l, r, FILEKEY_LEN);
}

static int compare_offset(pgoff_t l, pgoff_t r)
{
	return l - r;
}

static struct inode_entry *inode_entry(struct page *page)
{
	return (struct inode_entry *)page->mapping;
}

static void set_inode_entry(struct page *page, struct inode_entry *ientry)
{
	page->mapping = (struct address_space *)ientry;
}

static struct page_entry *page_entry(struct page *page)
{
	return (struct page_entry *)page->index;
}

static void set_page_entry(struct page *page, struct page_entry *pentry)
{
	page->index = (pgoff_t)pentry;
}

static int cc_inode_rb_insert(struct rb_root *root,
				struct inode_entry *entry,
				struct inode_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct inode_entry *ientry;
	int compared;

	while (*link) {
		parent = *link;
		ientry = rb_entry(parent, struct inode_entry, rbnode);
		compared = compare_filekey(entry->filekey, ientry->filekey);
		if (compared < 0)
			link = &(*link)->rb_left;
		else if (compared > 0)
			link = &(*link)->rb_right;
		else {
			*dupentry = ientry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, root);
	return 0;
}

static void cc_inode_rb_erase(struct rb_root *root, struct inode_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static struct inode_entry *cc_inode_rb_search(struct rb_root *root,
						void *filekey)
{
	struct rb_node *node = root->rb_node;
	struct inode_entry *entry;
	int compared;

	while (node) {
		entry = rb_entry(node, struct inode_entry, rbnode);
		compared = compare_filekey(filekey, entry->filekey);
		if (compared < 0)
			node = node->rb_left;
		else if (compared > 0)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static int cc_page_rb_insert(struct rb_root *root,
				struct page_entry *entry,
				struct page_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct page_entry *pentry;
	int compared;

	while (*link) {
		parent = *link;
		pentry = rb_entry(parent, struct page_entry, rbnode);
		compared = compare_offset(entry->offset, pentry->offset);
		if (compared < 0)
			link = &(*link)->rb_left;
		else if (compared > 0)
			link = &(*link)->rb_right;
		else {
			*dupentry = pentry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, root);
	return 0;
}

static void cc_page_rb_erase(struct rb_root *root, struct page_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static struct page_entry *cc_page_rb_search(struct rb_root *root,
						pgoff_t offset)
{
	struct rb_node *node = root->rb_node;
	struct page_entry *entry;
	int compared;

	while (node) {
		entry = rb_entry(node, struct page_entry, rbnode);
		compared = compare_offset(offset, entry->offset);
		if (compared < 0)
			node = node->rb_left;
		else if (compared > 0)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static void free_inode_entry(struct inode_entry *entry)
{
	kmem_cache_free(inode_entry_cache, entry);
}

/* Allocates a page from gcma areas using round-robin way */
static struct page *cleancache_alloc_page(struct gcma **res_gcma)
{
	struct page *page;
	struct gcma *gcma;

retry:
	spin_lock(&ginfo.lock);
	gcma = list_first_entry(&ginfo.head, struct gcma, list);
	list_move_tail(&gcma->list, &ginfo.head);

	list_for_each_entry(gcma, &ginfo.head, list) {
		page = gcma_alloc_page(gcma);
		if (page) {
			spin_unlock(&ginfo.lock);
			goto got;
		}
	}
	spin_unlock(&ginfo.lock);

	/* Failed to alloc a page from entire gcma. Evict adequate LRU
	 * frontswap slots and try allocation again */
	if (evict_cleancache_pages(NR_CC_EVICT_BATCH))
		goto retry;

got:
	*res_gcma = gcma;
	return page;
}

static void free_page_entry(struct page_entry *entry)
{
	if (entry->page != NULL)
		gcma_free_page(entry->gcma, entry->page);
	kmem_cache_free(page_entry_cache, entry);
}

static void get_inode_entry(struct inode_entry *entry)
{
	atomic_inc(&entry->refcount);
}

static void get_page_entry(struct page_entry *entry)
{
	atomic_inc(&entry->refcount);
}

static void put_inode_entry(struct rb_root *root,
				struct inode_entry *entry)
{
	int refcount = atomic_dec_return(&entry->refcount);

	BUG_ON(refcount < 0);

	if (refcount == 0) {
		cc_inode_rb_erase(root, entry);
		free_inode_entry(entry);
	}
}

static void put_page_entry(struct rb_root *root,
				struct page_entry *entry)
{
	int refcount = atomic_dec_return(&entry->refcount);

	BUG_ON(refcount < 0);

	if (refcount == 0) {
		struct page *page = entry->page;

		cc_page_rb_erase(root, entry);
		list_del(&page->lru);

		free_page_entry(entry);
	}
}

static struct inode_entry *find_get_inode_entry(struct rb_root *root,
						void *filekey)
{
	struct inode_entry *entry = cc_inode_rb_search(root, filekey);
	if (entry)
		get_inode_entry(entry);
	return entry;
}

static struct page_entry *find_get_page_entry(struct rb_root *root,
						pgoff_t offset)
{
	struct page_entry *entry = cc_page_rb_search(root, offset);
	if (entry)
		get_page_entry(entry);
	return entry;
}

/*
 * evict_cleancache_pages - evict @nr_pages LRU cleancache backed pages
 *
 * @nr_pages	number of LRU pages to be evicted
 *
 * Returns number of successfully evicted pages
 */
static unsigned long evict_cleancache_pages(unsigned long nr_pages)
{
	struct inode_entry *ientry;
	struct page_entry *pentry;
	struct page *page, *n;
	unsigned long evicted = 0;
	pgoff_t page_offset;
	LIST_HEAD(free_pages);

	spin_lock(&clru_lock);
	list_for_each_entry_safe_reverse(page, n, &clru_list, lru) {
		pentry = page_entry(page);

		/*
		 * the entry could be free by other thread in the while.
		 * check whether the situation occurred and avoid others to
		 * free it by compare reference count and increase it
		 * atomically.
		 */
		if (!atomic_inc_not_zero(&pentry->refcount))
			continue;

		clear_gpage_flag(page, GF_CC_LRU);
		list_move(&page->lru, &free_pages);
		if (++evicted >= nr_pages)
			break;
	}
	spin_unlock(&clru_lock);

	list_for_each_entry_safe(page, n, &free_pages, lru) {
		ientry = inode_entry(page);
		pentry = page_entry(page);

		spin_lock(&ientry->pages_lock);
		spin_lock(&clru_lock);
		/* drop refcount increased by above loop */
		page_offset = pentry->offset;
		put_page_entry(&ientry->pages_root, pentry);
		/* free pentry if the pentry is still in inode entry */
		if (cc_page_rb_search(&ientry->pages_root, page_offset))
			put_page_entry(&ientry->pages_root, pentry);
		spin_unlock(&clru_lock);
		spin_unlock(&ientry->pages_lock);
	}

	atomic_add(evicted, &gcma_evicted_pages);
	return evicted;
}

static struct inode_entry *create_insert_get_inode_entry(struct rb_root *root,
							void *filekey, struct inode_entry *entry)
{
	struct inode_entry *dupentry;

	entry->pages_root = RB_ROOT;
	memcpy(entry->filekey, filekey, FILEKEY_LEN);
	atomic_set(&entry->refcount, 1);
	RB_CLEAR_NODE(&entry->rbnode);
	spin_lock_init(&entry->pages_lock);

	BUG_ON(cc_inode_rb_insert(root, entry, &dupentry) == -EEXIST);
	get_inode_entry(entry);
	return entry;
}

static struct page_entry *create_page_entry(pgoff_t offset,
						struct page *src_page)
{
	struct page_entry *entry;
	u8 *src, *dst;

	entry = kmem_cache_alloc(page_entry_cache, GFP_KERNEL);
	if (entry == NULL) {
		pr_warn("%s failed to alloc page entry\n", __func__);
		return entry;
	}
	entry->offset = offset;
	entry->page = cleancache_alloc_page(&entry->gcma);
	if (!entry->page) {
		pr_warn("%s failed to alloc page from cma\n", __func__);
		kmem_cache_free(page_entry_cache, entry);
		return NULL;
	}
	atomic_set(&entry->refcount, 1);
	RB_CLEAR_NODE(&entry->rbnode);

	src = kmap_atomic(src_page);
	dst = kmap_atomic(entry->page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);
	return entry;
}

/* Returns positive tree id or negative error code */
int gcma_cleancache_init_fs(size_t pagesize)
{
	int nr_tree;
	struct cleancache_tree *tree;

	tree = kzalloc(sizeof(struct cleancache_tree), GFP_KERNEL);
	if (!tree) {
		pr_err("%s failed to alloc cleancache_tree\n", __func__);
		return -1;
	}

	tree->inodes_root = RB_ROOT;
	spin_lock_init(&tree->lock);

	nr_tree = atomic_add_return(1, &nr_cleancache_tree) - 1;
	cleancache_trees[nr_tree] = tree;

	atomic_inc(&gcma_cc_init_fses);
	return nr_tree;
}

int gcma_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	/* gcma doesn't support shared fs cleancache yet */
	return -1;
}

void gcma_cleancache_put_page(int tree_id, struct cleancache_filekey key,
				pgoff_t offset, struct page *page)
{
	struct page_entry *pentry, *dupentry;
	struct inode_entry *ientry;
	struct cleancache_tree *tree = cleancache_trees[tree_id];
	int res;

	if (!tree) {
		atomic_inc(&gcma_cc_put_failed_pages);
		return;
	}

	ientry = kmem_cache_alloc(inode_entry_cache, GFP_ATOMIC);
	if (ientry == NULL) {
		pr_warn("%s failed to alloc inode\n", __func__);
		return;
	}

	spin_lock(&tree->lock);
	ientry = find_get_inode_entry(&tree->inodes_root, &key);
	if (!ientry) {
		ientry = create_insert_get_inode_entry(&tree->inodes_root,
							&key, ientry);
		if (ientry == NULL) {
			spin_unlock(&tree->lock);
			atomic_inc(&gcma_cc_put_failed_pages);
			return;
		}
	} else {
		kmem_cache_free(inode_entry_cache, ientry);
	}
	spin_unlock(&tree->lock);

	pentry = create_page_entry(offset, page);
	if (pentry == NULL) {
		atomic_inc(&gcma_cc_put_failed_pages);
		goto out;
	}

	set_inode_entry(pentry->page, (void *)ientry);
	set_page_entry(pentry->page, (void *)pentry);

	spin_lock(&ientry->pages_lock);
	do {
		res = cc_page_rb_insert(&ientry->pages_root, pentry, &dupentry);
		if (res == -EEXIST)
			cc_page_rb_erase(&ientry->pages_root, dupentry);
	} while (res == -EEXIST);
	spin_unlock(&ientry->pages_lock);

	spin_lock(&clru_lock);
	set_gpage_flag(pentry->page, GF_CC_LRU);
	list_add(&pentry->page->lru, &clru_list);
	spin_unlock(&clru_lock);

	atomic_inc(&gcma_cc_put_pages);
out:
	spin_lock(&tree->lock);
	put_inode_entry(&tree->inodes_root, ientry);
	spin_unlock(&tree->lock);
}

int gcma_cleancache_get_page(int tree_id, struct cleancache_filekey key,
				pgoff_t offset, struct page *page)
{
	struct cleancache_tree *tree = cleancache_trees[tree_id];
	struct inode_entry *ientry;
	struct page_entry *pentry;
	u8 *src, *dst;

	if (!tree) {
		atomic_inc(&gcma_cc_get_failed_pages);
		return -1;
	}

	spin_lock(&tree->lock);
	ientry = find_get_inode_entry(&tree->inodes_root, (void *)&key);
	spin_unlock(&tree->lock);
	if (!ientry) {
		atomic_inc(&gcma_cc_get_failed_pages);
		return -1;
	}

	spin_lock(&ientry->pages_lock);
	pentry = find_get_page_entry(&ientry->pages_root, offset);
	spin_unlock(&ientry->pages_lock);
	if (!pentry) {
		atomic_inc(&gcma_cc_get_failed_pages);
		spin_lock(&tree->lock);
		put_inode_entry(&tree->inodes_root, ientry);
		spin_unlock(&tree->lock);
		atomic_inc(&gcma_cc_get_failed_pages);
		return -1;
	}

	src = kmap_atomic(pentry->page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	/*
	 * Because cleancache is ephemeral, we could save space by
	 * implementing this clean cache backend as exclusive cache.
	 * But, don't do so because we have LRU mechanism.
	 */
	spin_lock(&ientry->pages_lock);
	spin_lock(&clru_lock);
	if (likely(gpage_flag(pentry->page, GF_CC_LRU)))
		list_move(&pentry->page->lru, &clru_list);
	put_page_entry(&ientry->pages_root, pentry);
	spin_unlock(&clru_lock);
	spin_unlock(&ientry->pages_lock);

	spin_lock(&tree->lock);
	put_inode_entry(&tree->inodes_root, ientry);
	spin_unlock(&tree->lock);

	atomic_inc(&gcma_cc_get_pages);
	return 0;
}

void gcma_cleancache_invalidate_page(int tree_id, struct cleancache_filekey key,
					pgoff_t offset)
{
	struct cleancache_tree *tree = cleancache_trees[tree_id];
	struct inode_entry *ientry;
	struct page_entry *pentry;

	spin_lock(&tree->lock);
	ientry = cc_inode_rb_search(&tree->inodes_root, (void *)&key);
	if (!ientry) {
		spin_unlock(&tree->lock);
		atomic_inc(&gcma_cc_invalidate_failed_pages);
		return;
	}

	spin_lock(&ientry->pages_lock);
	pentry = cc_page_rb_search(&ientry->pages_root, offset);
	if (!pentry) {
		atomic_inc(&gcma_cc_invalidate_failed_pages);
		goto out;
	}

	spin_lock(&clru_lock);
	put_page_entry(&ientry->pages_root, pentry);
	spin_unlock(&clru_lock);
	atomic_inc(&gcma_cc_invalidate_pages);

out:
	spin_unlock(&ientry->pages_lock);
	spin_unlock(&tree->lock);
}

void gcma_cleancache_invalidate_inode(int tree_id,
					struct cleancache_filekey key)
{
	struct cleancache_tree *tree = cleancache_trees[tree_id];
	struct inode_entry *ientry;
	struct page_entry *pentry, *n;

	spin_lock(&tree->lock);
	ientry = cc_inode_rb_search(&tree->inodes_root, (void *)&key);
	if (!ientry) {
		spin_unlock(&tree->lock);
		atomic_inc(&gcma_cc_invalidate_failed_inodes);
		return;
	}
	spin_lock(&ientry->pages_lock);
	rbtree_postorder_for_each_entry_safe(pentry, n, &ientry->pages_root,
						rbnode) {
		spin_lock(&clru_lock);
		put_page_entry(&ientry->pages_root, pentry);
		spin_unlock(&clru_lock);
	}
	ientry->pages_root = RB_ROOT;
	spin_unlock(&ientry->pages_lock);

	put_inode_entry(&tree->inodes_root, ientry);
	spin_unlock(&tree->lock);
	atomic_inc(&gcma_cc_invalidate_inodes);
}

void gcma_cleancache_invalidate_fs(int tree_id)
{
	struct cleancache_tree *tree = cleancache_trees[tree_id];
	struct inode_entry *ientry, *n;
	struct page_entry *pentry, *m;

	if (!tree) {
		atomic_inc(&gcma_cc_invalidate_failed_fses);
		return;
	}

	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(ientry, n, &tree->inodes_root,
						rbnode) {
		spin_lock(&ientry->pages_lock);
		rbtree_postorder_for_each_entry_safe(pentry, m,
						&ientry->pages_root, rbnode) {
			spin_lock(&clru_lock);
			put_page_entry(&ientry->pages_root, pentry);
			spin_unlock(&clru_lock);
		}
		ientry->pages_root = RB_ROOT;
		spin_unlock(&ientry->pages_lock);
		put_inode_entry(&tree->inodes_root, ientry);
	}
	tree->inodes_root = RB_ROOT;
	spin_unlock(&tree->lock);

	kfree(tree);
	cleancache_trees[tree_id] = NULL;
	atomic_inc(&gcma_cc_invalidate_fses);
}

struct cleancache_ops gcma_cleancache_ops = {
	.init_fs = gcma_cleancache_init_fs,
	.init_shared_fs = gcma_cleancache_init_shared_fs,
	.get_page = gcma_cleancache_get_page,
	.put_page = gcma_cleancache_put_page,
	.invalidate_page = gcma_cleancache_invalidate_page,
	.invalidate_inode = gcma_cleancache_invalidate_inode,
	.invalidate_fs = gcma_cleancache_invalidate_fs,
};

/*
 * Return 0 if [start_pfn, end_pfn] is isolated.
 * Otherwise, return first unisolated pfn from the start_pfn.
 */
static unsigned long isolate_interrupted(struct gcma *gcma,
		unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long offset;
	unsigned long *bitmap;
	unsigned long pfn, ret = 0;
	struct page *page;

	spin_lock(&gcma->lock);

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		int set;

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;

		set = test_bit(pfn % BITS_PER_LONG, bitmap);
		if (!set) {
			ret = pfn;
			break;
		}

		page = pfn_to_page(pfn);
		if (!gpage_flag(page, GF_ISOLATED)) {
			ret = pfn;
			break;
		}

	}
	spin_unlock(&gcma->lock);
	return ret;
}

/*
 * gcma_alloc_contig - allocates contiguous pages
 *
 * @start_pfn	start pfn of requiring contiguous memory area
 * @size	size of the requiring contiguous memory area
 *
 * Returns 0 on success, error code on failure.
 */
int gcma_alloc_contig(struct gcma *gcma, unsigned long start_pfn,
			unsigned long size)
{
	LIST_HEAD(fs_free_pages);
	LIST_HEAD(cc_free_pages);
	struct page *page, *n;
	struct swap_slot_entry *entry;
	unsigned long offset;
	unsigned long *bitmap;
	struct frontswap_tree *tree;
	unsigned long pfn;
	unsigned long orig_start = start_pfn;
	pgoff_t slot_offset;

	struct inode_entry *ientry;
	struct page_entry *pentry;

retry:
	for (pfn = start_pfn; pfn < start_pfn + size; pfn++) {
		spin_lock(&gcma->lock);

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;
		page = pfn_to_page(pfn);

		if (!test_bit(offset % BITS_PER_LONG, bitmap)) {
			/* set a bit for prevent allocation for frontswap */
			bitmap_set(gcma->bitmap, offset, 1);
			set_gpage_flag(page, GF_ISOLATED);
			spin_unlock(&gcma->lock);
			continue;
		}
		if (gpage_flag(page, GF_ISOLATED)) {
			spin_unlock(&gcma->lock);
			continue;
		}

		/* Someone is using the page so it's complicated :( */
		spin_unlock(&gcma->lock);

		spin_lock(&slru_lock);
		spin_lock(&clru_lock);
		spin_lock(&gcma->lock);

		/* Avoid allocation from other threads */
		set_gpage_flag(page, GF_RECLAIMING);

		/*
		 * The page is in LRU and being used by someone. Remove from
		 * LRU and ready to put the swap slot soon.
		 */
		if (gpage_flag(page, GF_SWAP_LRU)) {
			pr_info("frontswap page found from %s\n", __func__);
			entry = swap_slot(page);
			if (atomic_inc_not_zero(&entry->refcount)) {
				clear_gpage_flag(page, GF_SWAP_LRU);
				list_move(&page->lru, &fs_free_pages);
				atomic_inc(&gcma_reclaimed_pages);
				goto next_page;
			}
		}

		if (gpage_flag(page, GF_CC_LRU)) {
			pr_info("cleancache page found from %s\n", __func__);
			pentry = page_entry(page);
			if (atomic_inc_not_zero(&pentry->refcount)) {
				clear_gpage_flag(page, GF_CC_LRU);
				list_move(&page->lru, &cc_free_pages);
				atomic_inc(&gcma_reclaimed_pages);
				goto next_page;
			}
		}

		/*
		 * The page is
		 * 1) allocated by others but not yet in LRU in case of
		 *    frontswap_store or
		 * 2) deleted from LRU but not yet from gcma's bitmap in case
		 *    of frontswap_invalidate or evict_frontswap_pages.
		 * Anycase, the race is small so retry after a while will see
		 * success. Below isolate_interrupted handles it.
		 */
next_page:
		spin_unlock(&gcma->lock);
		spin_unlock(&clru_lock);
		spin_unlock(&slru_lock);
	}

	/*
	 * Since we increased refcount of the page above, we can access
	 * swap_slot_entry with safe
	 */
	pr_info("let's iterate fs free pages\n");
	list_for_each_entry_safe(page, n, &fs_free_pages, lru) {
		tree = swap_tree(page);
		entry = swap_slot(page);

		spin_lock(&tree->lock);
		spin_lock(&slru_lock);
		/* drop refcount increased by above loop */
		slot_offset = entry->offset;
		swap_slot_entry_put(tree, entry);
		/* free entry if the entry is still in tree */
		if (frontswap_rb_search(&tree->rbroot, slot_offset))
			swap_slot_entry_put(tree, entry);
		spin_unlock(&slru_lock);
		spin_unlock(&tree->lock);
	}

	pr_info("let's iterate cc free pages\n");
	list_for_each_entry_safe(page, n, &cc_free_pages, lru) {
		ientry = inode_entry(page);
		pentry = page_entry(page);

		spin_lock(&ientry->pages_lock);
		spin_lock(&clru_lock);
		/* drop refcount increased by above loop */
		slot_offset = pentry->offset;
		put_page_entry(&ientry->pages_root, pentry);
		/* free entry if the entry is still in tree */
		if (cc_page_rb_search(&ientry->pages_root, slot_offset))
			put_page_entry(&ientry->pages_root, pentry);
		spin_unlock(&clru_lock);
		spin_unlock(&ientry->pages_lock);
	}

	start_pfn = isolate_interrupted(gcma, orig_start, orig_start + size);
	if (start_pfn)
		goto retry;

	return 0;
}

/*
 * gcma_free_contig - free allocated contiguous pages
 *
 * @start_pfn	start pfn of freeing contiguous memory area
 * @size	number of pages in freeing contiguous memory area
 */
void gcma_free_contig(struct gcma *gcma,
		      unsigned long start_pfn, unsigned long size)
{
	unsigned long offset;

	spin_lock(&gcma->lock);
	offset = start_pfn - gcma->base_pfn;
	bitmap_clear(gcma->bitmap, offset, size);
	spin_unlock(&gcma->lock);
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
	debugfs_create_atomic_t("reclaimed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_reclaimed_pages);

	debugfs_create_atomic_t("cc_init_fses", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_init_fses);
	debugfs_create_atomic_t("cc_put_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_put_pages);
	debugfs_create_atomic_t("cc_put_failed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_put_failed_pages);
	debugfs_create_atomic_t("cc_get_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_get_pages);
	debugfs_create_atomic_t("cc_get_failed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_get_failed_pages);
	debugfs_create_atomic_t("cc_invalidate_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_pages);
	debugfs_create_atomic_t("cc_invalidate_failed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_failed_pages);
	debugfs_create_atomic_t("cc_invalidate_inodes", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_inodes);
	debugfs_create_atomic_t("cc_invalidate_failed_inodes", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_failed_inodes);
	debugfs_create_atomic_t("cc_invalidate_fses", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_pages);
	debugfs_create_atomic_t("cc_invalidate_failed_fses", S_IRUGO,
			gcma_debugfs_root, &gcma_cc_invalidate_failed_pages);

	pr_info("gcma debufs init\n");
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
	pr_info("loading gcma\n");

	spin_lock_init(&slru_lock);
	swap_slot_entry_cache = KMEM_CACHE(swap_slot_entry, 0);
	if (swap_slot_entry_cache == NULL)
		return -ENOMEM;

	/*
	 * By writethough mode, GCMA could discard all of pages in an instant
	 * instead of slow writing pages out to the swap device.
	 */
	frontswap_writethrough(true);
	frontswap_register_ops(&gcma_frontswap_ops);

	spin_lock_init(&clru_lock);
	inode_entry_cache = KMEM_CACHE(inode_entry, 0);
	if (inode_entry_cache == NULL) {
		pr_warn("failed to create inode cache\n");
		return -ENOMEM;
	}

	page_entry_cache= KMEM_CACHE(page_entry, 0);
	if (page_entry_cache == NULL) {
		pr_warn("failed to create page entry cache\n");
		kmem_cache_destroy(inode_entry_cache);
		return -ENOMEM;
	}
	cleancache_register_ops(&gcma_cleancache_ops);
	gcma_debugfs_init();
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
