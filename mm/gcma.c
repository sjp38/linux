/*
 * gcma.c - Guaranteed Contiguous Memory Allocator
 *
 * GCMA aims for successful allocation within predictable time limit.
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

struct gcma_page {
	struct page *page;
	unsigned int gid;
};

struct swap_slot_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	struct gcma_page gpage;
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

/*********************************
* tunables
**********************************/
/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool gcma_enabled __read_mostly;
module_param_named(enabled, gcma_enabled, bool, 0);

/* Default size of contiguous memory area : 100M */
static unsigned long long def_gcma_bytes __read_mostly = (100 << 20);

static int __init early_gcma(char *p)
{
	pr_debug("set %s as default cma size\n", p);
	def_gcma_bytes = memparse(p, &p);
	return 0;
}
early_param("gcma.def_cma_bytes", early_gcma);

#define MAX_GCMA	32

struct gcma_info {
	unsigned long *bitmap;
	unsigned long base_pfn;
	unsigned long size; /* PAGE_SIZE unit */
	spinlock_t lock;
};

static struct gcma_info ginfo[MAX_GCMA];
static atomic_t reserved_gcma = ATOMIC_INIT(0);

static int evict_frontswap_pages(int gid, int pages);

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


/**
 * gcma_reserve - Reserve contiguous memory area
 * @size: Size of the reserved area (in bytes), 0 for default size
 *
 * This function reserves memory from early allocator, memblock.
 * It can be called several times under MAX_GCMA during boot.
 *
 * Returns id of reserved contiguous memory area if success,
 * Otherwise, return negative number.
 */
int __init gcma_reserve(unsigned long long size)
{
	int gid;
	struct gcma_info *info;
	phys_addr_t addr;

	gid = atomic_inc_return(&reserved_gcma);
	if (gid > MAX_GCMA) {
		atomic_dec(&reserved_gcma);
		pr_warn("There is no more space in GCMA.\n");
		return -ENOMEM;
	}

	if (size == 0)
		size = def_gcma_bytes;

	size = PAGE_ALIGN(size);
	/* TODO: Why not MEMBLOCK_ALLOC_ANYWHERE? */
	addr = __memblock_alloc_base((phys_addr_t)size, PAGE_SIZE,
				MEMBLOCK_ALLOC_ACCESSIBLE);
	if (!addr) {
		pr_warn("failed to reserveg cma\n");
		atomic_dec(&reserved_gcma);
		return -ENOMEM;
	}

	gid -= 1;
	pr_debug("%llu bytes gcma reserved\n", size);

	info = &ginfo[gid];

	info->size = size >> PAGE_SHIFT;
	info->base_pfn = PFN_DOWN(addr);

	return gid;
}

/**
 * gcma_alloc_contig - allocate pages from contiguous area
 * @gmca_id: Identifier of contiguous memory area which the allocation be done
 * @pages: number of pages
 *
 * Returns NULL if failed to allocate,
 * Returns struct page of start address of allocated memory
 */
struct page *gcma_alloc_contig(int gid, int pages)
{
	int max_gcma;
	int id = gid;
	unsigned long *bitmap, next_zero_area;
	struct gcma_info *info;
	struct page *page = NULL;

	max_gcma = atomic_read(&reserved_gcma);
	if (id >= max_gcma) {
		WARN(1, "invalid gcma_id %d [%d]\n", id, max_gcma);
		goto out;
	}

	info = &ginfo[id];
	bitmap = info->bitmap;
	spin_lock(&info->lock);
	do {
		/*
		 * TODO : we should respect mask for dma allocation instead of 0
		 */
		next_zero_area = bitmap_find_next_zero_area(bitmap, info->size,
				0, pages, 0);
		if (next_zero_area >= info->size) {
			spin_unlock(&info->lock);
			if (evict_frontswap_pages(gid, pages) < 0) {
				return NULL;
			}
			spin_lock(&info->lock);
		}
	} while (next_zero_area >= info->size);

	bitmap_set(bitmap, next_zero_area, pages);
	spin_unlock(&info->lock);

	page = pfn_to_page(info->base_pfn + next_zero_area);
out:
	return page;
}
EXPORT_SYMBOL_GPL(gcma_alloc_contig);

/**
 * gcma_release_contig - release pages from contiguous area
 * @gid: id of contiguous memory area
 * @pages: Requested number of pages.
 */
