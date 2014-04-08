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
#include <linux/cleancache.h>

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
static atomic_t total_size = ATOMIC_INIT(0);
static atomic_t alloced_size = ATOMIC_INIT(0);

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
	int gcma_id;
	struct gcma_info *info;
	phys_addr_t addr;

	gcma_id = atomic_inc_return(&reserved_gcma);
	if (gcma_id > MAX_GCMA) {
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

	gcma_id -= 1;
	pr_debug("%llu bytes gcma reserved\n", size);

	info = &ginfo[gcma_id];

	info->size = size >> PAGE_SHIFT;
	info->base_pfn = PFN_DOWN(addr);

	atomic_add(size, &total_size);
	return gcma_id;
}

static void cleanup_frontswap(void);

/**
 * gcma_alloc_contig - allocate pages from contiguous area
 * @gmca_id: Identifier of contiguous memory area which the allocation be done
 * @pages: number of pages
 *
 * Returns NULL if failed to allocate,
 * Returns struct page of start address of allocated memory
 */
struct page *gcma_alloc_contig(int gcma_id, int pages)
{
	int max_gcma;
	int id = gcma_id;
	unsigned long *bitmap, next_zero_area;
	struct gcma_info *info;

	max_gcma = atomic_read(&reserved_gcma);
	if (id >= max_gcma) {
		pr_warn("invalid gcma_id %d [%d]\n", id, max_gcma);
		return NULL;
	}

	info = &ginfo[id];
	bitmap = info->bitmap;
	spin_lock(&info->lock);

	/*
	 * TODO : we should respect mask for dma allocation instead of 0
	 */
	next_zero_area = bitmap_find_next_zero_area(bitmap, info->size,
				0, pages, 0);
	if (next_zero_area >= info->size) {
		pr_warn("failed to alloc pages count %d\n", pages);
		spin_unlock(&info->lock);
		return NULL;
	}

	bitmap_set(bitmap, next_zero_area, pages);
	spin_unlock(&info->lock);

	atomic_add(pages * PAGE_SIZE, &alloced_size);
	/* Trigger frontswap cleanup if 80% of contiguous memory is full */
	if (atomic_read(&alloced_size) >= (atomic_read(&total_size) / 5) * 4) {
		pr_debug("trigger cleancache / frontswap cleanup\n");
		cleanup_frontswap();
	}

	return pfn_to_page(info->base_pfn + next_zero_area);
}
EXPORT_SYMBOL_GPL(gcma_alloc_contig);

/**
 * gcma_release_contig - release pages from contiguous area
 * @gcma_id: id of contiguous memory area
 * @pages: Requested number of pages.
 */
void gcma_release_contig(int gcma_id, struct page *page, int pages)
{
	unsigned long pfn, offset;
	unsigned long *bitmap;
	unsigned long next_zero_bit;
	struct gcma_info *info;
	int id = gcma_id;
	int max_gcma = atomic_read(&reserved_gcma);

	if (id >= max_gcma) {
		pr_warn("invalid gcma_id %d [%d]\n", id, max_gcma);
		return;
	}

	info = &ginfo[id];
	pfn = page_to_pfn(page);
	offset = pfn - info->base_pfn;

	pr_debug("pfn: %ld, gcma start: %ld, offset: %ld\n",
			pfn, info->base_pfn, offset);

	bitmap = info->bitmap;
	spin_lock(&info->lock);

	/* TODO: Let's do this in only debug mode */
	next_zero_bit = find_next_zero_bit(bitmap, offset + pages, offset);
	if (next_zero_bit < offset + pages - 1) {
		pr_err("freeing free area. free page: %ld\n", next_zero_bit);
		spin_unlock(&info->lock);
		return;
	}

	bitmap_clear(bitmap, offset, pages);
	spin_unlock(&info->lock);
}
EXPORT_SYMBOL_GPL(gcma_release_contig);

struct frontswap_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	unsigned int gcma_id;
	struct page *page;
	int refcount;
};

struct frontswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static struct frontswap_tree *gcma_swap_trees[MAX_SWAPFILES];
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
	gcma_release_contig(entry->gcma_id, entry->page, 1);
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
	struct frontswap_entry *entry, *dupentry;
	struct page *cma_page = NULL;
	struct frontswap_tree *tree = gcma_swap_trees[type];
	u8 *src, *dst;
	int i, ret;
	int max_gcma = atomic_read(&reserved_gcma);

	if (!tree) {
		pr_warn("frontswap tree for type %d is not exist\n",
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
	entry->gcma_id = i;

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
int gcma_frontswap_load(unsigned type, pgoff_t offset,
			       struct page *page)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
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

void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
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

static void gcma_frontswap_cleanup_area(unsigned type)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
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
}

void gcma_frontswap_invalidate_area(unsigned type)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
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
	gcma_swap_trees[type] = NULL;
}

static void cleanup_frontswap(void)
{
	int i = 0;

	spin_lock(&cleanup_lock);
	for (i = 0; i < MAX_SWAPFILES; i++) {
		if (gcma_swap_trees[i]) {
			pr_debug("cleanup %d type!!!!\n", i);
			gcma_frontswap_cleanup_area(i);
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
 * cleancache backend
 *********************************/

static int gcma_cleancache_init_fs(size_t pagesize)
{
	return -1;
}

static int gcma_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	return -1;
}

static int gcma_cleancache_get_page(int pool_id, struct cleancache_filekey key,
					pgoff_t index, struct page *page)
{
	return -1;
}

static void gcma_cleancache_put_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t index, struct page *page)
{
	return;
}

static void gcma_cleancache_invalidate_page(int pool_id,
						struct cleancache_filekey key,
						pgoff_t index)
{
	return;
}

static void gcma_cleancache_invalidate_inode(int pool_id,
						struct cleancache_filekey key)
{
	return;
}

static void gcma_cleancache_invalidate_fs(int pool_id)
{
	return;
}

static struct cleancache_ops gcma_cleancache_ops = {
	.init_fs = gcma_cleancache_init_fs,
	.init_shared_fs = gcma_cleancache_init_shared_fs,
	.put_page = gcma_cleancache_put_page,
	.get_page = gcma_cleancache_get_page,
	.invalidate_page = gcma_cleancache_invalidate_page,
	.invalidate_inode = gcma_cleancache_invalidate_inode,
	.invalidate_fs = gcma_cleancache_invalidate_fs
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

	spin_lock_init(&cleanup_lock);
	frontswap_entry_cache = KMEM_CACHE(frontswap_entry, 0);
	if (frontswap_entry_cache == NULL) {
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

	cleancache_register_ops(&gcma_cleancache_ops);
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
