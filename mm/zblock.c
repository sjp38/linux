// SPDX-License-Identifier: GPL-2.0-only
/*
 * zblock.c
 *
 * Author: Vitaly Wool <vitaly.wool@konsulko.se>
 * Based on the work from Ananda Badmaev <a.badmaev@clicknet.pro>
 * Copyright (C) 2022-2025, Konsulko AB.
 *
 * Zblock is a small object allocator with the intention to serve as a
 * zpool backend. It operates on page blocks which consist of number
 * of physical pages being a power of 2 and store integer number of
 * compressed pages per block which results in determinism and simplicity.
 *
 * zblock doesn't export any API and is meant to be used via zpool API.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/zpool.h>
#include "zblock.h"

static struct rb_root block_desc_tree = RB_ROOT;
static struct dentry *zblock_debugfs_root;

/* Encode handle of a particular slot in the pool using metadata */
static inline unsigned long metadata_to_handle(struct zblock_block *block, unsigned int slot)
{
	return (unsigned long)(block) | slot;
}

/* Return block, block type and slot in the pool corresponding to handle */
static inline struct zblock_block *handle_to_metadata(unsigned long handle, unsigned int *slot)
{
	*slot = handle & SLOT_MASK;
	return (struct zblock_block *)(handle & ~SLOT_MASK);
}

/*
 * Find a block with at least one free slot and claim it.
 * We make sure that the first block, if exists, will always work.
 */
static inline struct zblock_block *find_and_claim_block(struct block_list *b,
		int block_type, unsigned long *handle)
{
	struct list_head *l = &b->active_list;
	unsigned int slot;
	struct zblock_block *z;

	rcu_read_lock();
retry_claim:
	z = list_first_or_null_rcu(l, typeof(*z), link);
	if (z) {
		spin_lock(&b->lock);
		if (unlikely(!z->free_slots)) {
			spin_unlock(&b->lock);
			goto retry_claim;
		}
		if (--z->free_slots == 0)
			list_bidir_del_rcu(&z->link);
		spin_unlock(&b->lock);
		/*
		 * There is a slot in the block and we just made sure it will
		 * remain.
		 * Find that slot and set the busy bit.
		 */
		for (slot = find_first_zero_bit(z->slot_info,
					block_desc[block_type].slots_per_block);
		     slot < block_desc[block_type].slots_per_block;
		     slot = find_next_zero_bit(z->slot_info,
					block_desc[block_type].slots_per_block,
					slot)) {
			if (!test_and_set_bit(slot, z->slot_info))
				break;
		}

		*handle = metadata_to_handle(z, slot);
	}
	rcu_read_unlock();
	return z;
}

/*
 * allocate new block and add it to corresponding block list
 */
static struct zblock_block *alloc_block(struct zblock_pool *pool,
					int block_type, gfp_t gfp,
					unsigned long *handle,
					unsigned int nid)
{
	struct block_list *block_list = &pool->block_lists[block_type];
	unsigned int num_pages = block_desc[block_type].num_pages;
	struct zblock_block *block;

	block = __vmalloc_node(PAGE_SIZE * num_pages, PAGE_SIZE, gfp, nid, NULL);
	if (!block)
		return NULL;

	/* init block data */
	init_rcu_head(&block->rcu);
	block->block_type = block_type;
	block->free_slots = block_desc[block_type].slots_per_block - 1;
	memset(&block->slot_info, 0, sizeof(block->slot_info));
	set_bit(0, block->slot_info);
	*handle = metadata_to_handle(block, 0);

	spin_lock(&block_list->lock);
	list_add_rcu(&block->link, &block_list->active_list);
	block_list->block_count++;
	spin_unlock(&block_list->lock);
	return block;
}

