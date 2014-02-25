/*
 * gcma.c - gcma driver file
 *
 * gcma is a contiguous memory allocator which guarantees success and upper
 * time bound of allocation.
 * It uses frontswap/cleancache to manage contiguous memory region.
 *
 * It forked from zswap, initially.
 *
 * Copyright (C) 2014  Minchan Kim <minchan@kernel.org>,
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
#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/frontswap.h>
#include <linux/rbtree.h>
#include <linux/swap.h>
#include <linux/crypto.h>
#include <linux/mempool.h>
#include <linux/zbud.h>

#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/swapops.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>

#include "gcma.h"

/*********************************
* statistics
**********************************/
/* Number of memory pages used by the compressed pool */
static u64 gcma_pool_pages;
/* The number of compressed pages currently stored in gcma */
static atomic_t gcma_stored_pages = ATOMIC_INIT(0);

/*
 * The statistics below are not protected from concurrent access for
 * performance reasons so they may not be a 100% accurate.  However,
 * they do provide useful information on roughly how many times a
 * certain event is occurring.
*/

/* Pool limit was hit (see gcma_max_pool_percent) */
static u64 gcma_pool_limit_hit;
/* Pages written back when pool limit was reached */
static u64 gcma_written_back_pages;
/* Store failed due to a reclaim failure after pool limit was reached */
static u64 gcma_reject_reclaim_fail;
/* Compressed page was too big for the allocator to (optimally) store */
static u64 gcma_reject_compress_poor;
/* Store failed because underlying allocator could not get memory */
static u64 gcma_reject_alloc_fail;
/* Store failed because the entry metadata could not be allocated (rare) */
static u64 gcma_reject_kmemcache_fail;
/* Duplicate store was encountered (rare) */
static u64 gcma_duplicate_entry;

/*********************************
* tunables
**********************************/
/* Order of total contiguous memory region size */
static uint gcma_tcmr_order __read_mostly;
module_param_named(tcmr_order, gcma_tcmr_order, uint, 0);

/* Enable/disable gcma (disabled by default, fixed at boot for now) */
static bool gcma_enabled __read_mostly;
module_param_named(enabled, gcma_enabled, bool, 0);

/* Compressor to be used by gcma (fixed at boot for now) */
#define GCMA_COMPRESSOR_DEFAULT "lzo"
static char *gcma_compressor = GCMA_COMPRESSOR_DEFAULT;
module_param_named(compressor, gcma_compressor, charp, 0);

/* The maximum percentage of memory that the compressed pool can occupy */
static unsigned int gcma_max_pool_percent = 20;
module_param_named(max_pool_percent,
			gcma_max_pool_percent, uint, 0644);

/*********************************
* compression functions
**********************************/
/* per-cpu compression transforms */
static struct crypto_comp * __percpu *gcma_comp_pcpu_tfms;

enum comp_op {
	GCMA_COMPOP_COMPRESS,
	GCMA_COMPOP_DECOMPRESS
};

static int gcma_comp_op(enum comp_op op, const u8 *src, unsigned int slen,
				u8 *dst, unsigned int *dlen)
{
	struct crypto_comp *tfm;
	int ret;

	tfm = *per_cpu_ptr(gcma_comp_pcpu_tfms, get_cpu());
	switch (op) {
	case GCMA_COMPOP_COMPRESS:
		ret = crypto_comp_compress(tfm, src, slen, dst, dlen);
		break;
	case GCMA_COMPOP_DECOMPRESS:
		ret = crypto_comp_decompress(tfm, src, slen, dst, dlen);
		break;
	default:
		ret = -EINVAL;
	}

	put_cpu();
	return ret;
}

