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

struct gcma_page {
	struct page *page;
	unsigned int gid;
};

struct swap_slot_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	struct gcma_page gpage;
	int refcount;
	struct list_head lru_list;
	struct frontswap_tree *tree;
};

struct frontswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static struct list_head swap_lru_list;
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

static int evict_cleancache_pages(int gid, int pages);

static int evict_frontswap_pages(int gid, int pages);

/**
 * evict_pages - Evict pages used for frontswap / cleancache backend
 * @gid: Identifier for cma to evict
 * @pages: Number of pages requested to be evicted
 *
 * returns number of pages evicted
 * returns negative number if eviction failed
 */
static int evict_pages(int gid, int pages)
{
	int evicted;

	evicted = evict_cleancache_pages(gid, pages);
	if (evicted < pages)
		evicted += evict_frontswap_pages(gid, pages - evicted);

	return evicted;
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

	max_gcma = atomic_read(&reserved_gcma);
	if (id >= max_gcma) {
		pr_warn("invalid gcma_id %d [%d]\n", id, max_gcma);
		return NULL;
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
			pr_warn("failed to alloc pages count %d\n", pages);
			if (evict_pages(gid, pages) < 0) {
				pr_warn("failed to evict pages\n");
				return NULL;
			}
			spin_lock(&info->lock);
		}
	} while (next_zero_area >= info->size);

	bitmap_set(bitmap, next_zero_area, pages);
	spin_unlock(&info->lock);

	return pfn_to_page(info->base_pfn + next_zero_area);
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
		pr_warn("invalid gid %d [%d]\n", gid, max_gcma);
		return;
	}

	info = &ginfo[gid];
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
	entry->refcount++;
}

/*
 * Stolen from zswap.
 * Caller should hold frontswap tree spinlock
 * remove from the tree and free it, if nobody reference the entry
 */
static void swap_slot_entry_put(struct frontswap_tree *tree,
		struct swap_slot_entry *entry)
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
static struct swap_slot_entry *frontswap_rb_find_get(struct rb_root *root,
			pgoff_t offset)
{
	struct swap_slot_entry *entry = frontswap_rb_search(root, offset);
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

	entry = kmem_cache_alloc(swap_slot_entry_cache, GFP_KERNEL);
	if (!entry) {
		pr_warn("failed to get frontswap entry from cache\n");
		return -ENOMEM;
	}

	entry->gpage.page = cma_page;
	entry->gpage.gid = i;
	entry->tree = tree;

	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);
	INIT_LIST_HEAD(&entry->lru_list);

	src = kmap_atomic(page);
	dst = kmap_atomic(cma_page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	entry->offset = offset;

	spin_lock(&tree->lock);
	do {
		ret = frontswap_rb_insert(&tree->rbroot, entry, &dupentry);
		if (ret == -EEXIST)
			frontswap_rb_erase(&tree->rbroot, dupentry);
	} while (ret == -EEXIST);
	spin_unlock(&tree->lock);

	spin_lock(&swap_lru_lock);
	list_add(&entry->lru_list, &swap_lru_list);
	spin_unlock(&swap_lru_lock);
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

	src = kmap_atomic(entry->gpage.page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&tree->lock);
	swap_slot_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	spin_lock(&swap_lru_lock);
	list_del_init(&entry->lru_list);
	list_add(&entry->lru_list, &swap_lru_list);
	spin_unlock(&swap_lru_lock);

	return 0;
}

void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
	struct swap_slot_entry *entry;

	spin_lock(&tree->lock);
	entry = frontswap_rb_search(&tree->rbroot, offset);
	if (!entry) {
		pr_warn("failed to get entry\n");
		spin_unlock(&tree->lock);
		return;
	}

	spin_lock(&swap_lru_lock);
	list_del(&entry->lru_list);
	spin_unlock(&swap_lru_lock);

	frontswap_rb_erase(&tree->rbroot, entry);
	swap_slot_entry_put(tree, entry);

	spin_unlock(&tree->lock);
}

