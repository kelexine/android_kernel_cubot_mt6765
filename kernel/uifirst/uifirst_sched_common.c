// SPDX-License-Identifier: GPL-2.0-only
/*
 * File: uifirst_sched_common.c
 * Author: kelexine <https://github.com/kelexine>
 * Date: 2026-10-30
 * Purpose: Core UIFIRST UX runqueue tracking, CPU placement, procfs static_ux,
 *          and Android audio priority scheduling assist
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kallsyms.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/seq_file.h>
#include <linux/trace_events.h>
#include <linux/version.h>
#include <trace/events/sched.h>
#include <../fs/proc/internal.h>
#include <../kernel/sched/sched.h>

#ifdef CONFIG_MMAP_LOCK_OPT
#include <linux/mm.h>
#include <linux/rwsem.h>
#endif

#include <linux/uifirst/uifirst_sched_common.h>

extern unsigned int walt_ravg_window;
#define walt_scale_demand_divisor (walt_ravg_window >> SCHED_CAPACITY_SHIFT)
#define scale_demand(d) ((d) / walt_scale_demand_divisor)

#define S2NS_T				1000000
#define DEFAULT_INPUT_BOOST_DURATION	1000
#define LAUNCHER_OVER_THRESH_MS		400
#define THRESHOLD_BOOST			102
#define CAMERA_PROVIDER_NAME		"provider@2.4-se"
#define CAMERA_HAL_SERVER_NAME		"camerahalserver"
#define CAMERA_APP_KEYWORDS		"camera"
#define CAMERA_HALCONT_NAME		"Camera Hal Cont"

int ux_min_sched_delay_granularity;     /* ux thread delay upper bound (ms) */
int ux_max_dynamic_exist = 1000;        /* ux dynamic max exist time (ms) */
int ux_max_dynamic_granularity = 64;
int ux_min_migration_delay = 10;        /* ux min migration delay time (ms) */
int ux_max_over_thresh = 50;            /* ms */
int sysctl_input_boost_enabled;
int sched_assist_ib_duration_coedecay = 1;
u64 sched_assist_input_boost_duration;
int sysctl_animation_type;

#ifdef CONFIG_UIFIRST_AUDIO_OPT
int sysctl_sched_impt_tgid;
#endif

static int param_ux_debug;
module_param_named(debug, param_ux_debug, uint, 0644);

enum ux_debug_level {
	UX_DEBUG_LEVEL_NONE = 0,
	UX_DEBUG_LEVEL_LIGHT,
	UX_DEBUG_LEVEL_HEAVY
};

static int entity_before(struct sched_entity *a, struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

static int entity_over(struct sched_entity *a, struct sched_entity *b)
{
	int over_thresh = sysctl_launcher_boost_enabled ?
			  LAUNCHER_OVER_THRESH_MS : ux_max_over_thresh;

	return (s64)(a->vruntime - b->vruntime) > (s64)over_thresh * S2NS_T;
}

extern const struct sched_class fair_sched_class;

bool test_task_ux(struct task_struct *task)
{
	u64 now = 0;

	if (!sysctl_uifirst_enabled || !task)
		return false;

	if (task->sched_class != &fair_sched_class)
		return false;

	if (task->static_ux)
		return true;

	now = jiffies_to_nsecs(jiffies);
	if (atomic64_read(&task->dynamic_ux) &&
	    (now - task->dynamic_ux_start) <=
	    (u64)ux_max_dynamic_granularity * S2NS_T)
		return true;

	return false;
}

void enqueue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;
	bool exist = false;

	if (!rq || !p || !list_empty(&p->ux_entry))
		return;

	p->enqueue_time = rq->clock;
	if (test_task_ux(p)) {
		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				exist = true;
				break;
			}
		}
		if (!exist) {
			list_add_tail(&p->ux_entry, &rq->ux_thread_list);
			get_task_struct(p);
		}
	}
}

void dequeue_ux_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos, *n;
	u64 now = jiffies_to_nsecs(jiffies);

	if (!rq || !p)
		return;

	p->enqueue_time = 0;
	if (!list_empty(&p->ux_entry)) {
		if (atomic64_read(&p->dynamic_ux) &&
		    (now - p->dynamic_ux_start) >
		    (u64)ux_max_dynamic_exist * S2NS_T) {
			atomic64_set(&p->dynamic_ux, 0);
		}
		list_for_each_safe(pos, n, &rq->ux_thread_list) {
			if (pos == &p->ux_entry) {
				list_del_init(&p->ux_entry);
				put_task_struct(p);
				return;
			}
		}
	}
}

static struct task_struct *pick_first_ux_thread(struct rq *rq)
{
	struct list_head *ux_thread_list = &rq->ux_thread_list;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct task_struct *temp = NULL;
	struct task_struct *leftmost_task = NULL;

	list_for_each_safe(pos, n, ux_thread_list) {
		temp = list_entry(pos, struct task_struct, ux_entry);
		if (unlikely(task_cpu(temp) != rq->cpu)) {
			list_del_init(&temp->ux_entry);
			put_task_struct(temp);
			continue;
		}
		if (!test_task_ux(temp))
			continue;

		if (leftmost_task == NULL ||
		    entity_before(&temp->se, &leftmost_task->se))
			leftmost_task = temp;
	}