static int __init gcma_comp_init(void)
{
	if (!crypto_has_comp(gcma_compressor, 0, 0)) {
		pr_info("%s compressor not available\n", gcma_compressor);
		/* fall back to default compressor */
		gcma_compressor = GCMA_COMPRESSOR_DEFAULT;
		if (!crypto_has_comp(gcma_compressor, 0, 0))
			/* can't even load the default compressor */
			return -ENODEV;
	}
	pr_info("using %s compressor\n", gcma_compressor);

	/* alloc percpu transforms */
	gcma_comp_pcpu_tfms = alloc_percpu(struct crypto_comp *);
	if (!gcma_comp_pcpu_tfms)
		return -ENOMEM;
	return 0;
}

static void gcma_comp_exit(void)
{
	/* free percpu transforms */
	if (gcma_comp_pcpu_tfms)
		free_percpu(gcma_comp_pcpu_tfms);
}

/*********************************
* data structures
**********************************/
/*
 * struct gcma_entry
 *
 * This structure contains the metadata for tracking a single compressed
 * page within gcma.
 *
 * rbnode - links the entry into red-black tree for the appropriate swap type
 * refcount - the number of outstanding reference to the entry. This is needed
 *            to protect against premature freeing of the entry by code
 *            concurent calls to load, invalidate, and writeback.  The lock
 *            for the gcma_tree structure that contains the entry must
 *            be held while changing the refcount.  Since the lock must
 *            be held, there is no reason to also make refcount atomic.
 * offset - the swap offset for the entry.  Index into the red-black tree.
 * handle - zsmalloc allocation handle that stores the compressed page data
 * length - the length in bytes of the compressed page data.  Needed during
 *           decompression
 */
struct gcma_entry {
	struct rb_node rbnode;
	pgoff_t offset;
	int refcount;
	unsigned int length;
	unsigned long handle;
};

struct gcma_header {
	swp_entry_t swpentry;
};

/*
 * The tree lock in the gcma_tree struct protects a few things:
 * - the rbtree
 * - the refcount field of each entry in the tree
 */
struct gcma_tree {
	struct rb_root rbroot;
	spinlock_t lock;
	struct zbud_pool *pool;
};

static struct gcma_tree *gcma_trees[MAX_SWAPFILES];

/*********************************
* gcma entry functions
**********************************/
static struct kmem_cache *gcma_entry_cache;
static struct page *gcma_tcmr;
static spinlock_t cma_lock;

/**
 * gcma_secure_tcmr - secure enough amount of contiguous memory as gcma's
 * property to use it later when contig memory allocation requirement came.
 */
static int gcma_secure_tcmr(void)
{
	spin_lock_init(&cma_lock);
	/* TODO: alloc_pages alloc only 2**MAX_ORDER pages.
	 * Should use another mechanism */
	gcma_tcmr = alloc_pages(GFP_KERNEL, gcma_tcmr_order);
	pr_debug("secured 2^%d pages for tcmr\n", gcma_tcmr_order);
	return gcma_tcmr == NULL;
}

static int gcma_entry_cache_create(void)
{
	gcma_entry_cache = KMEM_CACHE(gcma_entry, 0);
	return (gcma_entry_cache == NULL);
}

static void gcma_entry_cache_destory(void)
{
	kmem_cache_destroy(gcma_entry_cache);
}

static struct gcma_entry *gcma_entry_cache_alloc(gfp_t gfp)
{
	struct gcma_entry *entry;
	entry = kmem_cache_alloc(gcma_entry_cache, gfp);
	if (!entry)
		return NULL;
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);
	return entry;
}

static void gcma_entry_cache_free(struct gcma_entry *entry)
{
	kmem_cache_free(gcma_entry_cache, entry);
}

/*********************************
* gcma interfaces
**********************************/
static int cm_allocated;
struct page *gcma_alloc(int count)
{
	struct page *ret;
	spin_lock(&cma_lock);
	ret = gcma_tcmr + cm_allocated;
	cm_allocated += count;
	spin_unlock(&cma_lock);
	return ret;
}

int gcma_free(struct page *page)
{
	return 0;
}

/*********************************
* rbtree functions
**********************************/
static struct gcma_entry *gcma_rb_search(struct rb_root *root, pgoff_t offset)
{
	struct rb_node *node = root->rb_node;
	struct gcma_entry *entry;

