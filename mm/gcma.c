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
#include <linux/hash.h>

#define DMEM_HASH_BUCKET_BITS	8
#define NR_DMEM_HASH_BUCKETS	(1 << DMEM_HASH_BUCKET_BITS)

#define MAX_CLEANCACHE_FS	16

/* XXX: What's the ideal? */
#define NR_EVICT_BATCH	32

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

/*
 * TODO: Better naming sense than dmem, maybe?
 * Discardable memory is a key-value store which stores easily discardable
 * pages for second-class clients of gcma.
 */

struct dmem_key {
	u8 key[sizeof(pgoff_t) + sizeof(struct cleancache_filekey)];
};

/* TODO: Configurable discard mechanism */
struct dmem_entry {
	struct gcma *gcma;
	struct rb_node rbnode;
	struct dmem_key key;
	struct page *page;
	atomic_t refcount;
};

/* discardable memory hash bucket */
struct dmem_hashbucket {
	struct rb_root rbroot;
	int (*compare)(struct dmem_key *lkey, struct dmem_key *rkey);
	spinlock_t lock;
};

/*
 * Discardable memory pool
 */
struct dmem_pool {
	struct dmem_hashbucket hashbuckets[NR_DMEM_HASH_BUCKETS];
};

static LIST_HEAD(dlru_list);	/* LRU list of dmem */
static spinlock_t dlru_lock;	/* protect dlru_list */
static struct dmem_pool *gcma_dmem_pools[MAX_SWAPFILES + MAX_CLEANCACHE_FS];
static struct kmem_cache *dmem_entry_cache;

/* For statistics */
static atomic_t gcma_fs_stored_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_loaded_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_evicted_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_reclaimed_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_invalidated_pages = ATOMIC_INIT(0);
static atomic_t gcma_fs_invalidated_areas = ATOMIC_INIT(0);

#if 0
static atomic_t gcma_cc_stored_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_loaded_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_load_failed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_evicted_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_reclaimed_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidated_pages = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidated_inodes = ATOMIC_INIT(0);
static atomic_t gcma_cc_invalidated_fses = ATOMIC_INIT(0);
#endif

static unsigned long dmem_evict_lru(unsigned long nr_pages);

static struct dmem_hashbucket *dmem_hashbuck(struct page *page)
{
	return (struct dmem_hashbucket *)page->mapping;
}

static void set_dmem_hashbuck(struct page *page, struct dmem_hashbucket *buck)
{
	page->mapping = (struct address_space *)buck;
}

static struct dmem_entry *dmem_entry(struct page *page)
{
	return (struct dmem_entry *)page->index;
}

static void set_dmem_entry(struct page *page, struct dmem_entry *entry)
{
	page->index = (pgoff_t)entry;
}

/*
 * Flags for status of a page in gcma
 *
 * GF_SWAP_LRU
 * The page is being used for frontswap and hang on frontswap LRU list.
 * It can be drained for contiguous memory allocation anytime.
 * Protected by dlru_lock.
 *
 * GF_RECLAIMING
 * The page is being draining for contiguous memory allocation.
 * Frontswap guests should not use it.
 * Protected by dlru_lock.
 *
 * GF_ISOLATED
 * The page is isolated for contiguous memory allocation.
 * GCMA guests can use the page safely while frontswap guests should not.
 * Protected by gcma->lock.
 */
enum gpage_flags {
	GF_SWAP_LRU = 0x1,
	GF_RECLAIMING = 0x2,
	GF_ISOLATED = 0x4,
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

/* Caller should hold dlru_lock */
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
static int dmem_insert_entry(struct dmem_hashbucket *bucket,
		struct dmem_entry *entry,
		struct dmem_entry **dupentry)
{
	struct rb_node **link = &bucket->rbroot.rb_node, *parent = NULL;
	struct dmem_entry *ientry;
	int cmp;

	while (*link) {
		parent = *link;
		ientry = rb_entry(parent, struct dmem_entry, rbnode);
		cmp = bucket->compare(&entry->key, &ientry->key);
		if (cmp < 0)
			link = &(*link)->rb_left;
		else if (cmp > 0)
			link = &(*link)->rb_right;
		else {
			*dupentry = ientry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, &bucket->rbroot);
	return 0;
}

static void dmem_erase_entry(struct dmem_hashbucket *bucket,
		struct dmem_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, &bucket->rbroot);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

static struct dmem_entry *dmem_search_entry(struct dmem_hashbucket *bucket,
		struct dmem_key *key)
{
	struct rb_node *node = bucket->rbroot.rb_node;
	struct dmem_entry *entry;
	int cmp;