	return leftmost_task;
}

void pick_ux_thread(struct rq *rq, struct task_struct **p,
		    struct sched_entity **se)
{
	struct task_struct *ori_p;
	struct task_struct *key_task;
	struct sched_entity *key_se;

	if (!rq || !p || !se || !*p)
		return;

#ifdef CONFIG_UIFIRST_AUDIO_OPT
	if ((*p)->ux_audio_task_info.im_small)
		return;
#endif

	ori_p = *p;
	if (!ori_p->static_ux && !atomic64_read(&ori_p->dynamic_ux)) {
		if (!list_empty(&rq->ux_thread_list)) {
			key_task = pick_first_ux_thread(rq);
			if (key_task && entity_over(&key_task->se, &ori_p->se))
				goto out;

			if (key_task) {
				key_se = &key_task->se;
				if (rq->clock >= key_task->enqueue_time &&
				    rq->clock - key_task->enqueue_time >=
				    ((u64)ux_min_sched_delay_granularity * S2NS_T)) {
					*p = key_task;
					*se = key_se;
				}
			}
		}
	}
out:
	if (param_ux_debug == UX_DEBUG_LEVEL_HEAVY)
		sched_assist_systrace((*p)->static_ux, "static_ux_%d", rq->cpu);
}

#define DYNAMIC_UX_SEC_WIDTH	8
#define DYNAMIC_UX_MASK_BASE	0x00000000ffULL

#define dynamic_ux_offset_of(type)	((type) * DYNAMIC_UX_SEC_WIDTH)
#define dynamic_ux_mask_of(type) \
	(DYNAMIC_UX_MASK_BASE << dynamic_ux_offset_of(type))
#define dynamic_ux_get_bits(value, type) \
	(((value) & dynamic_ux_mask_of(type)) >> dynamic_ux_offset_of(type))
#define dynamic_ux_value(type, value) \
	((u64)(value) << dynamic_ux_offset_of(type))

bool test_dynamic_ux(struct task_struct *task, int type)
{
	u64 dynamic_ux;

	if (!task)
		return false;

	dynamic_ux = atomic64_read(&task->dynamic_ux);
	return dynamic_ux_get_bits(dynamic_ux, type) > 0;
}

static bool test_task_exist(struct task_struct *task, struct list_head *head)
{
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, head) {
		if (pos == &task->ux_entry)
			return true;
	}
	return false;
}

void dynamic_ux_inc(struct task_struct *task, int type)
{
	atomic64_add(dynamic_ux_value(type, 1), &task->dynamic_ux);
}

void dynamic_ux_sub(struct task_struct *task, int type, int value)
{
	atomic64_sub(dynamic_ux_value(type, value), &task->dynamic_ux);
}

