/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header: uifirst_sched_mutex.h
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: Mutex UX wait-queue ordering and owner priority inheritance hooks
 */

#ifndef _LINUX_UIFIRST_SCHED_MUTEX_H_
#define _LINUX_UIFIRST_SCHED_MUTEX_H_

struct list_head;
struct mutex;
struct task_struct;

extern void mutex_list_add(struct task_struct *task, struct list_head *entry,
			   struct list_head *head, struct mutex *lock);
extern void mutex_set_inherit_ux(struct mutex *lock, struct task_struct *task);
extern void mutex_unset_inherit_ux(struct mutex *lock, struct task_struct *task);
extern void mutex_clear_inherit_ux_on_abort(struct mutex *lock);

#endif /* _LINUX_UIFIRST_SCHED_MUTEX_H_ */
