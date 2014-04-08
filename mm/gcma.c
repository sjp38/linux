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
#include <linux/highmem.h>

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
static int reserved_gcma;

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

	if (reserved_gcma == MAX_GCMA) {
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
		return -ENOMEM;
	}

	pr_debug("%llu bytes gcma reserved\n", size);

	gcma_id = reserved_gcma++;
	info = &ginfo[gcma_id];

	/*
	 * TODO: protect race by concurrent call of gcma_reserve
	 */
	info->size = size >> PAGE_SHIFT;
	info->base_pfn = PFN_DOWN(addr);

	return gcma_id;
}

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
	int id = gcma_id;
	unsigned long *bitmap, next_zero_area;
	struct gcma_info *info;

	if (id >= reserved_gcma) {
		pr_warn("invalid gcma_id %d [%d]\n", id, reserved_gcma);
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

	if (id >= reserved_gcma) {
		pr_warn("invalid gcma_id %d [%d]\n", id, reserved_gcma);
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

static int __init init_gcma(void)
{
	int i;
	unsigned long bitmap_bytes;

	if (!gcma_enabled)
		return 0;

	pr_info("loading gcma\n");

	for (i = 0; i < reserved_gcma; i++) {
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

	return 0;
}

module_init(init_gcma);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Minchan Kim <minchan@kernel.org>");
MODULE_AUTHOR("SeongJae Park <sj38.park@gmail.com>");
MODULE_DESCRIPTION("Guaranteed Contiguous Memory Allocator");
