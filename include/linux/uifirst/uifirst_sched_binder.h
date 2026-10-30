/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header: uifirst_sched_binder.h
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: Binder IPC dynamic UX propagation helpers
 */

#ifndef _LINUX_UIFIRST_SCHED_BINDER_H_
#define _LINUX_UIFIRST_SCHED_BINDER_H_

#include <linux/uifirst/uifirst_sched_common.h>

extern const struct sched_class rt_sched_class;

static inline void binder_set_inherit_ux(struct task_struct *thread_task,
					 struct task_struct *from_task)
{
	if (!from_task || !thread_task)
		return;

	if (test_set_dynamic_ux(from_task) && !test_task_ux(thread_task)) {
		dynamic_ux_enqueue(thread_task, DYNAMIC_UX_BINDER,
				   from_task->ux_depth);
	}
#ifdef CONFIG_CAMERA_OPT
	else if (from_task->camera_opt && !thread_task->camera_opt) {
		thread_task->camera_opt = 2;
	}
#endif
	else if (from_task->sched_class == &rt_sched_class) {
		if (!test_task_ux(thread_task))
			dynamic_ux_enqueue(thread_task, DYNAMIC_UX_BINDER,
					   from_task->ux_depth);
	}
#ifdef CONFIG_UIFIRST_AUDIO_OPT
	else if (is_audio_task(from_task)) {
		if (!test_task_ux(thread_task))
			dynamic_ux_enqueue(thread_task, DYNAMIC_UX_BINDER,
					   from_task->ux_depth);
	}
#endif
}

static inline void binder_unset_inherit_ux(struct task_struct *thread_task)
{
	if (!thread_task)
		return;

	if (test_dynamic_ux(thread_task, DYNAMIC_UX_BINDER)) {
		dynamic_ux_dequeue(thread_task, DYNAMIC_UX_BINDER);
	}
#ifdef CONFIG_CAMERA_OPT
	else if (thread_task->camera_opt == 2) {
		thread_task->camera_opt = 0;
	}
#endif
}

#endif /* _LINUX_UIFIRST_SCHED_BINDER_H_ */
