// SPDX-License-Identifier: GPL-2.0-only
/*
 * File: uifirst_sched_rwsem.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: RWSEM UX wait-queue ordering and RCU-safe writer UX inheritance
 */

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/uifirst/uifirst_sched_common.h>

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
};

/*
 * Bit 0 set in sem->owner denotes anonymous ownership (RWSEM_READER_OWNED = 1UL
 * or RWSEM_OWNER_UNKNOWN = -1L). Only even non-NULL pointers are task_structs.
 */
static inline bool rwsem_owner_is_writer(struct task_struct *owner)
{
	return owner && !((unsigned long)owner & 1UL);
}

static void rwsem_list_add_ux(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct rwsem_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct rwsem_waiter, list);
		if (!waiter->task)
			continue;
#ifdef CONFIG_RWSEM_PRIO_AWARE
		if (!test_task_ux(waiter->task) &&
		    waiter->task->prio > MAX_RT_PRIO) {
#else
		if (!test_task_ux(waiter->task)) {
#endif
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	list_add_tail(entry, head);
}

#ifndef CONFIG_MTK_TASK_TURBO
#ifdef CONFIG_RWSEM_PRIO_AWARE
bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry,
		    struct list_head *head, struct rw_semaphore *sem)
{
	if (!entry || !head || !sem)
		return false;

	if (test_task_ux(tsk)) {
		rwsem_list_add_ux(entry, head);
		sem->m_count++;
		return true;
	}

	return false;
}
#else /* !CONFIG_RWSEM_PRIO_AWARE */
void rwsem_list_add(struct task_struct *tsk, struct list_head *entry,
		    struct list_head *head)
{
	if (!entry || !head)
		return;

	if (test_task_ux(tsk))
		rwsem_list_add_ux(entry, head);
	else
		list_add_tail(entry, head);
}
#endif /* CONFIG_RWSEM_PRIO_AWARE */
#endif /* !CONFIG_MTK_TASK_TURBO */

void rwsem_set_inherit_ux(struct task_struct *tsk, struct task_struct *owner,
			  struct rw_semaphore *sem)
{
	if (!tsk || !sem || sem->ux_dep_task || !test_set_dynamic_ux(tsk))
		return;

	rcu_read_lock();
	if (rwsem_owner_is_writer(owner) && !test_task_ux(owner) &&
	    atomic_inc_not_zero(&owner->usage)) {
		rcu_read_unlock();
		dynamic_ux_enqueue(owner, DYNAMIC_UX_RWSEM, tsk->ux_depth);
		WRITE_ONCE(sem->ux_dep_task, owner);
		return;
	}
	rcu_read_unlock();
}

void rwsem_unset_inherit_ux(struct rw_semaphore *sem, struct task_struct *tsk)
{
	if (sem && tsk && sem->ux_dep_task == tsk) {
		WRITE_ONCE(sem->ux_dep_task, NULL);
		dynamic_ux_dequeue(tsk, DYNAMIC_UX_RWSEM);
		put_task_struct(tsk);
	}
}

void rwsem_clear_inherit_ux_on_abort(struct rw_semaphore *sem)
{
	struct rwsem_waiter *waiter = NULL;
	struct task_struct *dep = NULL;

	if (!sem || !sem->ux_dep_task)
		return;

	list_for_each_entry(waiter, &sem->wait_list, list) {
		if (waiter->task && test_set_dynamic_ux(waiter->task))
			return;
	}

	dep = sem->ux_dep_task;
	WRITE_ONCE(sem->ux_dep_task, NULL);
	dynamic_ux_dequeue(dep, DYNAMIC_UX_RWSEM);
	put_task_struct(dep);
}