static void __dynamic_ux_dequeue(struct task_struct *task, int type, int value)
{
	struct rq_flags flags;
	bool exist = false;
	struct rq *rq = NULL;
	s64 dynamic_ux = 0;

	rq = task_rq_lock(task, &flags);
	dynamic_ux = atomic64_read(&task->dynamic_ux);
	if (dynamic_ux <= 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	dynamic_ux_sub(task, type, value);
	dynamic_ux = atomic64_read(&task->dynamic_ux);
	if (dynamic_ux > 0) {
		task_rq_unlock(rq, task, &flags);
		return;
	}
	if (dynamic_ux < 0)
		atomic64_set(&task->dynamic_ux, 0);

	task->ux_depth = 0;

	exist = test_task_exist(task, &rq->ux_thread_list);
	if (exist) {
		list_del_init(&task->ux_entry);
		put_task_struct(task);
	}
	task_rq_unlock(rq, task, &flags);
}

void dynamic_ux_dequeue(struct task_struct *task, int type)
{
	if (!task || type >= DYNAMIC_UX_MAX)
		return;

	__dynamic_ux_dequeue(task, type, 1);
}

void dynamic_ux_dequeue_refs(struct task_struct *task, int type, int value)
{
	if (!task || type >= DYNAMIC_UX_MAX)
		return;

	__dynamic_ux_dequeue(task, type, value);
}

static void __dynamic_ux_enqueue(struct task_struct *task, int type, int depth)
{
	struct rq_flags flags;
	bool exist = false;
	struct rq *rq = NULL;

	rq = task_rq_lock(task, &flags);
	if (unlikely(!list_empty(&task->ux_entry))) {
		task_rq_unlock(rq, task, &flags);
		return;
	}

	dynamic_ux_inc(task, type);
	task->dynamic_ux_start = jiffies_to_nsecs(jiffies);
	task->ux_depth = task->ux_depth > depth + 1 ? task->ux_depth : depth + 1;
	if (task->state == TASK_RUNNING &&
	    task->sched_class == &fair_sched_class) {
		exist = test_task_exist(task, &rq->ux_thread_list);
		if (!exist) {
			get_task_struct(task);
			list_add_tail(&task->ux_entry, &rq->ux_thread_list);
		}
	}
	task_rq_unlock(rq, task, &flags);
}

void dynamic_ux_enqueue(struct task_struct *task, int type, int depth)
{
	if (!task || type >= DYNAMIC_UX_MAX)
		return;

	__dynamic_ux_enqueue(task, type, depth);
}

bool test_task_ux_depth(int ux_depth)
{
	return ux_depth < UX_DEPTH_MAX;
}

bool test_set_dynamic_ux(struct task_struct *tsk)
{
	return tsk && (tsk->static_ux || atomic64_read(&tsk->dynamic_ux)) &&
	       test_task_ux_depth(tsk->ux_depth);
}

void ux_init_rq_data(struct rq *rq)
{
	if (!rq)
		return;

	INIT_LIST_HEAD(&rq->ux_thread_list);
}

int ux_prefer_cpu[NR_CPUS] = { 0 };

void ux_init_cpu_data(void)
{
	int i = 0;
	int min_cpu = 0, ux_cpu = 0;

	for (; i < nr_cpu_ids; ++i)
		ux_prefer_cpu[i] = -1;

	ux_cpu = cpumask_weight(topology_core_cpumask(min_cpu));
	if (ux_cpu == 0) {
		ux_warn("failed to init ux cpu data\n");
		return;
	}

	for (i = 0; i < nr_cpu_ids && ux_cpu < nr_cpu_ids; ++i)
		ux_prefer_cpu[i] = ux_cpu++;
}

bool test_ux_task_cpu(int cpu)
{
	return cpu >= ux_prefer_cpu[0];
}

bool test_ux_prefer_cpu(struct task_struct *tsk, int cpu)
{
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;

	if (cpu < 0)
		return false;

	if (tsk->pid == tsk->tgid)
		return capacity_orig_of(cpu) >= rd->max_cpu_capacity.val;

	return cpu >= ux_prefer_cpu[0];
}

struct ux_sched_cputopo ux_sched_cputopo;

void find_ux_task_cpu(struct task_struct *tsk, int *target_cpu)
{
	int i = 0;
	int temp_cpu = 0;
	struct rq *rq = NULL;

	for (i = (nr_cpu_ids - 1); i >= 0; --i) {
		temp_cpu = ux_prefer_cpu[i];
		if (temp_cpu <= 0 || temp_cpu >= nr_cpu_ids)
			continue;

		rq = cpu_rq(temp_cpu);
		if (!rq || rq->curr->prio <= MAX_RT_PRIO)
			continue;

#ifdef CONFIG_CAMERA_OPT
		if (sysctl_camera_opt_enabled && rq->curr->camera_opt)
			continue;
#endif
		if (!test_task_ux(rq->curr) && cpu_online(temp_cpu) &&
		    !cpu_isolated(temp_cpu) &&
		    cpumask_test_cpu(temp_cpu, &tsk->cpus_allowed)) {
			*target_cpu = temp_cpu;
			return;
		}
	}
}

int set_ux_task_cpu_common(struct task_struct *task, int prev_cpu,
			   int *target_cpu)
{
	int i;
	int lowest_prio = INT_MIN;
	struct task_struct *curr;

	if (*target_cpu != prev_cpu)
		return -1;

	for_each_cpu(i, cpu_active_mask) {
		if (!cpumask_test_cpu(i, &task->cpus_allowed))
			continue;
		curr = cpu_rq(i)->curr;
		if (curr && curr->sched_class == &fair_sched_class &&
		    curr->prio > lowest_prio) {
			lowest_prio = curr->prio;
			*target_cpu = i;
		}
	}

	return *target_cpu;
}

extern unsigned long capacity_curr_of(int cpu);

int set_ux_task_cpu_common_by_prio(struct task_struct *task, int *target_cpu,
				   bool boost, bool prefer_idle)
{
	int i;
	int lowest_prio = INT_MIN;
	unsigned long lowest_prio_max_cap = 0;
	int ret = -1;

	for_each_cpu(i, cpu_active_mask) {
		unsigned long capacity_curr;
		struct task_struct *curr;

		if (!cpumask_test_cpu(i, &task->cpus_allowed) || cpu_isolated(i))
			continue;

		if (prefer_idle && idle_cpu(i)) {
			*target_cpu = i;
			return 0;
		}

		curr = cpu_rq(i)->curr;
		if (!curr || curr->sched_class != &fair_sched_class)
			continue;

		if (test_task_ux(curr))
			continue;

#ifdef CONFIG_CAMERA_OPT
		if (sysctl_camera_opt_enabled && curr->camera_opt)
			continue;
#endif
		capacity_curr = capacity_curr_of(i);
		if (curr->prio > lowest_prio ||
		    (boost && capacity_curr > lowest_prio_max_cap)) {
			lowest_prio = curr->prio;
			lowest_prio_max_cap = capacity_curr;
			*target_cpu = i;
			ret = 0;
		}
	}

	return ret;
}

void select_cpu_dur_anim(struct task_struct *task, int *target_cpu)
{
	int i;
	int lowest_prio = INT_MIN;
	unsigned long max_cap = 0;
	unsigned long capacity_curr = 0;
	struct task_struct *curr = NULL;
	bool boost = (task->static_ux == 1);

	if (idle_cpu(*target_cpu) && !boost)
		return;

	for_each_cpu(i, cpu_active_mask) {
		if (!cpumask_test_cpu(i, &task->cpus_allowed) || cpu_isolated(i))
			continue;

		if (idle_cpu(i) && !boost) {
			*target_cpu = i;
			return;
		}

		curr = cpu_rq(i)->curr;
		capacity_curr = capacity_curr_of(i);

		if (!curr || curr->sched_class != &fair_sched_class)
			continue;

		if (!test_task_ux(curr) &&
		    ((boost && capacity_curr > max_cap) ||
		     curr->prio > lowest_prio)) {
			max_cap = capacity_curr;
			lowest_prio = curr->prio;
			*target_cpu = i;
		}
	}
}

static bool is_sf(struct task_struct *p)
{
	return p && strcmp(p->comm, "surfaceflinger") == 0 && p->pid == p->tgid;
}

#ifdef CONFIG_CAMERA_OPT
void set_camera_opt(struct task_struct *p)
{
	struct task_struct *grp_leader = p ? p->group_leader : NULL;

	if (!grp_leader)
		return;

	if (strstr(grp_leader->comm, CAMERA_PROVIDER_NAME) ||
	    strstr(grp_leader->comm, CAMERA_HAL_SERVER_NAME)) {
		if (p->sched_class == &fair_sched_class)
			p->camera_opt = 1;
		return;
	}

	if (strstr(grp_leader->comm, CAMERA_APP_KEYWORDS) &&
	    strstr(p->comm, CAMERA_HALCONT_NAME)) {
		p->static_ux = 1;
	}
}
#else
void set_camera_opt(struct task_struct *p)
{
}
#endif

void drop_ux_task_cpus(struct task_struct *p, struct cpumask *lowest_mask)
{
	unsigned int cpu = cpumask_first(lowest_mask);
#ifdef CONFIG_SCHED_WALT
	bool sf = is_sf(p);
#endif

	while (cpu < nr_cpu_ids) {
		struct task_struct *task = READ_ONCE(cpu_rq(cpu)->curr);

#ifdef CONFIG_CAMERA_OPT
		if (task->static_ux == 1 || task->static_ux == 2 ||
		    (sysctl_camera_opt_enabled && task->camera_opt == 1))
#else
		if (task->static_ux == 1 || task->static_ux == 2)
#endif
			cpumask_clear_cpu(cpu, lowest_mask);

#ifdef CONFIG_SCHED_WALT
		if ((sysctl_slide_boost_enabled == 2 ||
		     sysctl_input_boost_enabled) && sf) {
			if (cpu < ux_prefer_cpu[0] || task->static_ux == 2)
				cpumask_clear_cpu(cpu, lowest_mask);
		}
#endif
		cpu = cpumask_next(cpu, lowest_mask);
	}
}

static inline struct task_struct *ux_task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
#define ux_entity_is_task(se)	(!(se)->my_q)
#else
#define ux_entity_is_task(se)	(1)
#endif

void place_entity_adjust_ux_task(struct cfs_rq *cfs_rq,
				 struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;
	unsigned long thresh = sysctl_sched_latency;
	struct task_struct *se_task = NULL;

	if (unlikely(!sysctl_uifirst_enabled))
		return;

	if (!ux_entity_is_task(se) || initial)
		return;

	se_task = ux_task_of(se);
#ifdef CONFIG_MMAP_LOCK_OPT
	if (sysctl_uxchain_v2 && se_task->ux_once) {
		se->vruntime = vruntime - (5 * thresh);
		se_task->ux_once = 0;
		return;
	}
#endif

#ifdef CONFIG_CAMERA_OPT
#ifdef CONFIG_UIFIRST_AUDIO_OPT
	if (se_task->static_ux == 1 ||
	    (se_task->camera_opt && sysctl_camera_opt_enabled &&
	     !sysctl_sched_impt_tgid)) {
#else
	if (se_task->static_ux == 1 ||
	    (se_task->camera_opt && sysctl_camera_opt_enabled)) {
#endif
#else
	if (se_task->static_ux == 1) {
#endif
		se->vruntime = vruntime - (4 * thresh);
		return;
	}

	if (test_dynamic_ux(se_task, DYNAMIC_UX_BINDER) &&
	    is_sf(se_task->group_leader)) {
		se->vruntime = vruntime - (3 * thresh);
	}
}

bool should_ux_task_skip_further_check(struct sched_entity *se)
{
	return ux_entity_is_task(se) && (ux_task_of(se)->static_ux == 1);
}

bool should_ux_preempt_wakeup(struct task_struct *wake_task,
			      struct task_struct *curr_task)
{
	bool wake_ux = false;
	bool curr_ux = false;

	if (!sysctl_uifirst_enabled)
		return false;

#ifdef CONFIG_CAMERA_OPT
	wake_ux = test_task_ux(wake_task) ||
		  (sysctl_camera_opt_enabled && curr_task->camera_opt);
	curr_ux = test_task_ux(curr_task) ||
		  (sysctl_camera_opt_enabled && curr_task->camera_opt);
#else
	wake_ux = test_task_ux(wake_task);
	curr_ux = test_task_ux(curr_task);
#endif

	if (wake_ux && !curr_ux)
		return true;

#ifdef CONFIG_CAMERA_OPT
	if ((wake_task->static_ux == 1 ||
	     (sysctl_camera_opt_enabled && wake_task->camera_opt)) &&
	    curr_task->static_ux != 1)
#else
	if (wake_task->static_ux == 1 && curr_task->static_ux != 1)
#endif
		return true;

	return false;
}

bool ux_skip_sync_wakeup(struct task_struct *task, int *sync)
{
	if (test_dynamic_ux(task, DYNAMIC_UX_BINDER) &&
	    current->static_ux != 2) {
		*sync = 0;
		return true;
	}

	return false;
}

static inline bool ux_is_min_capacity_cpu(int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;

	if (unlikely(cls_nr <= 0))
		return false;

	return capacity_orig_of(cpu) == ux_cputopo.sched_cls[0].capacity;
}

/*
 * Procfs interface: /proc/<pid>/task/<tid>/static_ux
 */
bool is_special_entry(struct dentry *dentry, const char *special_proc)
{
	const unsigned char *name;

	if (!dentry || !special_proc)
		return false;

	name = dentry->d_name.name;
	return name && !strncmp(special_proc, name, 32);
}

static int proc_static_ux_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p = get_proc_task(inode);

	if (!p)
		return -ESRCH;

	task_lock(p);
	seq_printf(m, "%d\n", p->static_ux);
	task_unlock(p);
	put_task_struct(p);
	return 0;
}

static int proc_static_ux_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_static_ux_show, inode);
}

