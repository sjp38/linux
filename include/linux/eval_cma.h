/*
 * eval_cma.h - Evaluate Contiguous Memory Allocator
 *
 *
 * Copyright (C) 2014  SeongJae Park <sj38.park@gmail.com>
 */

#ifndef _LINUX_EVAL_CMA_H
#define _LINUX_EVAL_CMA_H

#ifdef CONFIG_EVAL_CMA_MIGRATE

void eval_cma_reclaim_start(void);
void eval_cma_reclaim_end(unsigned long nr_reclaimed);

void eval_cma_migrate_start(void);
void eval_cma_migrate_end(unsigned long nr_migrated);

#endif

#endif /* _LINUX_EVAL_CMA_H */