	while (node) {
		entry = rb_entry(node, struct dmem_entry, rbnode);
		cmp = bucket->compare(key, &entry->key);
		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

/* Allocates a page from gcma areas using round-robin way */
static struct page *dmem_alloc_page(struct gcma **res_gcma)
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
	 * discardable memory and try allocation again */
	if (dmem_evict_lru(NR_EVICT_BATCH))
		goto retry;

got:
	*res_gcma = gcma;
	return page;
}

static void dmem_free_entry(struct dmem_entry *entry)
{
	gcma_free_page(entry->gcma, entry->page);
	kmem_cache_free(dmem_entry_cache, entry);
}

/* Caller should hold hashbucket spinlock */
static void dmem_get(struct dmem_entry *entry)
{
	atomic_inc(&entry->refcount);
}

/*
 * Caller should hold hashbucket spinlock and dlru_lock.
 * Remove from the bucket and free it, if nobody reference the entry.
 */
static void dmem_put(struct dmem_hashbucket *buck,
				struct dmem_entry *entry)
{
	int refcount = atomic_dec_return(&entry->refcount);

	BUG_ON(refcount < 0);

	if (refcount == 0) {
		struct page *page = entry->page;

		dmem_erase_entry(buck, entry);
		list_del(&page->lru);

		dmem_free_entry(entry);
	}
}

/*
 * dmem_evict_lru - evict @nr_pages LRU dmem pages
 *
 * @nr_pages	number of LRU pages to be evicted
 *
 * Returns number of successfully evicted pages
 */
static unsigned long dmem_evict_lru(unsigned long nr_pages)
{
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;
	struct page *page, *n;
	unsigned long evicted = 0;
	struct dmem_key key;
	LIST_HEAD(free_pages);

	spin_lock(&dlru_lock);
	list_for_each_entry_safe_reverse(page, n, &dlru_list, lru) {
		entry = dmem_entry(page);

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
	spin_unlock(&dlru_lock);

	list_for_each_entry_safe(page, n, &free_pages, lru) {
		buck = dmem_hashbuck(page);
		entry = dmem_entry(page);

		spin_lock(&buck->lock);
		spin_lock(&dlru_lock);
		/* drop refcount increased by above loop */
		memcpy(&key, &entry->key, sizeof(struct dmem_key));
		dmem_put(buck, entry);
		/* free entry if the entry is still in tree */
		if (dmem_search_entry(buck, &key))
			dmem_put(buck, entry);
		spin_unlock(&dlru_lock);
		spin_unlock(&buck->lock);
	}

	atomic_add(evicted, &gcma_fs_evicted_pages);
	return evicted;
}

/* Caller should hold bucket spinlock */
static struct dmem_entry *dmem_find_get_entry(struct dmem_hashbucket *buck,
						struct dmem_key *key)
{
	struct dmem_entry *entry;

	assert_spin_locked(&buck->lock);
	entry = dmem_search_entry(buck, key);
	if (entry)
		dmem_get(entry);

	return entry;
}

unsigned int dmem_hash(struct dmem_key *key)
{
	unsigned long *k = (unsigned long *)&key->key;
	return hash_long(k[0] ^ k[1] ^ k[2], DMEM_HASH_BUCKET_BITS);
}

static struct dmem_hashbucket *dmem_find_hashbucket(struct dmem_pool *pool,
							struct dmem_key *key)
{
	return &pool->hashbuckets[dmem_hash(key)];
}

static void frontswap_set_dmem_key(pgoff_t *offset, struct dmem_key *key)
{
	memcpy(key, offset, sizeof(pgoff_t));
}

static int frontswap_cmp(struct dmem_key *lkey, struct dmem_key *rkey)
{
	return memcmp(lkey, rkey, sizeof(pgoff_t));
}

void gcma_frontswap_init(unsigned type)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	int i;

	pool = kzalloc(sizeof(struct dmem_pool), GFP_KERNEL);
	if (!pool) {
		pr_warn("failed to alloc dmem pool for frontswap type %d\n",
				type);
		return;
	}

	for (i = 0; i < NR_DMEM_HASH_BUCKETS; i++) {
		buck = &pool->hashbuckets[i];
		buck->rbroot = RB_ROOT;
		buck->compare = frontswap_cmp;
		spin_lock_init(&buck->lock);
	}

	gcma_dmem_pools[type] = pool;
}

int gcma_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct dmem_entry *entry, *dupentry;
	struct gcma *gcma;
	struct page *gcma_page = NULL;
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
      
	u8 *src, *dst;
	int ret;
 