static ssize_t proc_static_ux_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	int err, static_ux;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtoint(strstrip(buffer), 0, &static_ux);
	if (err)
		return err;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	task->static_ux = static_ux;
	if (param_ux_debug == UX_DEBUG_LEVEL_LIGHT)
		sched_assist_systrace_pid(task->tgid, task->static_ux,
					  "static_ux %d", task->pid);

	put_task_struct(task);
	return count;
}

static ssize_t proc_static_ux_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	char buffer[128];
	struct task_struct *task = get_proc_task(file_inode(file));
	int static_ux = -1;
	u64 dyn_ux = 0;
	bool bi, fu, rw, mu;
	size_t len = 0;

	if (!task)
		return -ESRCH;

	static_ux = task->static_ux;
	dyn_ux = atomic64_read(&task->dynamic_ux);
	bi = test_dynamic_ux(task, DYNAMIC_UX_BINDER);
	fu = test_dynamic_ux(task, DYNAMIC_UX_FUTEX);
	rw = test_dynamic_ux(task, DYNAMIC_UX_RWSEM);
	mu = test_dynamic_ux(task, DYNAMIC_UX_MUTEX);
	put_task_struct(task);

	len = snprintf(buffer, sizeof(buffer),
		       "static=%d dynamic=%llx(bi:%d fu:%d rw:%d mu:%d)\n",
		       static_ux, dyn_ux, bi, fu, rw, mu);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct file_operations proc_static_ux_operations = {
	.open		= proc_static_ux_open,
	.write		= proc_static_ux_write,
	.read		= proc_static_ux_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#ifdef CONFIG_CAMERA_OPT
static int proc_camera_opt_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p = get_proc_task(inode);

	if (!p)
		return -ESRCH;

	task_lock(p);
	seq_printf(m, "%d\n", p->camera_opt);
	task_unlock(p);
	put_task_struct(p);
	return 0;
}

