/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Vitaly Wool <vitaly.wool@konsulko.com>
 * Copyright (C) 2025, Konsulko AB.
 */
#ifndef __ZBLOCK_H__
#define __ZBLOCK_H__

#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/rculist.h>
#include <linux/types.h>

#if PAGE_SIZE == 0x1000
/* max 64 slots per block, max table size 64 */
#define SLOT_BITS 6
#elif PAGE_SIZE == 0x4000
/* max 256 slots per block, max table size 64 */
#define SLOT_BITS 8
#else
#warning This PAGE_SIZE is not quite supported yet
#define SLOT_BITS 8
#endif

#define MAX_TABLE_SIZE (1 << (PAGE_SHIFT - SLOT_BITS))

#define MAX_SLOTS (1 << SLOT_BITS)
#define SLOT_MASK ((0x1UL << SLOT_BITS) - 1)

#define ZBLOCK_HEADER_SIZE	round_up(sizeof(struct zblock_block), sizeof(long))
#define BLOCK_DATA_SIZE(num) ((PAGE_SIZE * (num)) - ZBLOCK_HEADER_SIZE)
#define SLOT_SIZE(nslots, num) (round_down((BLOCK_DATA_SIZE(num) / nslots), sizeof(long)))
#define ZBLOCK_MAX_PAGES_PER_BLOCK	8

/**
 * struct zblock_block - block metadata
 * Block consists of several pages and contains fixed
 * integer number of slots for allocating compressed pages.
 *
 * free_slots:	number of free slots in the block
 * slot_info:	contains data about free/occupied slots
 */
struct zblock_block {
	DECLARE_BITMAP(slot_info, 1 << SLOT_BITS);
	struct list_head link;
	struct rcu_head rcu;
	unsigned short block_type;
	unsigned short free_slots;
};

/**
 * struct block_desc - general metadata for block lists
 * Each block list stores only blocks of corresponding type which means
 * that all blocks in it have the same number and size of slots.
 * All slots are aligned to size of long.
 *
 * slot_size:		size of slot for this list
 * slots_per_block:	number of slots per block for this list
 * num_pages:		number of pages per block
 */
struct block_desc {
	unsigned int slot_size;
	unsigned short slots_per_block;
	unsigned short num_pages;
};

struct block_desc_node {
	struct rb_node node;
	unsigned int this_slot_size;
	unsigned int next_slot_size;
	unsigned int block_idx;
};

