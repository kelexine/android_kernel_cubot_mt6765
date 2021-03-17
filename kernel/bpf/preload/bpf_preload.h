/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_PRELOAD_H
#define _BPF_PRELOAD_H

#include <linux/pid.h>
#include "iterators/bpf_preload_common.h"

/* Minimal stand-in for the UMD infrastructure (linux/usermode_driver.h),
 * which is not part of this port. The bpffs preloader plumbing only
 * needs tgid to detect whether the UMD process was started. */
struct umd_info {
	struct pid *tgid;
};

static inline void umd_cleanup_helper(struct umd_info *info)
{
	if (info->tgid) {
		put_pid(info->tgid);
		info->tgid = NULL;
	}
}

struct bpf_preload_ops {
        struct umd_info info;
	int (*preload)(struct bpf_preload_info *);
	int (*finish)(void);
	struct module *owner;
};
extern struct bpf_preload_ops *bpf_preload_ops;
#define BPF_PRELOAD_LINKS 2
#endif