static int proc_camera_opt_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_camera_opt_show, inode);
}

static ssize_t proc_camera_opt_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	char buffer[128];
	struct task_struct *task = get_proc_task(file_inode(file));
	int camera_opt = -1;
	size_t len = 0;

	if (!task)
		return -ESRCH;

	camera_opt = task->camera_opt;
	put_task_struct(task);

	len = snprintf(buffer, sizeof(buffer), "is_opt:%d\n", camera_opt);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_camera_opt_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	int err, camera_opt;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	err = kstrtoint(strstrip(buffer), 0, &camera_opt);
	if (err)
		return err;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	task->camera_opt = camera_opt;
	put_task_struct(task);
	return count;
}

const struct file_operations proc_camera_opt_operations = {
	.open		= proc_camera_opt_open,
	.read		= proc_camera_opt_read,
	.write		= proc_camera_opt_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* CONFIG_CAMERA_OPT */

int sysctl_sched_assist_input_boost_ctrl_handler(struct ctl_table *table,
						 int write,
						 void __user *buffer,
						 size_t *lenp, loff_t *ppos)
{
	int result;
	static DEFINE_MUTEX(sa_boost_mutex);

	mutex_lock(&sa_boost_mutex);
	result = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write)
		goto out;

	sched_assist_input_boost_duration =
		jiffies_to_msecs(jiffies) +
		DEFAULT_INPUT_BOOST_DURATION / sched_assist_ib_duration_coedecay;
out:
	mutex_unlock(&sa_boost_mutex);
	return result;
}

