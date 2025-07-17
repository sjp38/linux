/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KASAN_ENABLED_H
#define _LINUX_KASAN_ENABLED_H

#include <linux/static_key.h>

/* Controls whether KASAN is enabled at all (compile-time check). */
static __always_inline bool kasan_enabled(void)
{
	return IS_ENABLED(CONFIG_KASAN);
}

#ifdef CONFIG_ARCH_DEFER_KASAN
/*
 * Global runtime flag for architectures that need deferred KASAN.
 * Switched to 'true' by the appropriate kasan_init_*()
 * once KASAN is fully initialized.
 */
DECLARE_STATIC_KEY_FALSE(kasan_flag_enabled);

static __always_inline bool kasan_shadow_initialized(void)
{
	return static_branch_likely(&kasan_flag_enabled);
}

static inline void kasan_enable(void)
{
	static_branch_enable(&kasan_flag_enabled);
}
#else
/* For architectures that can enable KASAN early, use compile-time check. */
static __always_inline bool kasan_shadow_initialized(void)
{
	return kasan_enabled();
}

/* No-op for architectures that don't need deferred KASAN. */
static inline void kasan_enable(void) {}
#endif /* CONFIG_ARCH_DEFER_KASAN */

#ifdef CONFIG_KASAN_HW_TAGS
static inline bool kasan_hw_tags_enabled(void)
{
	return kasan_enabled();
}
#else
static inline bool kasan_hw_tags_enabled(void)
{
	return false;
}
#endif /* CONFIG_KASAN_HW_TAGS */

#endif /* LINUX_KASAN_ENABLED_H */
