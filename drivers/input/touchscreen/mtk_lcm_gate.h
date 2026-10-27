/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_lcm_gate.h - bind a touch driver only to the LCM it was written for.
 *
 * Author: kelexine <https://github.com/kelexine>
 *
 * Cubot P50 (marlon) ships with more than one panel/touch combination and the
 * CUSTOM_KERNEL_LCM list in the defconfig builds every LCM driver. The panel
 * actually fitted is chosen by the display driver at boot and published in
 * mtkfb_lcm_name (taken from the bootloader's atag,videolfb "lcmname").
 *
 * Touch drivers that are built in for every SKU must therefore ask "is my
 * panel the active one?" before touching the bus, otherwise a driver for a
 * panel that is not fitted probes a chip that is not there (and can disturb
 * the shared reset/IRQ lines of the chip that is).
 *
 * The decision logic is split into pure helpers that take the name as an
 * argument so they can be unit tested on a host without a kernel tree
 * (see tests/mtk_lcm_gate_test.c).
 */
#ifndef _MTK_LCM_GATE_H_
#define _MTK_LCM_GATE_H_

#ifdef MTK_LCM_GATE_HOST_TEST
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#ifndef EPROBE_DEFER
#define EPROBE_DEFER 517
#endif
#else
#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/string.h>
#include <linux/types.h>
#endif

/**
 * mtk_lcm_gate_name_matches - is @needle part of the active LCM name?
 * @active: active LCM name, may be NULL or empty (not published yet)
 * @needle: substring identifying the panel family, e.g. "icnl9911"
 *
 * Return: true only when both strings are non-empty and @active contains
 * @needle. An unknown name never matches.
 */
static inline bool mtk_lcm_gate_name_matches(const char *active,
					     const char *needle)
{
	return active && *active && needle && *needle &&
	       strstr(active, needle) != NULL;
}

/**
 * mtk_lcm_gate_eval_require - verdict for a driver that REQUIRES @needle
 * @active: active LCM name, may be NULL or empty
 * @needle: substring the active LCM name must contain
 *
 * Return: 0 to bind, -EPROBE_DEFER when the name is not published yet,
 * -ENODEV when a different LCM is active. An empty @needle disables the
 * gate (always 0).
 */
static inline int mtk_lcm_gate_eval_require(const char *active,
					    const char *needle)
{
	if (!needle || !*needle)
		return 0;
	if (!active || !*active)
		return -EPROBE_DEFER;
	return strstr(active, needle) ? 0 : -ENODEV;
}

#ifndef MTK_LCM_GATE_HOST_TEST
#if IS_ENABLED(CONFIG_MTK_FB)
/* Defined in drivers/misc/mediatek/video/<soc>/videox/mtkfb.c */
extern char mtkfb_lcm_name[256];
#define MTK_LCM_GATE_HAVE_NAME	1
#endif
#endif

/**
 * mtk_lcm_gate_require - gate a driver that is only valid for one LCM family
 * @needle: substring the active LCM name must contain
 *
 * Without an LCM name source in this kernel config the gate is open, so
 * configs that do not use mtkfb behave exactly as before.
 */
static inline int mtk_lcm_gate_require(const char *needle)
{
#ifdef MTK_LCM_GATE_HAVE_NAME
	return mtk_lcm_gate_eval_require(mtkfb_lcm_name, needle);
#else
	(void)needle;
	return 0;
#endif
}

/**
 * mtk_lcm_gate_is_active - is the LCM family @needle known to be fitted?
 * @needle: substring identifying the LCM family
 *
 * Use this for "skip me when the other SKU's panel is fitted" checks. It is
 * deliberately false while the name is unknown, so the driver keeps its
 * previous behaviour instead of failing closed.
 */
static inline bool mtk_lcm_gate_is_active(const char *needle)
{
#ifdef MTK_LCM_GATE_HAVE_NAME
	return mtk_lcm_gate_name_matches(mtkfb_lcm_name, needle);
#else
	(void)needle;
	return false;
#endif
}

#endif /* _MTK_LCM_GATE_H_ */