static inline void sched_init_ux_cputopo(void)
{
	int i = 0;

	ux_sched_cputopo.cls_nr = 0;
	ux_sched_cputopo.big_cluster_idx = -1;
	for (; i < NR_CPUS; ++i) {
		cpumask_clear(&ux_sched_cputopo.sched_cls[i].cpus);
		ux_sched_cputopo.sched_cls[i].capacity = ULONG_MAX;
	}
}

static int get_big_cluster_id(void)
{
	int i;
	int idx = 0;

	for (i = 1; i < ux_sched_cputopo.cls_nr; i++) {
		if (ux_sched_cputopo.sched_cls[i].capacity >
		    ux_sched_cputopo.sched_cls[idx].capacity)
			idx = i;
	}
	return idx;
}

void update_ux_sched_cputopo(void)
{
	unsigned long prev_cap = 0;
	unsigned long cpu_cap = 0;
	unsigned int cpu = 0;
	int i, insert_idx = 0, cls_nr;
	struct ux_sched_cluster sched_cls;

	sched_init_ux_cputopo();

	for_each_possible_cpu(cpu) {
		cpu_cap = arch_scale_cpu_capacity(NULL, cpu);

		if (cpu_cap == prev_cap) {
			for (i = 0; i < ux_sched_cputopo.cls_nr; ++i) {
				if (cpu_cap ==
				    ux_sched_cputopo.sched_cls[i].capacity) {
					cpumask_set_cpu(
						cpu,
						&ux_sched_cputopo.sched_cls[i].cpus);
					break;
				}
			}
			continue;
		}

		cpumask_clear(&sched_cls.cpus);
		cpumask_set_cpu(cpu, &sched_cls.cpus);
		sched_cls.capacity = cpu_cap;
		cls_nr = ux_sched_cputopo.cls_nr;

		if (!cls_nr) {
			ux_sched_cputopo.sched_cls[cls_nr] = sched_cls;
		} else {
			insert_idx = ux_sched_cputopo.cls_nr;
			for (i = 0; i <= ux_sched_cputopo.cls_nr; ++i) {
				if (sched_cls.capacity <
				    ux_sched_cputopo.sched_cls[i].capacity) {
					insert_idx = i;
					break;
				}
			}
			if (insert_idx == ux_sched_cputopo.cls_nr) {
				ux_sched_cputopo.sched_cls[insert_idx] = sched_cls;
			} else {
				for (; cls_nr > insert_idx; cls_nr--) {
					ux_sched_cputopo.sched_cls[cls_nr] =
						ux_sched_cputopo.sched_cls[cls_nr - 1];
				}
				ux_sched_cputopo.sched_cls[insert_idx] = sched_cls;
			}
		}
		ux_sched_cputopo.cls_nr++;
		prev_cap = cpu_cap;
	}

	for (i = 0; i < ux_sched_cputopo.cls_nr; i++)
		ux_debug("update ux sched cpu topology [cls_nr:%d cpus:%*pbl cap:%lu]",
			 i, cpumask_pr_args(&ux_sched_cputopo.sched_cls[i].cpus),
			 ux_sched_cputopo.sched_cls[i].capacity);

	ux_sched_cputopo.big_cluster_idx = get_big_cluster_id();
}

void find_ux_task_cpu_capacity(struct task_struct *tsk, int *target_cpu)
{
	int cpu;
	struct rq *rq = NULL;

	if (unlikely(ux_sched_cputopo.big_cluster_idx < 0))
		return;

	for_each_cpu(cpu,
		     &ux_sched_cputopo.sched_cls[ux_sched_cputopo.big_cluster_idx].cpus) {
		rq = cpu_rq(cpu);
		if (!rq || rq->curr->prio <= MAX_RT_PRIO)
			continue;

		if (!test_task_ux(rq->curr) && cpu_online(cpu) &&
		    !cpu_isolated(cpu) &&
		    cpumask_test_cpu(cpu, &tsk->cpus_allowed)) {
			*target_cpu = cpu;
			return;
		}
	}
}

#ifdef CONFIG_MMAP_LOCK_OPT
void uxchain_rwsem_wake(struct task_struct *tsk, struct rw_semaphore *sem)
{
	if (unlikely(!sysctl_uifirst_enabled || !sysctl_uxchain_v2))
		return;
	if (current->mm && sem == &current->mm->mmap_sem)
		tsk->ux_once = 1;
}

void uxchain_rwsem_down(struct rw_semaphore *sem)
{
	if (unlikely(!sysctl_uifirst_enabled || !sysctl_uxchain_v2))
		return;
	if (current->mm && sem == &current->mm->mmap_sem) {
		current->get_mmlock = 1;
		current->get_mmlock_ts = sched_clock();
	}
}