static int zblock_blocks_show(struct seq_file *s, void *v)
{
	struct zblock_pool *pool = s->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(block_desc); i++) {
		struct block_list *block_list = &pool->block_lists[i];

		seq_printf(s, "%d: %ld blocks of %d pages (total %ld pages)\n",
			i, block_list->block_count, block_desc[i].num_pages,
			block_list->block_count * block_desc[i].num_pages);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(zblock_blocks);

/*****************
 * API Functions
 *****************/
/**
 * zblock_create_pool() - create a new zblock pool
 * @gfp:	gfp flags when allocating the zblock pool structure
 *
 * Return: pointer to the new zblock pool or NULL if the metadata allocation
 * failed.
 */
static struct zblock_pool *zblock_create_pool(gfp_t gfp)
{
	struct zblock_pool *pool = kmalloc(sizeof(struct zblock_pool), gfp);
	int i;

	if (!pool)
		return NULL;

	/* init each block list */
	for (i = 0; i < ARRAY_SIZE(block_desc); i++) {
		struct block_list *block_list = &pool->block_lists[i];

		spin_lock_init(&block_list->lock);
		INIT_LIST_HEAD(&block_list->active_list);
		block_list->block_count = 0;
	}

	pool->block_header_cache = kmem_cache_create("zblock",
						     sizeof(struct zblock_block),
						     (1 << SLOT_BITS), 0, NULL);
	if (!pool->block_header_cache)
		goto out;

	debugfs_create_file("blocks", S_IFREG | 0444, zblock_debugfs_root,
			    pool, &zblock_blocks_fops);
	return pool;

out:
	kfree(pool);
	return NULL;
}

/**
 * zblock_destroy_pool() - destroys an existing zblock pool
 * @pool:	the zblock pool to be destroyed
 *
 */
static void zblock_destroy_pool(struct zblock_pool *pool)
{
	kmem_cache_destroy(pool->block_header_cache);
	kfree(pool);
}


/**
 * zblock_alloc() - allocates a slot of appropriate size
 * @pool:	zblock pool from which to allocate
 * @size:	size in bytes of the desired allocation
 * @gfp:	gfp flags used if the pool needs to grow
 * @handle:	handle of the new allocation
 *
 * Return: 0 if success and handle is set, otherwise -EINVAL if the size or
 * gfp arguments are invalid or -ENOMEM if the pool was unable to allocate
 * a new slot.
 */
static int zblock_alloc(struct zblock_pool *pool, size_t size, gfp_t gfp,
			unsigned long *handle, unsigned int nid)
{
	int block_type = -1;
	struct zblock_block *block;
	struct block_list *block_list;

	if (!size)
		return -EINVAL;

	if (size > block_desc[ARRAY_SIZE(block_desc) - 1].slot_size)
		return -ENOSPC;

	/* find basic block type with suitable slot size */
	if (size < block_desc[0].slot_size)
		block_type = 0;
	else {
		struct block_desc_node *block_node;
		struct rb_node *node = block_desc_tree.rb_node;

		while (node) {
			block_node = container_of(node, typeof(*block_node), node);
			if (size < block_node->this_slot_size)
				node = node->rb_left;
			else if (size >= block_node->next_slot_size)
				node = node->rb_right;
			else {
				block_type = block_node->block_idx + 1;
				break;
			}
		}
	}
	if (WARN_ON(block_type < 0))
		return -EINVAL;

	block_list = &pool->block_lists[block_type];

	block = find_and_claim_block(block_list, block_type, handle);
	if (block)
		return 0;

	/* not found block with free slots try to allocate new empty block */
	block = alloc_block(pool, block_type, gfp, handle, nid);
	return block ? 0 : -ENOMEM;
}

/**
 * zblock_free() - frees the allocation associated with the given handle
 * @pool:	pool in which the allocation resided
 * @handle:	handle associated with the allocation returned by zblock_alloc()
 *
 */
static void zblock_free(struct zblock_pool *pool, unsigned long handle)
{
	unsigned int slot, block_type;
	struct zblock_block *block;
	struct block_list *block_list;

	block = handle_to_metadata(handle, &slot);
	block_type = block->block_type;
	block_list = &pool->block_lists[block_type];

	/* clear bit early, this will shorten the search */
	clear_bit(slot, block->slot_info);

	spin_lock(&block_list->lock);
	/* if all slots in block are empty delete the whole block */
	if (++block->free_slots == block_desc[block_type].slots_per_block) {
		block_list->block_count--;
		list_bidir_del_rcu(&block->link);
		spin_unlock(&block_list->lock);
		kvfree_rcu(block, rcu);
		return;
	} else if (block->free_slots == 1)
		list_add_tail_rcu(&block->link, &block_list->active_list);
	spin_unlock(&block_list->lock);
}

/**
 * zblock_map() - maps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be mapped
 *
 *
 * Returns: a pointer to the mapped allocation
 */
static void *zblock_map(struct zblock_pool *pool, unsigned long handle)
{
	unsigned int slot;
	struct zblock_block *block = handle_to_metadata(handle, &slot);
	unsigned long offs = ZBLOCK_HEADER_SIZE + slot * block_desc[block->block_type].slot_size;

	return (void *)block + offs;
}

/**
 * zblock_unmap() - unmaps the allocation associated with the given handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation to be unmapped
 */
static void zblock_unmap(struct zblock_pool *pool, unsigned long handle)
{
}

/**
 * zblock_write() - write to the memory area defined by handle
 * @pool:	pool in which the allocation resides
 * @handle:	handle associated with the allocation
 * @handle_mem: pointer to source memory block
 * @mem_len:	length of the memory block to write
 */
static void zblock_write(struct zblock_pool *pool, unsigned long handle,
			 void *handle_mem, size_t mem_len)
{
	unsigned int slot;
	struct zblock_block *block = handle_to_metadata(handle, &slot);
	unsigned long offs = ZBLOCK_HEADER_SIZE + slot * block_desc[block->block_type].slot_size;

	memcpy((void *)block + offs, handle_mem, mem_len);
}

/**
 * zblock_get_total_pages() - gets the zblock pool size in pages
 * @pool:	pool being queried
 *
 * Returns: size in bytes of the given pool.
 */
static u64 zblock_get_total_pages(struct zblock_pool *pool)
{
	u64 total_size;
	int i;

	total_size = 0;
	for (i = 0; i < ARRAY_SIZE(block_desc); i++)
		total_size += pool->block_lists[i].block_count * block_desc[i].num_pages;

	return total_size;
}

/*****************
 * zpool
 ****************/

static void *zblock_zpool_create(const char *name, gfp_t gfp)
{
	return zblock_create_pool(gfp);
}

static void zblock_zpool_destroy(void *pool)
{
	zblock_destroy_pool(pool);
}

static int zblock_zpool_malloc(void *pool, size_t size, gfp_t gfp,
			unsigned long *handle, const int nid)
{
	return zblock_alloc(pool, size, gfp, handle, nid);
}

static void zblock_zpool_free(void *pool, unsigned long handle)
{
	zblock_free(pool, handle);
}

static void *zblock_zpool_read_begin(void *pool, unsigned long handle,
				void *local_copy)
{
	return zblock_map(pool, handle);
}

static void zblock_zpool_obj_write(void *pool, unsigned long handle,
				void *handle_mem, size_t mem_len)
{
	zblock_write(pool, handle, handle_mem, mem_len);
}

static void zblock_zpool_read_end(void *pool, unsigned long handle,
				void *handle_mem)
{
	zblock_unmap(pool, handle);
}

static u64 zblock_zpool_total_pages(void *pool)
{
	return zblock_get_total_pages(pool);
}

static struct zpool_driver zblock_zpool_driver = {
	.type =			"zblock",
	.owner =		THIS_MODULE,
	.create =		zblock_zpool_create,
	.destroy =		zblock_zpool_destroy,
	.malloc =		zblock_zpool_malloc,
	.free =			zblock_zpool_free,
	.obj_read_begin =	zblock_zpool_read_begin,
	.obj_read_end =		zblock_zpool_read_end,
	.obj_write =		zblock_zpool_obj_write,
	.total_pages =		zblock_zpool_total_pages,
};

MODULE_ALIAS("zpool-zblock");

static void delete_rbtree(void)
{
	while (!RB_EMPTY_ROOT(&block_desc_tree))
		rb_erase(block_desc_tree.rb_node, &block_desc_tree);
}

static int __init create_rbtree(void)
{
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(block_desc) > MAX_TABLE_SIZE);
	for (i = 0; i < ARRAY_SIZE(block_desc); i++) {
		struct block_desc_node *block_node = kmalloc(sizeof(*block_node),
							GFP_KERNEL);
		struct rb_node **new = &block_desc_tree.rb_node, *parent = NULL;

		if (!block_node) {
			delete_rbtree();
			return -ENOMEM;
		}
		if (i > 0 && block_desc[i].slot_size <= block_desc[i-1].slot_size) {
			pr_err("%s: block descriptors not in ascending order\n",
				__func__);
			delete_rbtree();
			return -EINVAL;
		}
		block_node->this_slot_size = block_desc[i].slot_size;
		block_node->block_idx = i;
		if (i == ARRAY_SIZE(block_desc) - 1)
			block_node->next_slot_size = PAGE_SIZE * 2;
		else
			block_node->next_slot_size = block_desc[i+1].slot_size;
		while (*new) {
			parent = *new;
			/* the array is sorted so we will always go to the right */
			new = &((*new)->rb_right);
		}
		rb_link_node(&block_node->node, parent, new);
		rb_insert_color(&block_node->node, &block_desc_tree);
	}
	return 0;
}

static int __init init_zblock(void)
{
	int ret = create_rbtree();

	if (ret)
		return ret;

	zpool_register_driver(&zblock_zpool_driver);

	zblock_debugfs_root = debugfs_create_dir("zblock", NULL);
	return 0;
}

static void __exit exit_zblock(void)
{
	zpool_unregister_driver(&zblock_zpool_driver);
	debugfs_remove_recursive(zblock_debugfs_root);
	delete_rbtree();
}

module_init(init_zblock);
module_exit(exit_zblock);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vitaly Wool <vitaly.wool@konsulko.se>");
MODULE_DESCRIPTION("Block allocator for compressed pages");