	while (node) {
		entry = rb_entry(node, struct gcma_entry, rbnode);
		if (entry->offset > offset)
			node = node->rb_left;
		else if (entry->offset < offset)
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
static int gcma_rb_insert(struct rb_root *root, struct gcma_entry *entry,
			struct gcma_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct gcma_entry *myentry;

	while (*link) {
		parent = *link;
		myentry = rb_entry(parent, struct gcma_entry, rbnode);
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

static void gcma_rb_erase(struct rb_root *root, struct gcma_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
	}
}

/*
 * Carries out the common pattern of freeing and entry's zsmalloc allocation,
 * freeing the entry itself, and decrementing the number of stored pages.
 */
static void gcma_free_entry(struct gcma_tree *tree,
			struct gcma_entry *entry)
{
	zbud_free(tree->pool, entry->handle);
	gcma_entry_cache_free(entry);
	atomic_dec(&gcma_stored_pages);
	gcma_pool_pages = zbud_get_pool_size(tree->pool);
}

/* caller must hold the tree lock */
static void gcma_entry_get(struct gcma_entry *entry)
{
	entry->refcount++;
}

/* caller must hold the tree lock
* remove from the tree and free it, if nobody reference the entry
*/
static void gcma_entry_put(struct gcma_tree *tree,
			struct gcma_entry *entry)
{
	int refcount = --entry->refcount;

	BUG_ON(refcount < 0);
	if (refcount == 0) {
		gcma_rb_erase(&tree->rbroot, entry);
		gcma_free_entry(tree, entry);
	}
}

/* caller must hold the tree lock */
static struct gcma_entry *gcma_entry_find_get(struct rb_root *root,
				pgoff_t offset)
{
	struct gcma_entry *entry = NULL;

	entry = gcma_rb_search(root, offset);
	if (entry)
		gcma_entry_get(entry);

	return entry;
}

/*********************************
* per-cpu code
**********************************/
static DEFINE_PER_CPU(u8 *, gcma_dstmem);

static int __gcma_cpu_notifier(unsigned long action, unsigned long cpu)
{
	struct crypto_comp *tfm;
	u8 *dst;

	switch (action) {
	case CPU_UP_PREPARE:
		tfm = crypto_alloc_comp(gcma_compressor, 0, 0);
		if (IS_ERR(tfm)) {
			pr_err("can't allocate compressor transform\n");
			return NOTIFY_BAD;
		}
		*per_cpu_ptr(gcma_comp_pcpu_tfms, cpu) = tfm;
		dst = kmalloc(PAGE_SIZE * 2, GFP_KERNEL);
		if (!dst) {
			pr_err("can't allocate compressor buffer\n");
			crypto_free_comp(tfm);
			*per_cpu_ptr(gcma_comp_pcpu_tfms, cpu) = NULL;
			return NOTIFY_BAD;
		}
		per_cpu(gcma_dstmem, cpu) = dst;
		break;
	case CPU_DEAD:
	case CPU_UP_CANCELED:
		tfm = *per_cpu_ptr(gcma_comp_pcpu_tfms, cpu);
		if (tfm) {
			crypto_free_comp(tfm);
			*per_cpu_ptr(gcma_comp_pcpu_tfms, cpu) = NULL;
		}
		dst = per_cpu(gcma_dstmem, cpu);
		kfree(dst);
		per_cpu(gcma_dstmem, cpu) = NULL;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int gcma_cpu_notifier(struct notifier_block *nb,
				unsigned long action, void *pcpu)
{
	unsigned long cpu = (unsigned long)pcpu;
	return __gcma_cpu_notifier(action, cpu);
}

static struct notifier_block gcma_cpu_notifier_block = {
	.notifier_call = gcma_cpu_notifier
};

static int gcma_cpu_init(void)
{
	unsigned long cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		if (__gcma_cpu_notifier(CPU_UP_PREPARE, cpu) != NOTIFY_OK)
			goto cleanup;
	register_cpu_notifier(&gcma_cpu_notifier_block);
	put_online_cpus();
	return 0;

cleanup:
	for_each_online_cpu(cpu)
		__gcma_cpu_notifier(CPU_UP_CANCELED, cpu);
	put_online_cpus();
	return -ENOMEM;
}

/*********************************
* helpers
**********************************/
static bool gcma_is_full(void)
{
	return (totalram_pages * gcma_max_pool_percent / 100 <
		gcma_pool_pages);
}

/*********************************
* writeback code
**********************************/
/* return enum for gcma_get_swap_cache_page */
enum gcma_get_swap_ret {
	GCMA_SWAPCACHE_NEW,
	GCMA_SWAPCACHE_EXIST,
	GCMA_SWAPCACHE_FAIL,
};

/*
 * gcma_get_swap_cache_page
 *
 * This is an adaption of read_swap_cache_async()
 *
 * This function tries to find a page with the given swap entry
 * in the swapper_space address space (the swap cache).  If the page
 * is found, it is returned in retpage.  Otherwise, a page is allocated,
 * added to the swap cache, and returned in retpage.
 *
 * If success, the swap cache page is returned in retpage
 * Returns GCMA_SWAPCACHE_EXIST if page was already in the swap cache
 * Returns GCMA_SWAPCACHE_NEW if the new page needs to be populated,
 *     the new page is added to swapcache and locked
 * Returns GCMA_SWAPCACHE_FAIL on error
 */
static int gcma_get_swap_cache_page(swp_entry_t entry,
				struct page **retpage)
{
	struct page *found_page, *new_page = NULL;
	struct address_space *swapper_space = swap_address_space(entry);
	int err;

	*retpage = NULL;
	do {
		/*
		 * First check the swap cache.  Since this is normally
		 * called after lookup_swap_cache() failed, re-calling
		 * that would confuse statistics.
		 */
		found_page = find_get_page(swapper_space, entry.val);
		if (found_page)
			break;

		/*
		 * Get a new page to read into from swap.
		 */
		if (!new_page) {
			new_page = alloc_page(GFP_KERNEL);
			if (!new_page)
				break; /* Out of memory */
		}

		/*
		 * call radix_tree_preload() while we can wait.
		 */
		err = radix_tree_preload(GFP_KERNEL);
		if (err)
			break;

		/*
		 * Swap entry may have been freed since our caller observed it.
		 */
		err = swapcache_prepare(entry);
		if (err == -EEXIST) { /* seems racy */
			radix_tree_preload_end();
			continue;
		}
		if (err) { /* swp entry is obsolete ? */
			radix_tree_preload_end();
			break;
		}

		/* May fail (-ENOMEM) if radix-tree node allocation failed. */
		__set_page_locked(new_page);
		SetPageSwapBacked(new_page);
		err = __add_to_swap_cache(new_page, entry);
		if (likely(!err)) {
			radix_tree_preload_end();
			lru_cache_add_anon(new_page);
			*retpage = new_page;
			return GCMA_SWAPCACHE_NEW;
		}
		radix_tree_preload_end();
		ClearPageSwapBacked(new_page);
		__clear_page_locked(new_page);
		/*
		 * add_to_swap_cache() doesn't return -EEXIST, so we can safely
		 * clear SWAP_HAS_CACHE flag.
		 */
		swapcache_free(entry, NULL);
	} while (err != -ENOMEM);

	if (new_page)
		page_cache_release(new_page);
	if (!found_page)
		return GCMA_SWAPCACHE_FAIL;
	*retpage = found_page;
	return GCMA_SWAPCACHE_EXIST;
}

/*
 * Attempts to free an entry by adding a page to the swap cache,
 * decompressing the entry data into the page, and issuing a
 * bio write to write the page back to the swap device.
 *
 * This can be thought of as a "resumed writeback" of the page
 * to the swap device.  We are basically resuming the same swap
 * writeback path that was intercepted with the frontswap_store()
 * in the first place.  After the page has been decompressed into
 * the swap cache, the compressed version stored by gcma can be
 * freed.
 */
static int gcma_writeback_entry(struct zbud_pool *pool, unsigned long handle)
{
	struct gcma_header *zhdr;
	swp_entry_t swpentry;
	struct gcma_tree *tree;
	pgoff_t offset;
	struct gcma_entry *entry;
	struct page *page;
	u8 *src, *dst;
	unsigned int dlen;
	int ret;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
	};

	/* extract swpentry from data */
	zhdr = zbud_map(pool, handle);
	swpentry = zhdr->swpentry; /* here */
	zbud_unmap(pool, handle);
	tree = gcma_trees[swp_type(swpentry)];
	offset = swp_offset(swpentry);
	BUG_ON(pool != tree->pool);

	/* find and ref gcma entry */
	spin_lock(&tree->lock);
	entry = gcma_entry_find_get(&tree->rbroot, offset);
	if (!entry) {
		/* entry was invalidated */
		spin_unlock(&tree->lock);
		return 0;
	}
	spin_unlock(&tree->lock);
	BUG_ON(offset != entry->offset);

	/* try to allocate swap cache page */
	switch (gcma_get_swap_cache_page(swpentry, &page)) {
	case GCMA_SWAPCACHE_FAIL: /* no memory or invalidate happened */
		ret = -ENOMEM;
		goto fail;

	case GCMA_SWAPCACHE_EXIST:
		/* page is already in the swap cache, ignore for now */
		page_cache_release(page);
		ret = -EEXIST;
		goto fail;

	case GCMA_SWAPCACHE_NEW: /* page is locked */
		/* decompress */
		dlen = PAGE_SIZE;
		src = (u8 *)zbud_map(tree->pool, entry->handle) +
			sizeof(struct gcma_header);
		dst = kmap_atomic(page);
		ret = gcma_comp_op(GCMA_COMPOP_DECOMPRESS, src,
				entry->length, dst, &dlen);
		kunmap_atomic(dst);
		zbud_unmap(tree->pool, entry->handle);
		BUG_ON(ret);
		BUG_ON(dlen != PAGE_SIZE);

		/* page is up to date */
		SetPageUptodate(page);
	}

	/* move it to the tail of the inactive list after end_writeback */
	SetPageReclaim(page);

	/* start writeback */
	__swap_writepage(page, &wbc, end_swap_bio_write);
	page_cache_release(page);
	gcma_written_back_pages++;

	spin_lock(&tree->lock);
	/* drop local reference */
	gcma_entry_put(tree, entry);

	/*
	* There are two possible situations for entry here:
	* (1) refcount is 1(normal case),  entry is valid and on the tree
	* (2) refcount is 0, entry is freed and not on the tree
	*     because invalidate happened during writeback
	*  search the tree and free the entry if find entry
	*/
	if (entry == gcma_rb_search(&tree->rbroot, offset))
		gcma_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	goto end;

	/*
	* if we get here due to GCMA_SWAPCACHE_EXIST
	* a load may happening concurrently
	* it is safe and okay to not free the entry
	* if we free the entry in the following put
	* it it either okay to return !0
	*/
fail:
	spin_lock(&tree->lock);
	gcma_entry_put(tree, entry);
	spin_unlock(&tree->lock);

end:
	return ret;
}

/*********************************
* frontswap hooks
**********************************/
/* attempts to compress and store an single page */
static int gcma_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct gcma_tree *tree = gcma_trees[type];
	struct gcma_entry *entry, *dupentry;
	int ret;
	unsigned int dlen = PAGE_SIZE, len;
	unsigned long handle;
	char *buf;
	u8 *src, *dst;
	struct gcma_header *zhdr;

	if (!tree) {
		ret = -ENODEV;
		goto reject;
	}

	/* reclaim space if needed */
	if (gcma_is_full()) {
		gcma_pool_limit_hit++;
		if (zbud_reclaim_page(tree->pool, 8)) {
			gcma_reject_reclaim_fail++;
			ret = -ENOMEM;
			goto reject;
		}
	}

	/* allocate entry */
	entry = gcma_entry_cache_alloc(GFP_KERNEL);
	if (!entry) {
		gcma_reject_kmemcache_fail++;
		ret = -ENOMEM;
		goto reject;
	}

	/* compress */
	dst = get_cpu_var(gcma_dstmem);
	src = kmap_atomic(page);
	ret = gcma_comp_op(GCMA_COMPOP_COMPRESS, src, PAGE_SIZE, dst, &dlen);
	kunmap_atomic(src);
	if (ret) {
		ret = -EINVAL;
		goto freepage;
	}

	/* store */
	len = dlen + sizeof(struct gcma_header);
	ret = zbud_alloc(tree->pool, len, __GFP_NORETRY | __GFP_NOWARN,
		&handle);
	if (ret == -ENOSPC) {
		gcma_reject_compress_poor++;
		goto freepage;
	}
	if (ret) {
		gcma_reject_alloc_fail++;
		goto freepage;
	}
	zhdr = zbud_map(tree->pool, handle);
	zhdr->swpentry = swp_entry(type, offset);
	buf = (u8 *)(zhdr + 1);
	memcpy(buf, dst, dlen);
	zbud_unmap(tree->pool, handle);
	put_cpu_var(gcma_dstmem);

	/* populate entry */
	entry->offset = offset;
	entry->handle = handle;
	entry->length = dlen;

	/* map */
	spin_lock(&tree->lock);
	do {
		ret = gcma_rb_insert(&tree->rbroot, entry, &dupentry);
		if (ret == -EEXIST) {
			gcma_duplicate_entry++;
			/* remove from rbtree */
			gcma_rb_erase(&tree->rbroot, dupentry);
			gcma_entry_put(tree, dupentry);
		}
	} while (ret == -EEXIST);
	spin_unlock(&tree->lock);

	/* update stats */
	atomic_inc(&gcma_stored_pages);
	gcma_pool_pages = zbud_get_pool_size(tree->pool);

	return 0;

freepage:
	put_cpu_var(gcma_dstmem);
	gcma_entry_cache_free(entry);
reject:
	return ret;
}

/*
 * returns 0 if the page was successfully decompressed
 * return -1 on entry not found or error
*/
static int gcma_frontswap_load(unsigned type, pgoff_t offset,
				struct page *page)
{
	struct gcma_tree *tree = gcma_trees[type];
	struct gcma_entry *entry;
	u8 *src, *dst;
	unsigned int dlen;
	int ret;

	/* find */
	spin_lock(&tree->lock);
	entry = gcma_entry_find_get(&tree->rbroot, offset);
	if (!entry) {
		/* entry was written back */
		spin_unlock(&tree->lock);
		return -1;
	}
	spin_unlock(&tree->lock);

	/* decompress */
	dlen = PAGE_SIZE;
	src = (u8 *)zbud_map(tree->pool, entry->handle) +
			sizeof(struct gcma_header);
	dst = kmap_atomic(page);
	ret = gcma_comp_op(GCMA_COMPOP_DECOMPRESS, src, entry->length,
		dst, &dlen);
	kunmap_atomic(dst);
	zbud_unmap(tree->pool, entry->handle);
	BUG_ON(ret);

	spin_lock(&tree->lock);
	gcma_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	return 0;
}

/* frees an entry in gcma */
static void gcma_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	struct gcma_tree *tree = gcma_trees[type];
	struct gcma_entry *entry;

	/* find */
	spin_lock(&tree->lock);
	entry = gcma_rb_search(&tree->rbroot, offset);
	if (!entry) {
		/* entry was written back */
		spin_unlock(&tree->lock);
		return;
	}

	/* remove from rbtree */
	gcma_rb_erase(&tree->rbroot, entry);

	/* drop the initial reference from entry creation */
	gcma_entry_put(tree, entry);

	spin_unlock(&tree->lock);
}

/* frees all gcma entries for the given swap type */
static void gcma_frontswap_invalidate_area(unsigned type)
{
	struct gcma_tree *tree = gcma_trees[type];
	struct gcma_entry *entry, *n;

	if (!tree)
		return;

	/* walk the tree and free everything */
	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(entry, n, &tree->rbroot, rbnode)
		gcma_free_entry(tree, entry);
	tree->rbroot = RB_ROOT;
	spin_unlock(&tree->lock);

	zbud_destroy_pool(tree->pool);
	kfree(tree);
	gcma_trees[type] = NULL;
}

static struct zbud_ops gcma_zbud_ops = {
	.evict = gcma_writeback_entry
};

static void gcma_frontswap_init(unsigned type)
{
	struct gcma_tree *tree;

	tree = kzalloc(sizeof(struct gcma_tree), GFP_KERNEL);
	if (!tree)
		goto err;
	tree->pool = zbud_create_pool(GFP_KERNEL, &gcma_zbud_ops);
	if (!tree->pool)
		goto freetree;
	tree->rbroot = RB_ROOT;
	spin_lock_init(&tree->lock);
	gcma_trees[type] = tree;
	return;

freetree:
	kfree(tree);
err:
	pr_err("alloc failed, gcma disabled for swap type %d\n", type);
}

static struct frontswap_ops gcma_frontswap_ops = {
	.store = gcma_frontswap_store,
	.load = gcma_frontswap_load,
	.invalidate_page = gcma_frontswap_invalidate_page,
	.invalidate_area = gcma_frontswap_invalidate_area,
	.init = gcma_frontswap_init
};

/*********************************
* debugfs functions
**********************************/
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

	debugfs_create_u64("pool_limit_hit", S_IRUGO,
			gcma_debugfs_root, &gcma_pool_limit_hit);
	debugfs_create_u64("reject_reclaim_fail", S_IRUGO,
			gcma_debugfs_root, &gcma_reject_reclaim_fail);
	debugfs_create_u64("reject_alloc_fail", S_IRUGO,
			gcma_debugfs_root, &gcma_reject_alloc_fail);
	debugfs_create_u64("reject_kmemcache_fail", S_IRUGO,
			gcma_debugfs_root, &gcma_reject_kmemcache_fail);
	debugfs_create_u64("reject_compress_poor", S_IRUGO,
			gcma_debugfs_root, &gcma_reject_compress_poor);
	debugfs_create_u64("written_back_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_written_back_pages);
	debugfs_create_u64("duplicate_entry", S_IRUGO,
			gcma_debugfs_root, &gcma_duplicate_entry);
	debugfs_create_u64("pool_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_pool_pages);
	debugfs_create_atomic_t("stored_pages", S_IRUGO,
			gcma_debugfs_root, &gcma_stored_pages);

	return 0;
}

static void __exit gcma_debugfs_exit(void)
{
	debugfs_remove_recursive(gcma_debugfs_root);
}
#else
static int __init gcma_debugfs_init(void)
{
	return 0;
}

static void __exit gcma_debugfs_exit(void) { }
#endif

/*********************************
* module init and exit
**********************************/
static int __init init_gcma(void)
{
	if (!gcma_enabled)
		return 0;

	pr_info("loading gcma\n");
	if (gcma_secure_tcmr()) {
		pr_err("contig memory region securing failed\n");
		goto error;
	}
	if (gcma_entry_cache_create()) {
		pr_err("entry cache creation failed\n");
		goto error;
	}
	if (gcma_comp_init()) {
		pr_err("compressor initialization failed\n");
		goto compfail;
	}
	if (gcma_cpu_init()) {
		pr_err("per-cpu initialization failed\n");
		goto pcpufail;
	}
	frontswap_register_ops(&gcma_frontswap_ops);
	if (gcma_debugfs_init())
		pr_warn("debugfs initialization failed\n");
	return 0;
pcpufail:
	gcma_comp_exit();
compfail:
	gcma_entry_cache_destory();
error:
	return -ENOMEM;
}
/* must be late so crypto has time to come up */
late_initcall(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed contiguous memory allocator");