void uxchain_rwsem_up(struct rw_semaphore *sem)
{
	if (unlikely(!sysctl_uifirst_enabled || !sysctl_uxchain_v2))
		return;
	if (current->mm && sem == &current->mm->mmap_sem &&
	    current->get_mmlock == 1)
		current->get_mmlock = 0;
}
#endif /* CONFIG_MMAP_LOCK_OPT */

bool is_task_util_over(struct task_struct *task, int threshold)
{
	bool sum_over = false;
	bool demand_over = false;

#ifdef CONFIG_SCHED_WALT
	sum_over = scale_demand(task->ravg.sum) >= threshold;
	demand_over = task_util(task) >= threshold;
#endif

	return sum_over || demand_over;
}

bool sched_assist_task_misfit(struct task_struct *task, int cpu, int flag)
{
	if (unlikely(!sysctl_uifirst_enabled))
		return false;

#ifdef CONFIG_CAMERA_OPT
	if (!task->camera_opt)
		return false;
#endif
	if (is_task_util_over(task, THRESHOLD_BOOST) &&
	    ux_is_min_capacity_cpu(cpu))
		return true;

	return false;
}

static inline int get_task_cls_for_scene(struct task_struct *task)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_max = ux_cputopo.cls_nr - 1;
	int cls_mid = cls_max - 1;

	if (unlikely(cls_max <= 0))
		return 0;

	if (cls_mid == 0)
		cls_mid = cls_max;

	if (!is_heavy_ux_task(task))
		return 0;

	if (sysctl_launcher_boost_enabled)
		return cls_max;

	if ((sysctl_animation_type || sysctl_input_boost_enabled ||
	     sysctl_slide_boost_enabled) &&
	    is_task_util_over(task, sysctl_boost_task_threshold))
		return cls_mid;

	return 0;
}

static inline bool is_ux_task_prefer_cpu_for_scene(struct task_struct *task,
						   unsigned int cpu)
{
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_id = ux_cputopo.cls_nr - 1;

	if (unlikely(cls_id <= 0))
		return true;

	cls_id = get_task_cls_for_scene(task);
	return capacity_orig_of(cpu) >= ux_cputopo.sched_cls[cls_id].capacity;
}

bool should_ux_task_skip_cpu(struct task_struct *task, unsigned int cpu)
{
	if (!sysctl_uifirst_enabled || !test_task_ux(task))
		return false;

	if (!is_ux_task_prefer_cpu_for_scene(task, cpu))
		return true;

	if (!sysctl_launcher_boost_enabled || !is_heavy_ux_task(task)) {
		if (cpu_rq(cpu)->rt.rt_nr_running)
			return true;

		if (cpu_rq(cpu)->curr &&
		    (is_animator_ux_task(task) ||
		     !list_empty(&cpu_rq(cpu)->ux_thread_list)))
			return true;
	}

	return false;
}

void set_ux_task_to_prefer_cpu(struct task_struct *task, int *orig_target_cpu)
{
	struct rq *rq = NULL;
	struct ux_sched_cputopo ux_cputopo = ux_sched_cputopo;
	int cls_nr = ux_cputopo.cls_nr - 1;
	int cpu = 0;
	int direction = -1;

	if (!sysctl_uifirst_enabled || unlikely(cls_nr <= 0))
		return;

	if (is_ux_task_prefer_cpu_for_scene(task, *orig_target_cpu))
		return;

	cls_nr = get_task_cls_for_scene(task);
	if (cls_nr != ux_cputopo.cls_nr - 1)
		direction = 1;
retry:
	for_each_cpu(cpu, &ux_cputopo.sched_cls[cls_nr].cpus) {
		rq = cpu_rq(cpu);
		if (is_heavy_ux_task(task) || rq->curr->prio < MAX_RT_PRIO)
			continue;

		if (cpu_online(cpu) && !cpu_isolated(cpu) &&
		    cpumask_test_cpu(cpu, &task->cpus_allowed)) {
			if (param_ux_debug == UX_DEBUG_LEVEL_HEAVY)
				sched_assist_systrace(task->pid * 100 + cpu,
						      "ux_cpu");
			*orig_target_cpu = cpu;
			return;
		}
	}

	cls_nr = cls_nr + direction;
	if (cls_nr > 0 && cls_nr < ux_cputopo.cls_nr)
		goto retry;
}

static unsigned long __read_mostly mark_addr;

static int _sched_assist_update_tracemark(void)
{
	if (mark_addr)
		return 1;
	mark_addr = kallsyms_lookup_name("tracing_mark_write");
	return mark_addr != 0;
}

void sched_assist_systrace_pid(pid_t pid, int val, const char *fmt, ...)
{
	char log[256];
	va_list args;
	int len;

	if (likely(param_ux_debug == UX_DEBUG_LEVEL_NONE))
		return;
	if (unlikely(!_sched_assist_update_tracemark()))
		return;

	memset(log, ' ', sizeof(log));
	va_start(args, fmt);
	len = vsnprintf(log, sizeof(log), fmt, args);
	va_end(args);

	if (unlikely(len < 0))
		return;
	if (unlikely(len >= (int)sizeof(log)))
		log[sizeof(log) - 1] = '\0';

	preempt_disable();
	event_trace_printk(mark_addr, "C|%d|%s|%d\n", pid, log, val);
	preempt_enable();
}

