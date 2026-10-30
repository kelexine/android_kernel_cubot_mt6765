/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header: uifirst_sched_status.h
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: Task runtime/sleep sampling state for UIFIRST audio optimization
 */

#ifndef _LINUX_UIFIRST_SCHED_STATUS_H_
#define _LINUX_UIFIRST_SCHED_STATUS_H_

enum task_stats_type {
	TST_SLEEP = 0,          /* sleeping duration recorded on wakeup enqueue */
	TST_RUNNABLE,           /* runnable wait duration recorded on dispatch */
	TST_EXEC,               /* execution duration recorded on sleep dequeue */
	TST_SCHED_TYPE_TOTAL,
};

#define TASK_INFO_SAMPLE 4

struct task_info {
	u64 sa_info[TST_SCHED_TYPE_TOTAL][TASK_INFO_SAMPLE];
	bool im_small;
};

extern int sysctl_sched_impt_tgid;

#endif /* _LINUX_UIFIRST_SCHED_STATUS_H_ */