void gcma_release_contig(int gid, struct page *page, int pages)
{
	unsigned long pfn, offset;
	unsigned long *bitmap;
	unsigned long next_zero_bit;
	struct gcma_info *info;
	int max_gcma = atomic_read(&reserved_gcma);

	if (gid >= max_gcma) {
		WARN(1, "invalid gid %d [%d]\n", gid, max_gcma);
		return;
	}

	info = &ginfo[gid];
	pfn = page_to_pfn(page);
	offset = pfn - info->base_pfn;

	bitmap = info->bitmap;
	spin_lock(&info->lock);

	next_zero_bit = find_next_zero_bit(bitmap, offset + pages, offset);
	if (next_zero_bit < offset + pages - 1)
		BUG();

	bitmap_clear(bitmap, offset, pages);
	spin_unlock(&info->lock);
}
EXPORT_SYMBOL_GPL(gcma_release_contig);

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
	gcma_release_contig(entry->gpage.gid, entry->gpage.page, 1);
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
		struct page *page;
		page = entry->gpage.page;

		frontswap_rb_erase(&tree->rbroot, entry);
		if (!locked)
			spin_lock(&swap_lru_lock);
		list_del(&page->lru);
		if (!locked)
			spin_unlock(&swap_lru_lock);
		frontswap_free_entry(entry);
	}
}

static int evict_frontswap_pages(int gid, int pages)
{
	struct frontswap_tree *tree;
	struct swap_slot_entry *entry;
	struct page *page, *n;
	int evicted = 0;
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
		if (entry->gpage.gid != gid) {
			tree = (struct frontswap_tree *)root_of(page);
			swap_slot_entry_put(tree, entry, 1);
			continue;
		}

		list_move(&page->lru, &free_pages);

		if (++evicted >= pages)
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
	int i, ret;
	int max_gcma = atomic_read(&reserved_gcma);

	if (!tree) {
		WARN(1, "frontswap tree for type %d is not exist\n",
				type);
		return -ENODEV;
	}

	/*
	 * TODO: It's unfair because first-id reserved area is
	 * always consumed compared to others. Maybe we should
	 * do round-robin in future.
	 */
	for (i = 0; i < max_gcma; i++) {
		cma_page = gcma_alloc_contig(i, 1);
		if (cma_page != NULL)
			break;
	}

	if (cma_page == NULL) {
		pr_warn("failed to get 1 page from gcma\n");
		return -ENOMEM;
	}

	entry = kmem_cache_alloc(swap_slot_entry_cache, GFP_KERNEL);
	if (!entry) {
		return -ENOMEM;
	}

	entry->gpage.page = cma_page;
	entry->gpage.gid = i;
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
	list_add(&cma_page->lru, &swap_lru_list);
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

	src = kmap_atomic(entry->gpage.page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&tree->lock);
	spin_lock(&swap_lru_lock);
	list_move(&entry->gpage.page->lru, &swap_lru_list);
	spin_unlock(&swap_lru_lock);
	swap_slot_entry_put(tree, entry, 0);
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

	if (!tree) {
		WARN(1, "failed to get frontswap tree for type %d\n", type);
		return;
	}

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

static int __init init_gcma(void)
{
	int i;
	unsigned long bitmap_bytes;
	int max_gcma;

	if (!gcma_enabled)
		return 0;

	pr_info("loading gcma\n");
	max_gcma = atomic_read(&reserved_gcma);

	for (i = 0; i < max_gcma; i++) {
		spin_lock_init(&ginfo[i].lock);
		bitmap_bytes = ginfo[i].size / sizeof(*ginfo[i].bitmap);
		if (ginfo[i].size % sizeof(*ginfo[i].bitmap))
			bitmap_bytes += 1;
		ginfo[i].bitmap = kzalloc(bitmap_bytes, GFP_KERNEL);
		if (!ginfo[i].bitmap) {
			pr_debug("failed to alloc bitmap\n");
			return -ENOMEM;
		}
	}

	spin_lock_init(&swap_lru_lock);
	swap_slot_entry_cache = KMEM_CACHE(swap_slot_entry, 0);
	if (swap_slot_entry_cache == NULL) {
		pr_warn("failed to create frontswap cache\n");
		/* TODO : free allocated memory */
		return -ENOMEM;
	}

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
