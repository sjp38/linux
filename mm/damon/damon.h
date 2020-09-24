/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: SeongJae Park <sjpark@amazon.de>
 */

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32() % (r - l))