static const struct block_desc block_desc[] = {
#if PAGE_SIZE == 0x1000
	{ SLOT_SIZE(28, 1), 28, 1 },
	{ SLOT_SIZE(18, 1), 18, 1 },
	{ SLOT_SIZE(12, 1), 12, 1 },
	{ SLOT_SIZE(10, 1), 10, 1 },
	{ SLOT_SIZE(17, 2), 17, 2 },
	{ SLOT_SIZE(15, 2), 15, 2 },
	{ SLOT_SIZE(13, 2), 13, 2 },
	{ SLOT_SIZE(6, 1), 6, 1 },
	{ SLOT_SIZE(11, 2), 11, 2 },
	{ SLOT_SIZE(5, 1), 5, 1 },
	{ SLOT_SIZE(19, 4), 19, 4 },
	{ SLOT_SIZE(9, 2), 9, 2 },
	{ SLOT_SIZE(17, 4), 17, 4 },
	{ SLOT_SIZE(4, 1), 4, 1 },
	{ SLOT_SIZE(23, 6), 23, 6 },
	{ SLOT_SIZE(11, 3), 11, 3 },
	{ SLOT_SIZE(7, 2), 7, 2 },
	{ SLOT_SIZE(10, 3), 10, 3 },
	{ SLOT_SIZE(19, 6), 19, 6 },
	{ SLOT_SIZE(6, 2), 6, 2 },
	{ SLOT_SIZE(14, 5), 14, 5 },
	{ SLOT_SIZE(8, 3), 8, 3 },
	{ SLOT_SIZE(5, 2), 5, 2 },
	{ SLOT_SIZE(12, 5), 12, 5 },
	{ SLOT_SIZE(9, 4), 9, 4 },
	{ SLOT_SIZE(15, 7), 15, 7 },
	{ SLOT_SIZE(2, 1), 2, 1 },
	{ SLOT_SIZE(15, 8), 15, 8 },
	{ SLOT_SIZE(9, 5), 9, 5 },
	{ SLOT_SIZE(12, 7), 12, 7 },
	{ SLOT_SIZE(13, 8), 13, 8 },
	{ SLOT_SIZE(6, 4), 6, 4 },
	{ SLOT_SIZE(11, 8), 11, 8 },
	{ SLOT_SIZE(9, 7), 9, 7 },
	{ SLOT_SIZE(6, 5), 6, 5 },
	{ SLOT_SIZE(9, 8), 9, 8 },
	{ SLOT_SIZE(4, 4), 4, 4 },
#else
	{ SLOT_SIZE(185, 1), 185, 1 },
	{ SLOT_SIZE(113, 1), 113, 1 },
	{ SLOT_SIZE(86, 1), 86, 1 },
	{ SLOT_SIZE(72, 1), 72, 1 },
	{ SLOT_SIZE(58, 1), 58, 1 },
	{ SLOT_SIZE(49, 1), 49, 1 },
	{ SLOT_SIZE(42, 1), 42, 1 },
	{ SLOT_SIZE(37, 1), 37, 1 },
	{ SLOT_SIZE(33, 1), 33, 1 },
	{ SLOT_SIZE(59, 2), 59, 2 },
	{ SLOT_SIZE(27, 1), 27, 1 },
	{ SLOT_SIZE(25, 1), 25, 1 },
	{ SLOT_SIZE(23, 1), 23, 1 },
	{ SLOT_SIZE(21, 1), 21, 1 },
	{ SLOT_SIZE(39, 2), 39, 2 },
	{ SLOT_SIZE(37, 2), 37, 2 },
	{ SLOT_SIZE(35, 2), 35, 2 },
	{ SLOT_SIZE(33, 2), 33, 2 },
	{ SLOT_SIZE(31, 2), 31, 2 },
	{ SLOT_SIZE(29, 2), 29, 2 },
	{ SLOT_SIZE(27, 2), 27, 2 },
	{ SLOT_SIZE(25, 2), 25, 2 },
	{ SLOT_SIZE(12, 1), 12, 1 },
	{ SLOT_SIZE(11, 1), 11, 1 },
	{ SLOT_SIZE(21, 2), 21, 2 },
	{ SLOT_SIZE(10, 1), 10, 1 },
	{ SLOT_SIZE(19, 2), 19, 2 },
	{ SLOT_SIZE(9, 1), 9, 1 },
	{ SLOT_SIZE(17, 2), 17, 2 },
	{ SLOT_SIZE(8, 1), 8, 1 },
	{ SLOT_SIZE(15, 2), 15, 2 },
	{ SLOT_SIZE(14, 2), 14, 2 },
	{ SLOT_SIZE(27, 4), 27, 4 },
	{ SLOT_SIZE(13, 2), 13, 2 },
	{ SLOT_SIZE(25, 4), 25, 4 },
	{ SLOT_SIZE(12, 2), 12, 2 },
	{ SLOT_SIZE(23, 4), 23, 4 },
	{ SLOT_SIZE(11, 2), 11, 2 },
	{ SLOT_SIZE(21, 4), 21, 4 },
	{ SLOT_SIZE(10, 2), 10, 2 },
	{ SLOT_SIZE(19, 4), 19, 4 },
	{ SLOT_SIZE(9, 2), 9, 2 },
	{ SLOT_SIZE(17, 4), 17, 4 },
	{ SLOT_SIZE(4, 1), 4, 1 },
	{ SLOT_SIZE(23, 6), 23, 6 },
	{ SLOT_SIZE(11, 3), 11, 3 },
	{ SLOT_SIZE(7, 2), 7, 2 },
	{ SLOT_SIZE(10, 3), 10, 3 },
	{ SLOT_SIZE(16, 5), 16, 5 },
	{ SLOT_SIZE(6, 2), 6, 2 },
	{ SLOT_SIZE(11, 4), 11, 4 },
	{ SLOT_SIZE(8, 3), 8, 3 },
	{ SLOT_SIZE(5, 2), 5, 2 },
	{ SLOT_SIZE(7, 3), 7, 3 },
	{ SLOT_SIZE(11, 5), 11, 5 },
	{ SLOT_SIZE(4, 2), 4, 2 },
	{ SLOT_SIZE(9, 5), 9, 5 },
	{ SLOT_SIZE(8, 5), 8, 5 },
	{ SLOT_SIZE(3, 2), 3, 2 },
	{ SLOT_SIZE(7, 6), 7, 6 },
	{ SLOT_SIZE(4, 4), 4, 4 },
#endif /* PAGE_SIZE */
};

/**
 * struct block_list - stores metadata of particular list
 * lock:		protects the list of blocks
 * active_list:		linked list of active (non-full) blocks
 * block_count:		total number of blocks in the list
 */
struct block_list {
	spinlock_t lock;
	struct list_head active_list;
	unsigned long block_count;
};

/**
 * struct zblock_pool - stores metadata for each zblock pool
 * @block_lists:	array of block lists
 * @zpool:		zpool driver
 *
 * This structure is allocated at pool creation time and maintains metadata
 * for a particular zblock pool.
 */
struct zblock_pool {
	struct block_list block_lists[ARRAY_SIZE(block_desc)];
	struct kmem_cache *block_header_cache;
	struct zpool *zpool;
};


#endif