void gcma_frontswap_invalidate_area(unsigned type)
{
	struct frontswap_tree *tree = gcma_swap_trees[type];
	struct swap_slot_entry *entry, *n;

	if (!tree) {
		pr_warn("failed to get frontswap tree for type %d\n", type);
		return;
	}

	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(entry, n, &tree->rbroot, rbnode) {
		spin_lock(&swap_lru_lock);
		list_del(&entry->lru_list);
		spin_unlock(&swap_lru_lock);

		frontswap_free_entry(entry);
	}
	tree->rbroot = RB_ROOT;
	spin_unlock(&tree->lock);

	kfree(tree);
	gcma_swap_trees[type] = NULL;
}

static int evict_frontswap_pages(int gid, int pages)
{
	struct frontswap_tree *tree;
	struct swap_slot_entry *entry, *n;
	int evicted = 0;

	spin_lock(&swap_lru_lock);
	list_for_each_entry_safe_reverse(entry, n, &swap_lru_list, lru_list) {
		if (entry->gpage.gid != gid)
			continue;
		list_del(&entry->lru_list);

		tree = entry->tree;
		spin_lock(&tree->lock);
		frontswap_free_entry(entry);
		spin_unlock(&tree->lock);

		if (++evicted >= pages)
			goto out;
	}
out:
	spin_unlock(&swap_lru_lock);
	return evicted;
}

static struct frontswap_ops gcma_frontswap_ops = {
	.init = gcma_frontswap_init,
	.store = gcma_frontswap_store,
	.load = gcma_frontswap_load,
	.invalidate_page = gcma_frontswap_invalidate_page,
	.invalidate_area = gcma_frontswap_invalidate_area
};


/***********************
 * cleancache backend
 ***********************/
/*
 * gcma manage entries for frontswap / cleancache backend using
 * red-black tree. Could the data structure be changed later.
 *
 * In cleancache case, inodes reside in tree for each fs,
 * pages reside in inode's tree.
 */

/* comes from zcache. */
#define MAX_CLEANCACHE_FS 16
#define FILEKEY_LEN sizeof(struct cleancache_filekey)

struct inode_entry {
	struct rb_node rbnode;
	char filekey[FILEKEY_LEN];
	int refcount;
	struct rb_root page_root;
	spinlock_t pages_lock;
};

struct page_entry {
	struct rb_node rbnode;
	pgoff_t pgoffset;
	int refcount;
	struct gcma_page gpage;
	struct list_head lru_list;
	struct inode_entry *inode;
};

struct cleancache_tree {
	struct rb_root inodes_root;
	spinlock_t lock;
};

static struct list_head page_lru_list;
static spinlock_t page_lru_lock;
static struct kmem_cache *inode_entry_cache;
static struct kmem_cache *page_entry_cache;

static int compare_filekey(void *l, void *r)
{
	return memcmp(l, r, FILEKEY_LEN);
}

static int compare_pgoffset(pgoff_t l, pgoff_t r)
{
	return l - r;
}


/************************************************************
 * internal data structure manipulation of inode, page entry
 *
 * Stolen from zswap mostly.
 ************************************************************/

/*
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST
 */
