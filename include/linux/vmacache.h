#ifndef __LINUX_VMACACHE_H
#define __LINUX_VMACACHE_H

#include <linux/mm.h>

#define VMACACHE_BITS 2
#define VMACACHE_SIZE (1U << VMACACHE_BITS)
#define VMACACHE_MASK (VMACACHE_SIZE - 1)
/*
 * Hash based on the page number. Provides a good hit rate for
 * workloads with good locality and those with random accesses as well.
 */
#define VMACACHE_HASH(addr) ((addr >> PAGE_SHIFT) & VMACACHE_MASK)

#define vmacache_flush(tsk)					 \
	do {							 \
		memset(tsk->vmacache, 0, sizeof(tsk->vmacache)); \
	} while (0)

extern void vmacache_flush_all(struct mm_struct *mm);
extern void vmacache_update(unsigned long addr, struct vm_area_struct *newvma);
extern struct vm_area_struct *vmacache_find(struct mm_struct *mm,
						    unsigned long addr);

#ifndef CONFIG_MMU
extern struct vm_area_struct *vmacache_find_exact(struct mm_struct *mm,
						  unsigned long start,
						  unsigned long end);
#endif

static inline void vmacache_invalidate(struct mm_struct *mm)
{
	mm->vmacache_seqnum++;

	/* deal with overflows */
	if (unlikely(mm->vmacache_seqnum == 0))
		vmacache_flush_all(mm);
}

#endif /* __LINUX_VMACACHE_H */
