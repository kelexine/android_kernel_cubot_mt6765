/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header: uifirst_sched_rwsem.h
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: RWSEM UX wait-queue ordering and writer priority inheritance hooks
 */

#ifndef _LINUX_UIFIRST_SCHED_RWSEM_H_
#define _LINUX_UIFIRST_SCHED_RWSEM_H_

struct list_head;
struct rw_semaphore;
struct task_struct;

#ifndef CONFIG_MTK_TASK_TURBO
#ifdef CONFIG_RWSEM_PRIO_AWARE
extern bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry,
			   struct list_head *head, struct rw_semaphore *sem);
#else /* !CONFIG_RWSEM_PRIO_AWARE */
extern void rwsem_list_add(struct task_struct *tsk, struct list_head *entry,
			   struct list_head *head);
#endif /* CONFIG_RWSEM_PRIO_AWARE */
#endif /* !CONFIG_MTK_TASK_TURBO */

extern void rwsem_set_inherit_ux(struct task_struct *tsk,
				 struct task_struct *owner,
				 struct rw_semaphore *sem);
extern void rwsem_unset_inherit_ux(struct rw_semaphore *sem,
				   struct task_struct *tsk);
extern void rwsem_clear_inherit_ux_on_abort(struct rw_semaphore *sem);

#endif /* _LINUX_UIFIRST_SCHED_RWSEM_H_ */