	pool = gcma_dmem_pools[type];
	if (!pool) {
		WARN(1, "dmem pool for frontswap type %d is not exist\n",
				type);
		return -ENODEV;
	}

	gcma_page = dmem_alloc_page(&gcma);
	if (!gcma_page)
		return -ENOMEM;

	entry = kmem_cache_alloc(dmem_entry_cache, GFP_NOIO);
	if (!entry) {
		spin_lock(&dlru_lock);
		gcma_free_page(gcma, gcma_page);
		spin_unlock(&dlru_lock);
		return -ENOMEM;
	}

	entry->gcma = gcma;
	entry->page = gcma_page;
	frontswap_set_dmem_key(&offset, &entry->key);
	atomic_set(&entry->refcount, 1);
	RB_CLEAR_NODE(&entry->rbnode);

	buck = dmem_find_hashbucket(pool, &entry->key);
	set_dmem_hashbuck(gcma_page, buck);
	set_dmem_entry(gcma_page, entry);

	/* copy from orig data to gcma-page */
	src = kmap_atomic(page);
	dst = kmap_atomic(gcma_page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&buck->lock);
	do {
		/*
		 * Though this duplication scenario may happen rarely by
		 * race of swap layer, we handle this case here rather
		 * than fix swap layer because handling the possibility of
		 * duplicates is part of the tmem ABI.
		 */
		ret = dmem_insert_entry(buck, entry, &dupentry);
		if (ret == -EEXIST) {
			dmem_erase_entry(buck, dupentry);
			spin_lock(&dlru_lock);
			dmem_put(buck, dupentry);
			spin_unlock(&dlru_lock);
		}
	} while (ret == -EEXIST);

	spin_lock(&dlru_lock);
	set_gpage_flag(gcma_page, GF_SWAP_LRU);
	list_add(&gcma_page->lru, &dlru_list);
	spin_unlock(&dlru_lock);
	spin_unlock(&buck->lock);

	atomic_inc(&gcma_fs_stored_pages);
	return ret;
}

/*
 * Returns 0 if success,
 * Returns non-zero if failed.
 */
int gcma_frontswap_load(unsigned type, pgoff_t offset,
			       struct page *page)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;
	struct dmem_key key;
	struct page *gcma_page;
	u8 *src, *dst;

	pool = gcma_dmem_pools[type];
	if (!pool) {
		WARN(1, "dmem pool for frontswap type %d not exist\n", type);
		return -1;
	}

	frontswap_set_dmem_key(&offset, &key);
	buck = dmem_find_hashbucket(pool, &key);

	spin_lock(&buck->lock);
	entry = dmem_find_get_entry(buck, &key);
	spin_unlock(&buck->lock);
	if (!entry)
		return -1;

	gcma_page = entry->page;
	src = kmap_atomic(gcma_page);
	dst = kmap_atomic(page);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_atomic(src);
	kunmap_atomic(dst);

	spin_lock(&buck->lock);
	spin_lock(&dlru_lock);
	if (likely(gpage_flag(gcma_page, GF_SWAP_LRU)))
		list_move(&gcma_page->lru, &dlru_list);
	dmem_put(buck, entry);
	spin_unlock(&dlru_lock);
	spin_unlock(&buck->lock);

	atomic_inc(&gcma_fs_loaded_pages);
	return 0;
}

void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_key key;
	struct dmem_entry *entry;

	pool = gcma_dmem_pools[type];
	frontswap_set_dmem_key(&offset, &key);
	buck = dmem_find_hashbucket(pool, &key);

	spin_lock(&buck->lock);
	entry = dmem_search_entry(buck, &key);
	if (!entry) {
		spin_unlock(&buck->lock);
		return;
	}

	spin_lock(&dlru_lock);
	dmem_put(buck, entry);
	spin_unlock(&dlru_lock);
	spin_unlock(&buck->lock);

	atomic_inc(&gcma_fs_invalidated_pages);
}

void gcma_frontswap_invalidate_area(unsigned type)
{
	struct dmem_pool *pool;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry, *n;
	int i;

	pool = gcma_dmem_pools[type];
	if (!pool)
		return;

	for (i = 0; i < NR_DMEM_HASH_BUCKETS; i++) {
		buck = &pool->hashbuckets[i];
		spin_lock(&buck->lock);
		rbtree_postorder_for_each_entry_safe(entry, n, &buck->rbroot,
				rbnode) {
			/* TODO: unnecessary erase? */
			dmem_erase_entry(buck, entry);
			spin_lock(&dlru_lock);
			dmem_put(buck, entry);
			spin_unlock(&dlru_lock);
		}
		buck->rbroot = RB_ROOT;
		spin_unlock(&buck->lock);
	}

	kfree(pool);
	gcma_dmem_pools[type] = NULL;

	atomic_inc(&gcma_fs_invalidated_areas);
}

