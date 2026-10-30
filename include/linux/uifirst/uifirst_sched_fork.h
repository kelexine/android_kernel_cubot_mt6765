/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header: uifirst_sched_fork.h
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: Per-task UIFIRST UX field initialization during copy_process()
 */

#ifndef _LINUX_UIFIRST_SCHED_FORK_H_
#define _LINUX_UIFIRST_SCHED_FORK_H_

#include <linux/string.h>

static inline void init_task_ux_info(struct task_struct *p)
{
	p->static_ux = 0;
#ifdef CONFIG_CAMERA_OPT
	p->camera_opt = 0;
#endif
	atomic64_set(&p->dynamic_ux, 0);
	INIT_LIST_HEAD(&p->ux_entry);
	p->ux_depth = 0;
	p->enqueue_time = 0;
	p->dynamic_ux_start = 0;
#ifdef CONFIG_MMAP_LOCK_OPT
	p->ux_once = 0;
	p->get_mmlock = 0;
	p->get_mmlock_ts = 0;
#endif
#ifdef CONFIG_UIFIRST_AUDIO_OPT
	memset(&p->ux_audio_task_info, 0, sizeof(struct task_info));
#endif
}

#endif /* _LINUX_UIFIRST_SCHED_FORK_H_ */
