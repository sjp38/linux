#ifndef __CMA_H__
#define __CMA_H__

/*
 * There is always at least global CMA area and a few optional
 * areas configured in kernel .config.
 */
#ifdef CONFIG_CMA_AREAS
#define MAX_CMA_AREAS	(1 + CONFIG_CMA_AREAS)

#else
#define MAX_CMA_AREAS	(0)

#endif

extern phys_addr_t cma_get_base(unsigned int cma_id);
extern unsigned long cma_get_size(unsigned int cma_id);

extern int __init cma_declare_contiguous(phys_addr_t size,
			phys_addr_t base, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, unsigned int *cma_id);

extern struct page *cma_alloc(unsigned int cma_id, int count, unsigned int align);
extern bool cma_release(unsigned int cma_id, struct page *pages, int count);


#endif