#ifdef CONFIG_UIFIRST_AUDIO_OPT
/* Android audio thread priorities (from system/core/include/system/thread_defs.h) */
#define ANDROID_PRIORITY_URGENT_AUDIO	101
#define ANDROID_PRIORITY_AUDIO		104

#define FIT_SMALL_THRESH		3
#define RUNNING_MIN_THRESH_NS		3000000ULL

bool is_small_task(struct task_struct *task)
{
	return task && task->ux_audio_task_info.im_small;
}

bool is_audio_task(struct task_struct *task)
{
	return task && (task->prio == ANDROID_PRIORITY_URGENT_AUDIO ||
			task->prio == ANDROID_PRIORITY_AUDIO);
}

static inline bool is_audio_boost_target(struct task_struct *tsk)
{
	return is_audio_task(tsk) ||
	       (sysctl_sched_impt_tgid != 0 &&
		tsk->tgid == sysctl_sched_impt_tgid);
}

extern void clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se);

bool sched_assist_pick_next_task_opt(struct cfs_rq *cfs_rq,
				     struct sched_entity **se)
{
	if (cfs_rq && se && cfs_rq->next && ux_entity_is_task(cfs_rq->next)) {
		struct task_struct *tsk = ux_task_of(cfs_rq->next);

		if (tsk->ux_audio_task_info.im_small) {
			sched_assist_im_systrace_c(tsk, -1);
			*se = cfs_rq->next;
			clear_buddies(cfs_rq, *se);
			return true;
		}
	}
	return false;
}

static void update_task_sched_stat_common(struct task_struct *tsk, u64 delta_ns,
					  int stats_type)
{
	int i = TASK_INFO_SAMPLE - 1;

	if (unlikely(stats_type < 0 || stats_type >= TST_SCHED_TYPE_TOTAL))
		return;

	while (i > 0) {
		tsk->ux_audio_task_info.sa_info[stats_type][i] =
			tsk->ux_audio_task_info.sa_info[stats_type][i - 1];
		i--;
	}
	tsk->ux_audio_task_info.sa_info[stats_type][0] = delta_ns;
}

enum {
	TASK_SMALL = 0,
	TASK_TYPE_TOTAL,
};

static void try_to_mark_task_type(struct task_struct *tsk, int type)
{
	int i = 0;
	int fit_small = 0;
	struct task_info *info = &tsk->ux_audio_task_info;

	if (type != TASK_SMALL)
		return;

	if (info->sa_info[TST_EXEC][TASK_INFO_SAMPLE - 1] != 0 &&
	    info->sa_info[TST_SLEEP][TASK_INFO_SAMPLE - 1] != 0) {
		while (i < TASK_INFO_SAMPLE) {
			if (info->sa_info[TST_EXEC][i] < RUNNING_MIN_THRESH_NS &&
			    (info->sa_info[TST_EXEC][i] * 5) <
			    info->sa_info[TST_SLEEP][i]) {
				fit_small++;
			}
			if (fit_small >= FIT_SMALL_THRESH) {
				info->im_small = true;
				return;
			}
			i++;
		}
	}
	info->im_small = false;
}

void update_sa_task_stats(struct task_struct *tsk, u64 delta_ns, int stats_type)
{
	if (!tsk || !sysctl_uifirst_enabled)
		return;

	if (!is_audio_boost_target(tsk)) {
		if (unlikely(tsk->ux_audio_task_info.im_small))
			memset(&tsk->ux_audio_task_info, 0,
			       sizeof(struct task_info));
		return;
	}

	update_task_sched_stat_common(tsk, delta_ns, stats_type);
	try_to_mark_task_type(tsk, TASK_SMALL);
}

void sched_assist_update_record(struct task_struct *p, u64 delta_ns,
				int stats_type)
{
	update_sa_task_stats(p, delta_ns, stats_type);
	sched_assist_im_systrace_c(p, stats_type + 1);
}

void sched_assist_im_systrace_c(struct task_struct *tsk, int tst_type)
{
	if (likely(param_ux_debug != UX_DEBUG_LEVEL_LIGHT))
		return;
	if (!tsk || !is_audio_boost_target(tsk))
		return;
	if (unlikely(!_sched_assist_update_tracemark()))
		return;

	preempt_disable();
	event_trace_printk(mark_addr, "C|10001|short_run_%d_%d|%d\n",
			   tsk->pid, tst_type,
			   !!tsk->ux_audio_task_info.im_small);
	preempt_enable();
}

#ifdef CONFIG_SCHEDSTATS
static int __init uifirst_audio_opt_init(void)
{
	force_schedstat_enabled();
	return 0;
}
late_initcall(uifirst_audio_opt_init);
#endif
#endif /* CONFIG_UIFIRST_AUDIO_OPT */