static int insert_inode_entry(struct rb_root *root,
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

static void erase_inode_entry(struct rb_root *root, struct inode_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static struct inode_entry *search_inode_entry(struct rb_root *root,
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

/*
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST
 */
static int insert_page_entry(struct rb_root *root,
				struct page_entry *entry,
				struct page_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct page_entry *pentry;
	int compared;

	while (*link) {
		parent = *link;
		pentry = rb_entry(parent, struct page_entry, rbnode);
		compared = compare_pgoffset(entry->pgoffset, pentry->pgoffset);
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

static void erase_page_entry(struct rb_root *root, struct page_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static struct page_entry *search_page_entry(struct rb_root *root,
						pgoff_t pgoffset)
{
	struct rb_node *node = root->rb_node;
	struct page_entry *entry;
	int compared;

	while (node) {
		entry = rb_entry(node, struct page_entry, rbnode);
		compared = compare_pgoffset(pgoffset, entry->pgoffset);
		if (compared < 0)
			node = node->rb_left;
		else if (compared > 0)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}


/*************************************
 * inode, page entry put / get / free
 *
 * Stolen from zswap mostly.
 *************************************/

static void free_inode_entry(struct inode_entry *entry)
{
	kmem_cache_free(inode_entry_cache, entry);
}

static void free_page_entry(struct page_entry *entry)
{
	if (entry->gpage.page != NULL)
		gcma_release_contig(entry->gpage.gid, entry->gpage.page, 1);
	kmem_cache_free(page_entry_cache, entry);
}

/*
 * Caller should hold lock of entry's root
 */
static void get_inode_entry(struct inode_entry *entry)
{
	entry->refcount++;
}

/*
 * Caller should hold lock of entry's root
 */
static void get_page_entry(struct page_entry *entry)
{
	entry->refcount++;
}

/*
 * Caller should hold lock of entry's root
 */
static void put_inode_entry(struct rb_root *root,
				struct inode_entry *entry)
{
	int refcount = --entry->refcount;

	BUG_ON(refcount < 0);
	if (refcount == 0) {
		erase_inode_entry(root, entry);
		free_inode_entry(entry);
	}
}

/*
 * Caller should hold lock of entry's root
 */
static void put_page_entry(struct rb_root *root,
				struct page_entry *entry)
{
	int refcount = --entry->refcount;

	BUG_ON(refcount < 0);
	if (refcount == 0) {
		erase_page_entry(root, entry);
		free_page_entry(entry);
	}
}

/*
 * Caller should hold lock of entry's root
 */
static struct inode_entry *find_get_inode_entry(struct rb_root *root,
						void *filekey)
{
	struct inode_entry *entry = search_inode_entry(root, filekey);
	if (entry)
		get_inode_entry(entry);
	return entry;
}

/*
 * Caller should hold root of tree's spinlock
 */
static struct page_entry *find_get_page_entry(struct rb_root *root,
						pgoff_t pgoffset)
{
	struct page_entry *entry = search_page_entry(root, pgoffset);
	if (entry)
		get_page_entry(entry);
	return entry;
}


/*******************************
 * clean cache ops helper
 *******************************/

/*
 * Caller should ensure that same key inode entry is not exist in root
 */
static struct inode_entry *create_insert_get_inode_entry(struct rb_root *root,
						void *filekey)
{
	struct inode_entry *entry, *dupentry;

	entry = kmem_cache_alloc(inode_entry_cache, GFP_ATOMIC);
	if (entry == NULL) {
		pr_warn("failed to alloc inode from %s\b", __func__);
		return NULL;
	}
	entry->page_root = RB_ROOT;
	memcpy(entry->filekey, filekey, FILEKEY_LEN);
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);
	spin_lock_init(&entry->pages_lock);

	BUG_ON(insert_inode_entry(root, entry, &dupentry) == -EEXIST);
	get_inode_entry(entry);
	return entry;
}

/**
 * allocate a page from cma
 *
 * returns true if success,
 * returns false if failed.
 */
static bool alloc_cma_page(struct gcma_page *gpage)
{
	struct page *page = NULL;
	int max_gcma = atomic_read(&reserved_gcma);
	static int seek_start;
	int count, i;

	i = seek_start++;
	for (count = 0; count < max_gcma; count++, i++) {
		if (i >= max_gcma)
			i = 0;
		page = gcma_alloc_contig(i, 1);
		if (page != NULL)
			break;
	}
	if (count == max_gcma) {
		pr_warn("failed to alloc cma for page\n");
		return false;
	}

	gpage->page = page;
	gpage->gid = i;

	return true;
}

static struct page_entry *create_page_entry(pgoff_t pgoffset,
						struct page *src_page)
{
	struct page_entry *entry;
	u8 *src, *dst;

	entry = kmem_cache_alloc(page_entry_cache, GFP_KERNEL);
	if (entry == NULL) {
		pr_warn("failed to alloc page entry\n");
		return entry;
	}
	entry->pgoffset = pgoffset;
	if (alloc_cma_page(&entry->gpage) == false) {
		pr_warn("failed to alloc page from cma\n");
		kmem_cache_free(page_entry_cache, entry);
		return NULL;
	}
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);

	src = kmap_atomic(src_page);
	dst = kmap_atomic(entry->gpage.page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);
	return entry;
}

/*****************
 * cleancache ops
 *****************/
static struct cleancache_tree *cleancache_pools[MAX_CLEANCACHE_FS];
static atomic_t nr_cleancache_pool = ATOMIC_INIT(0);

int gcma_cleancache_init_fs(size_t pagesize)
{
	int nr_pool;
	struct cleancache_tree *pool;

	pr_debug("%s called", __func__);
	pool = kzalloc(sizeof(struct cleancache_tree), GFP_KERNEL);
	if (!pool) {
		pr_err("failed to alloc cleancache_tree from %s failed\n",
				__func__);
		return -1;
	}

	pool->inodes_root = RB_ROOT;
	spin_lock_init(&pool->lock);

	nr_pool = atomic_add_return(1, &nr_cleancache_pool) - 1;
	cleancache_pools[nr_pool] = pool;

	return nr_pool;
}

int gcma_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	return -1;
}

/**
 * should not be called from irq
 */
void gcma_cleancache_put_page(int pool_id, struct cleancache_filekey key,
				pgoff_t pgoffset, struct page *page)
{
	struct page_entry *pentry, *dupentry;
	struct inode_entry *ientry;
	struct cleancache_tree *pool = cleancache_pools[pool_id];
	int res;

	if (!pool) {
		pr_warn("cleancache pool for id %d is not exist\n", pool_id);
		return;
	}

	spin_lock(&pool->lock);
	ientry = find_get_inode_entry(&pool->inodes_root, &key);
	if (!ientry) {
		ientry = create_insert_get_inode_entry(&pool->inodes_root,
				&key);
		if (ientry == NULL) {
			spin_unlock(&pool->lock);
			return;
		}
	}
	spin_unlock(&pool->lock);

	pentry = create_page_entry(pgoffset, page);
	if (pentry == NULL)
		goto out;

	spin_lock(&ientry->pages_lock);
	do {
		res = insert_page_entry(&ientry->page_root, pentry,
						&dupentry);
		if (res == -EEXIST)
			erase_page_entry(&ientry->page_root, dupentry);
	} while (res == -EEXIST);
	spin_unlock(&ientry->pages_lock);

	spin_lock(&page_lru_lock);
	list_add(&pentry->lru_list, &page_lru_list);
	spin_unlock(&page_lru_lock);

out:
	spin_lock(&pool->lock);
	put_inode_entry(&pool->inodes_root, ientry);
	spin_unlock(&pool->lock);
}

int gcma_cleancache_get_page(int pool_id, struct cleancache_filekey key,
				pgoff_t pgoffset, struct page *page)
{
	struct cleancache_tree *pool = cleancache_pools[pool_id];
	struct inode_entry *ientry;
	struct page_entry *pentry;
	u8 *src, *dst;

	if (!pool) {
		pr_warn("wrong pool id %d requested to %s\n",
				pool_id, __func__);
		return -1;
	}

	spin_lock(&pool->lock);
	ientry = find_get_inode_entry(&pool->inodes_root, (void *)&key);
	spin_unlock(&pool->lock);
	if (!ientry) {
		pr_debug("couldn't find the inode from %s. pool id: %d\n",
				__func__, pool_id);
		return -1;
	}

	spin_lock(&ientry->pages_lock);
	pentry = find_get_page_entry(&ientry->page_root, pgoffset);
	spin_unlock(&ientry->pages_lock);
	if (!pentry) {
		pr_warn("couldn't find the page entry from cleancache file\n");
		spin_lock(&pool->lock);
		put_inode_entry(&pool->inodes_root, ientry);
		spin_unlock(&pool->lock);
		return -1;
	}

	src = kmap_atomic(pentry->gpage.page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&ientry->pages_lock);
	put_page_entry(&ientry->page_root, pentry);
	spin_unlock(&ientry->pages_lock);

	spin_lock(&pool->lock);
	put_inode_entry(&pool->inodes_root, ientry);
	spin_unlock(&pool->lock);

	/*
	 * Unlike frontswap, we can remove this entry from LRU because
	 * cleancache is ephemeral
	 */
	spin_lock(&page_lru_lock);
	list_del_init(&pentry->lru_list);
	spin_unlock(&page_lru_lock);

	return 0;
}

void gcma_cleancache_invalidate_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t pgoffset)
{
	struct cleancache_tree *pool = cleancache_pools[pool_id];
	struct inode_entry *ientry;
	struct page_entry *pentry;

	spin_lock(&pool->lock);
	ientry = search_inode_entry(&pool->inodes_root, (void *)&key);
	if (!ientry) {
		pr_warn("failed to search inode from %s\n", __func__);
		spin_unlock(&pool->lock);
		return;
	}

	spin_lock(&ientry->pages_lock);
	pentry = search_page_entry(&ientry->page_root, pgoffset);
	if (!pentry) {
		pr_warn("failed to get entry from %s\n", __func__);
		goto out;
	}

	spin_lock(&page_lru_lock);
	list_del(&pentry->lru_list);
	spin_unlock(&page_lru_lock);

	erase_page_entry(&ientry->page_root, pentry);
	put_page_entry(&ientry->page_root, pentry);

	/* TODO: free file when it's child is empty, too */

out:
	spin_unlock(&ientry->pages_lock);
	spin_unlock(&pool->lock);
}

void gcma_cleancache_invalidate_inode(int pool_id,
					struct cleancache_filekey key)
{
	struct cleancache_tree *pool = cleancache_pools[pool_id];
	struct inode_entry *ientry;
	struct page_entry *pentry, *n;

	spin_lock(&pool->lock);
	ientry = search_inode_entry(&pool->inodes_root, (void *)&key);
	if (!ientry) {
		pr_warn("failed to search inode from %s\n", __func__);
		spin_unlock(&pool->lock);
		return;
	}
	spin_lock(&ientry->pages_lock);
	rbtree_postorder_for_each_entry_safe(pentry, n, &ientry->page_root,
						rbnode) {
		spin_lock(&page_lru_lock);
		list_del(&pentry->lru_list);
		spin_unlock(&page_lru_lock);

		free_page_entry(pentry);
	}
	ientry->page_root = RB_ROOT;
	spin_unlock(&ientry->pages_lock);

	erase_inode_entry(&pool->inodes_root, ientry);
	put_inode_entry(&pool->inodes_root, ientry);
	spin_unlock(&pool->lock);
}

void gcma_cleancache_invalidate_fs(int pool_id)
{
	struct cleancache_tree *pool = cleancache_pools[pool_id];
	struct inode_entry *ientry, *n;
	struct page_entry *pentry, *m;

	if (!pool) {
		pr_warn("wrong pool id %d requested on %s\n",
				pool_id, __func__);
		return;
	}

	spin_lock(&pool->lock);
	rbtree_postorder_for_each_entry_safe(ientry, n, &pool->inodes_root,
						rbnode) {
		spin_lock(&ientry->pages_lock);
		rbtree_postorder_for_each_entry_safe(pentry, m,
						&ientry->page_root, rbnode) {
			spin_lock(&page_lru_lock);
			list_del(&pentry->lru_list);
			spin_unlock(&page_lru_lock);

			free_page_entry(pentry);
		}
		spin_unlock(&ientry->pages_lock);
		free_inode_entry(ientry);
	}
	pool->inodes_root = RB_ROOT;
	spin_unlock(&pool->lock);

	kfree(pool);
	cleancache_pools[pool_id] = NULL;
	return;
}

static int evict_cleancache_pages(int gid, int pages)
{
	struct inode_entry *ientry;
	struct page_entry *pentry, *n;
	int evicted = 0;

	spin_lock(&page_lru_lock);
	list_for_each_entry_safe_reverse(pentry, n, &page_lru_list, lru_list) {
		if (pentry->gpage.gid != gid)
			continue;
		list_del(&pentry->lru_list);

		ientry = pentry->inode;
		spin_lock(&ientry->pages_lock);
		erase_page_entry(&ientry->page_root, pentry);
		put_page_entry(&ientry->page_root, pentry);
		spin_unlock(&ientry->pages_lock);

		if (++evicted >= pages)
			goto out;
	}
out:
	spin_unlock(&page_lru_lock);
	return evicted;
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

	spin_lock_init(&swap_lru_lock);
	INIT_LIST_HEAD(&swap_lru_list);
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

	spin_lock_init(&page_lru_lock);
	INIT_LIST_HEAD(&page_lru_list);
	inode_entry_cache = KMEM_CACHE(inode_entry, 0);
	if (inode_entry_cache == NULL) {
		pr_warn("failed to create inode cache\n");
		return -ENOMEM;
	}

	page_entry_cache = KMEM_CACHE(page_entry, 0);
	if (page_entry_cache == NULL) {
		pr_warn("failed to create page cache\n");
		kmem_cache_destroy(inode_entry_cache);
		return -ENOMEM;
	}

	cleancache_register_ops(&gcma_cleancache_ops);
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