static struct frontswap_ops gcma_frontswap_ops = {
	.init = gcma_frontswap_init,
	.store = gcma_frontswap_store,
	.load = gcma_frontswap_load,
	.invalidate_page = gcma_frontswap_invalidate_page,
	.invalidate_area = gcma_frontswap_invalidate_area
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

/* Returns positive pool id or negative error code */
int gcma_cleancache_init_fs(size_t pagesize)
{
	return -1;
}

int gcma_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	return -1;
}

int gcma_cleancache_get_page(int pool_id, struct cleancache_filekey key,
				pgoff_t pgoffset, struct page *page)
{
	return -1;
}

void gcma_cleancache_put_page(int pool_id, struct cleancache_filekey key,
				pgoff_t pgoffset, struct page *page)
{
}

void gcma_cleancache_invalidate_page(int pool_id, struct cleancache_filekey key,
					pgoff_t pgoffset)
{
}

void gcma_cleancache_invalidate_inode(int pool_id,
					struct cleancache_filekey key)
{
}

void gcma_cleancache_invalidate_fs(int pool_id)
{
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
	LIST_HEAD(free_pages);
	struct page *page, *n;
	struct dmem_hashbucket *buck;
	struct dmem_entry *entry;
	struct dmem_key dmem_key;
	unsigned long offset;
	unsigned long *bitmap;
	unsigned long pfn;
	unsigned long orig_start = start_pfn;

retry:
	for (pfn = start_pfn; pfn < start_pfn + size; pfn++) {
		spin_lock(&gcma->lock);

		offset = pfn - gcma->base_pfn;
		bitmap = gcma->bitmap + offset / BITS_PER_LONG;
		page = pfn_to_page(pfn);

		if (!test_bit(offset % BITS_PER_LONG, bitmap)) {
			/* set a bit to prevent allocation for dmem */
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

		spin_lock(&dlru_lock);
		spin_lock(&gcma->lock);

		/* Avoid allocation from other threads */
		set_gpage_flag(page, GF_RECLAIMING);

		/*
		 * The page is in LRU and being used by someone. Remove from
		 * LRU and ready to put the swap slot soon.
		 */
		if (gpage_flag(page, GF_SWAP_LRU)) {
		       entry = dmem_entry(page);
		       if (atomic_inc_not_zero(&entry->refcount)) {
				clear_gpage_flag(page, GF_SWAP_LRU);
				list_move(&page->lru, &free_pages);
				atomic_inc(&gcma_fs_reclaimed_pages);
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
		spin_unlock(&dlru_lock);
	}

	/*
	 * Since we increased refcount of the page above, we can access
	 * dmem_entry with safe
	 */
	list_for_each_entry_safe(page, n, &free_pages, lru) {
		buck = dmem_hashbuck(page);
		entry = dmem_entry(page);

		spin_lock(&buck->lock);
		spin_lock(&dlru_lock);
		/* drop refcount increased by above loop */
		memcpy(&dmem_key, &entry->key, sizeof(struct dmem_key));
		dmem_put(buck, entry);
		/* free entry if the entry is still in tree */
		if (dmem_search_entry(buck, &dmem_key))
			dmem_put(buck, entry);
		spin_unlock(&dlru_lock);
		spin_unlock(&buck->lock);
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

	debugfs_create_atomic_t("fs_stored_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_stored_pages);
	debugfs_create_atomic_t("fs_loaded_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_loaded_pages);
	debugfs_create_atomic_t("fs_evicted_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_evicted_pages);
	debugfs_create_atomic_t("fs_reclaimed_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_reclaimed_pages);
	debugfs_create_atomic_t("fs_invalidated_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_invalidated_pages);
	debugfs_create_atomic_t("fs_invalidated_areas", S_IRUGO,
			gcma_debugfs_root, &gcma_fs_invalidated_areas);

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

	spin_lock_init(&dlru_lock);
	dmem_entry_cache = KMEM_CACHE(dmem_entry, 0);
	if (dmem_entry_cache == NULL)
		return -ENOMEM;

	/*
	 * By writethough mode, GCMA could discard all of pages in an instant
	 * instead of slow writing pages out to the swap device.
	 */
	frontswap_writethrough(true);
	frontswap_register_ops(&gcma_frontswap_ops);

	cleancache_register_ops(&gcma_cleancache_ops);

	gcma_debugfs_init();
	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
