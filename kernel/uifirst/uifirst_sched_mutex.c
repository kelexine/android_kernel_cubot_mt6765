// SPDX-License-Identifier: GPL-2.0-only
/*
 * File: uifirst_sched_mutex.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: Mutex UX wait-queue ordering and RCU-safe owner UX inheritance
 */

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/uifirst/uifirst_sched_common.h>

static void mutex_list_add_ux(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct mutex_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct mutex_waiter, list);
		if (waiter->task && !test_task_ux(waiter->task)) {
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	list_add_tail(entry, head);
}

void mutex_list_add(struct task_struct *task, struct list_head *entry,
		    struct list_head *head, struct mutex *lock)
{
	if (!entry || !head || !lock)
		return;

	if (test_task_ux(task) && !lock->ux_dep_task)
		mutex_list_add_ux(entry, head);
	else
		list_add_tail(entry, head);
}

void mutex_set_inherit_ux(struct mutex *lock, struct task_struct *task)
{
	struct task_struct *owner = NULL;

	if (!lock || lock->ux_dep_task || !test_set_dynamic_ux(task))
		return;

	rcu_read_lock();
	owner = __mutex_owner(lock);
	if (owner && !test_task_ux(owner) &&
	    atomic_inc_not_zero(&owner->usage)) {
		rcu_read_unlock();
		dynamic_ux_enqueue(owner, DYNAMIC_UX_MUTEX, task->ux_depth);
		lock->ux_dep_task = owner;
		return;
	}
	rcu_read_unlock();
}

void mutex_unset_inherit_ux(struct mutex *lock, struct task_struct *task)
{
	if (lock && task && lock->ux_dep_task == task) {
		lock->ux_dep_task = NULL;
		dynamic_ux_dequeue(task, DYNAMIC_UX_MUTEX);
		put_task_struct(task);
	}
}

void mutex_clear_inherit_ux_on_abort(struct mutex *lock)
{
	struct mutex_waiter *waiter = NULL;
	struct task_struct *dep = NULL;

	if (!lock || !lock->ux_dep_task)
		return;

	list_for_each_entry(waiter, &lock->wait_list, list) {
		if (waiter->task && test_set_dynamic_ux(waiter->task))
			return;
	}

	dep = lock->ux_dep_task;
	lock->ux_dep_task = NULL;
	dynamic_ux_dequeue(dep, DYNAMIC_UX_MUTEX);
	put_task_struct(dep);
}
