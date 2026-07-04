/*
 * Frame-aware scheduling boost for MT6853
 *
 * This module implements frame-aware CPU frequency boosting for the
 * MT6853 SoC. It detects frame rendering patterns and boosts CPU
 * frequency during frame rendering to reduce latency and jank.
 *
 * Features:
 * - Frame rendering pattern detection
 * - Per-task frame boost hints
 * - CPU frequency boost during frame rendering
 * - Sysfs interface for configuration
 * - Frame deadline monitoring
 *
 * Copyright (C) 2024 - MT6853 kernel optimization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>

#include "sched.h"
#include "frame_boost.h"

/* Global frame boost state */
static struct frame_boost_config fb_config = {
	.mode = FRAME_BOOST_AGGRESSIVE,
	.target_fps = 60,
	.boost_duration_ms = 150,
	.boost_min_util = 640,
	.detect_window_ms = 12,
	.prefer_idle = true,
	.prefer_high_cap = true,
};

static struct frame_boost_stats fb_stats;

/* Touch input boost */
static struct {
	bool enabled;
	u32 duration_ms;
	u32 boost_min_util;
	u32 boost_prio;
	ktime_t last_touch;
} fb_touch_boost = {
	.enabled = true,
	.duration_ms = 100,
	.boost_min_util = 400,
	.boost_prio = FRAME_BOOST_PRIO_HIGH,
};

/* Per-CPU frame boost state */
static DEFINE_PER_CPU(struct frame_boost_cpu, fb_cpu_state);

/* Frame hint task table */
static struct frame_hint fb_hint_tasks[FRAME_HINT_MAX_TASKS];
static DEFINE_SPINLOCK(fb_hint_lock);

/* Sysfs kobject */
static struct kobject *frame_boost_kobj;

/*
 * frame_boost_init - Initialize the frame boost subsystem
 *
 * Called during scheduler initialization to set up per-CPU state
 * and default configuration.
 */
void __init frame_boost_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct frame_boost_cpu *fbc = &per_cpu(fb_cpu_state, cpu);

		fbc->window_start = 0;
		fbc->frames_in_window = 0;
		fbc->boost_level = 0;
		fbc->boosted = false;
		fbc->boost_start = 0;
		fbc->boost_end = 0;
	}

	memset(fb_hint_tasks, 0, sizeof(fb_hint_tasks));
	atomic64_set(&fb_stats.total_frames, 0);
	atomic64_set(&fb_stats.boosted_frames, 0);
	atomic64_set(&fb_stats.boost_time_ns, 0);
	atomic64_set(&fb_stats.missed_deadlines, 0);

	pr_info("frame_boost: Initialized with mode=%u target_fps=%u\n",
		fb_config.mode, fb_config.target_fps);
}

/*
 * find_hint_slot - Find or allocate a slot for a frame hint task
 *
 * Must be called with fb_hint_lock held.
 */
static struct frame_hint *find_hint_slot(pid_t pid)
{
	int i;
	struct frame_hint *empty = NULL;

	for (i = 0; i < FRAME_HINT_MAX_TASKS; i++) {
		if (fb_hint_tasks[i].pid == pid)
			return &fb_hint_tasks[i];
		if (!empty && !fb_hint_tasks[i].pid)
			empty = &fb_hint_tasks[i];
	}

	return empty;
}

/*
 * frame_boost_register_task - Register a task for frame boost
 * @pid: Process ID of the task
 * @boost_prio: Boost priority level
 *
 * Returns 0 on success, negative error code on failure.
 */
int frame_boost_register_task(pid_t pid, u32 boost_prio)
{
	struct frame_hint *hint;
	unsigned long flags;

	if (boost_prio > FRAME_BOOST_PRIO_MAX)
		return -EINVAL;

	spin_lock_irqsave(&fb_hint_lock, flags);
	hint = find_hint_slot(pid);
	if (!hint) {
		spin_unlock_irqrestore(&fb_hint_lock, flags);
		return -ENOSPC;
	}

	hint->pid = pid;
	hint->boost_prio = boost_prio;
	hint->last_frame = 0;
	hint->avg_frame_time = 0;
	hint->frame_count = 0;
	hint->is_render_thread = true;
	spin_unlock_irqrestore(&fb_hint_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(frame_boost_register_task);

/*
 * frame_boost_unregister_task - Unregister a frame hint task
 * @pid: Process ID of the task
 */
void frame_boost_unregister_task(pid_t pid)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&fb_hint_lock, flags);
	for (i = 0; i < FRAME_HINT_MAX_TASKS; i++) {
		if (fb_hint_tasks[i].pid == pid) {
			memset(&fb_hint_tasks[i], 0, sizeof(struct frame_hint));
			break;
		}
	}
	spin_unlock_irqrestore(&fb_hint_lock, flags);
}
EXPORT_SYMBOL_GPL(frame_boost_unregister_task);

/*
 * get_hint_for_pid - Get frame hint for a specific PID
 *
 * Must be called with fb_hint_lock held.
 */
static struct frame_hint *get_hint_for_pid(pid_t pid)
{
	int i;

	for (i = 0; i < FRAME_HINT_MAX_TASKS; i++) {
		if (fb_hint_tasks[i].pid == pid)
			return &fb_hint_tasks[i];
	}

	return NULL;
}

/*
 * frame_boost_frame_start - Called when a frame begins rendering
 * @pid: Process ID of the rendering task
 *
 * This function triggers CPU frequency boost when a frame starts
 * rendering. It updates the per-CPU boost state and records the
 * frame start time for deadline monitoring.
 */
void frame_boost_frame_start(pid_t pid)
{
	struct frame_hint *hint;
	struct frame_boost_cpu *fbc;
	unsigned long flags;
	ktime_t now;
	int cpu;

	if (fb_config.mode == FRAME_BOOST_DISABLED)
		return;

	now = ktime_get();

	spin_lock_irqsave(&fb_hint_lock, flags);
	hint = get_hint_for_pid(pid);
	if (!hint) {
		spin_unlock_irqrestore(&fb_hint_lock, flags);
		return;
	}

	hint->last_frame = now;
	hint->frame_count++;
	spin_unlock_irqrestore(&fb_hint_lock, flags);

	atomic64_inc(&fb_stats.total_frames);

	/* Boost current CPU */
	cpu = smp_processor_id();
	fbc = &per_cpu(fb_cpu_state, cpu);

	fbc->boosted = true;
	fbc->boost_start = now;
	fbc->boost_end = ktime_add_ms(now, fb_config.boost_duration_ms);
	fbc->boost_level = hint->boost_prio;
	fbc->frames_in_window++;

	atomic64_inc(&fb_stats.boosted_frames);
}
EXPORT_SYMBOL_GPL(frame_boost_frame_start);

/*
 * frame_boost_frame_done - Called when a frame completes rendering
 * @pid: Process ID of the rendering task
 *
 * This function is called when a frame finishes rendering. It checks
 * whether the frame deadline was met and updates statistics.
 */
void frame_boost_frame_done(pid_t pid)
{
	struct frame_hint *hint;
	unsigned long flags;
	ktime_t now, frame_time;

	if (fb_config.mode == FRAME_BOOST_DISABLED)
		return;

	now = ktime_get();

	spin_lock_irqsave(&fb_hint_lock, flags);
	hint = get_hint_for_pid(pid);
	if (!hint || !hint->last_frame) {
		spin_unlock_irqrestore(&fb_hint_lock, flags);
		return;
	}

	frame_time = ktime_sub(now, hint->last_frame);

	/* Update moving average frame time */
	if (hint->avg_frame_time == 0)
		hint->avg_frame_time = frame_time;
	else
		hint->avg_frame_time = ktime_add(
			ktime_divns(hint->avg_frame_time, 4),
			ktime_divns(frame_time, 4));
	/* EMA: avg = (3*avg + new) / 4 */

	spin_unlock_irqrestore(&fb_hint_lock, flags);

	/* Check if frame deadline was missed */
	if (ktime_to_ns(frame_time) > FRAME_LATENCY_THRESH_NS)
		atomic64_inc(&fb_stats.missed_deadlines);

	/* Clear boost on current CPU */
	{
		int cpu = smp_processor_id();
		struct frame_boost_cpu *fbc = &per_cpu(fb_cpu_state, cpu);

		if (fbc->boosted) {
			u64 boost_ns = ktime_to_ns(ktime_sub(now, fbc->boost_start));

			atomic64_add(boost_ns, &fb_stats.boost_time_ns);
		}
		fbc->boosted = false;
		fbc->boost_level = 0;
	}
}
EXPORT_SYMBOL_GPL(frame_boost_frame_done);

void frame_boost_touch_event(void)
{
	ktime_t now, expire;
	struct frame_boost_cpu *fbc;
	int cpu;

	if (!fb_touch_boost.enabled)
		return;
	if (fb_config.mode == FRAME_BOOST_DISABLED)
		return;

	now = ktime_get();
	expire = ktime_add_ms(now, fb_touch_boost.duration_ms);

	preempt_disable();
	for_each_online_cpu(cpu) {
		fbc = &per_cpu(fb_cpu_state, cpu);
		if (fbc->boosted && ktime_before(now, fbc->boost_end))
			continue;
		fbc->boosted = true;
		fbc->boost_start = now;
		fbc->boost_end = expire;
		fbc->boost_level = fb_touch_boost.boost_prio;
		atomic64_inc(&fb_stats.boosted_frames);
	}
	preempt_enable();

	fb_touch_boost.last_touch = now;
}
EXPORT_SYMBOL_GPL(frame_boost_touch_event);

/*
 * frame_boost_cpu_util - Get frame boost utilization for a CPU
 * @cpu: CPU number
 *
 * Returns the minimum utilization boost if the CPU is currently
 * boosted for frame rendering, 0 otherwise.
 */
unsigned long frame_boost_cpu_util(int cpu)
{
	struct frame_boost_cpu *fbc;
	ktime_t now;

	if (fb_config.mode == FRAME_BOOST_DISABLED)
		return 0;

	fbc = &per_cpu(fb_cpu_state, cpu);

	if (!fbc->boosted)
		return 0;

	/* Check if boost has expired */
	now = ktime_get();
	if (ktime_after(now, fbc->boost_end)) {
		fbc->boosted = false;
		fbc->boost_level = 0;
		return 0;
	}

	/* Scale boost based on priority level */
	return fb_config.boost_min_util * fbc->boost_level;
}
EXPORT_SYMBOL_GPL(frame_boost_cpu_util);

/*
 * frame_boost_task_eligible - Check if a task should get frame boost
 * @p: Task to check
 *
 * Returns true if the task is registered as a frame hint task
 * and is currently eligible for boost.
 */
bool frame_boost_task_eligible(struct task_struct *p)
{
	struct frame_hint *hint;
	unsigned long flags;
	bool eligible = false;

	if (fb_config.mode == FRAME_BOOST_DISABLED)
		return false;

	spin_lock_irqsave(&fb_hint_lock, flags);
	hint = get_hint_for_pid(p->pid);
	if (hint && hint->is_render_thread)
		eligible = true;
	spin_unlock_irqrestore(&fb_hint_lock, flags);

	return eligible;
}
EXPORT_SYMBOL_GPL(frame_boost_task_eligible);

/*
 * frame_boost_get_hint - Get frame boost hint for task placement
 * @p: Task being placed
 * @cpu: CPU being considered
 *
 * Returns a score indicating how suitable the CPU is for the task
 * during frame boost. Higher values are better.
 */
int frame_boost_get_hint(struct task_struct *p, int cpu)
{
	struct frame_boost_cpu *fbc;
	int score = 0;

	if (fb_config.mode == FRAME_BOOST_DISABLED)
		return 0;

	if (!frame_boost_task_eligible(p))
		return 0;

	fbc = &per_cpu(fb_cpu_state, cpu);

	/* If this CPU is already boosted, prefer it */
	if (fbc->boosted)
		score += 10;

	/* Prefer idle CPUs if configured */
	if (fb_config.prefer_idle) {
		if (idle_cpu(cpu))
			score += 5;
	}

	/* Prefer high-capacity CPUs if configured */
	if (fb_config.prefer_high_cap) {
		/* MT6853: CPUs 6-7 are typically big cores */
		if (cpu >= 6)
			score += 3;
	}

	return score;
}
EXPORT_SYMBOL_GPL(frame_boost_get_hint);

/* ===== Sysfs Interface ===== */

static ssize_t frame_boost_mode_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_config.mode);
}

static ssize_t frame_boost_mode_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	u32 mode;
	int ret;

	ret = kstrtou32(buf, 10, &mode);
	if (ret)
		return ret;

	if (mode > FRAME_BOOST_AGGRESSIVE)
		return -EINVAL;

	fb_config.mode = mode;
	return count;
}

static struct kobj_attribute frame_boost_mode_attr =
	__ATTR(mode, 0644, frame_boost_mode_show, frame_boost_mode_store);

static ssize_t frame_boost_target_fps_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_config.target_fps);
}

static ssize_t frame_boost_target_fps_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf, size_t count)
{
	u32 fps;
	int ret;

	ret = kstrtou32(buf, 10, &fps);
	if (ret)
		return ret;

	if (fps < 30 || fps > 144)
		return -EINVAL;

	fb_config.target_fps = fps;
	return count;
}

static struct kobj_attribute frame_boost_target_fps_attr =
	__ATTR(target_fps, 0644, frame_boost_target_fps_show,
	       frame_boost_target_fps_store);

static ssize_t frame_boost_duration_show(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_config.boost_duration_ms);
}

static ssize_t frame_boost_duration_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	u32 dur;
	int ret;

	ret = kstrtou32(buf, 10, &dur);
	if (ret)
		return ret;

	if (dur > 20)
		return -EINVAL;

	fb_config.boost_duration_ms = dur;
	return count;
}

static struct kobj_attribute frame_boost_duration_attr =
	__ATTR(boost_duration_ms, 0644, frame_boost_duration_show,
	       frame_boost_duration_store);

static ssize_t frame_boost_min_util_show(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_config.boost_min_util);
}

static ssize_t frame_boost_min_util_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	u32 util;
	int ret;

	ret = kstrtou32(buf, 10, &util);
	if (ret)
		return ret;

	if (util > 1024)
		return -EINVAL;

	fb_config.boost_min_util = util;
	return count;
}

static struct kobj_attribute frame_boost_min_util_attr =
	__ATTR(boost_min_util, 0644, frame_boost_min_util_show,
	       frame_boost_min_util_store);

static ssize_t frame_boost_prefer_idle_show(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_config.prefer_idle);
}

static ssize_t frame_boost_prefer_idle_store(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     const char *buf, size_t count)
{
	u32 val;
	int ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return ret;

	fb_config.prefer_idle = !!val;
	return count;
}

static struct kobj_attribute frame_boost_prefer_idle_attr =
	__ATTR(prefer_idle, 0644, frame_boost_prefer_idle_show,
	       frame_boost_prefer_idle_store);

static ssize_t frame_boost_prefer_high_cap_show(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_config.prefer_high_cap);
}

static ssize_t frame_boost_prefer_high_cap_store(struct kobject *kobj,
						  struct kobj_attribute *attr,
						  const char *buf, size_t count)
{
	u32 val;
	int ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return ret;

	fb_config.prefer_high_cap = !!val;
	return count;
}

static struct kobj_attribute frame_boost_prefer_high_cap_attr =
	__ATTR(prefer_high_cap, 0644, frame_boost_prefer_high_cap_show,
	       frame_boost_prefer_high_cap_store);

static ssize_t frame_boost_stats_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
		"total_frames:     %llu\n"
		"boosted_frames:   %llu\n"
		"boost_time_ns:    %llu\n"
		"missed_deadlines: %llu\n",
		atomic64_read(&fb_stats.total_frames),
		atomic64_read(&fb_stats.boosted_frames),
		atomic64_read(&fb_stats.boost_time_ns),
		atomic64_read(&fb_stats.missed_deadlines));
}

static struct kobj_attribute frame_boost_stats_attr =
	__ATTR(stats, 0444, frame_boost_stats_show, NULL);

static ssize_t frame_boost_tasks_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	ssize_t sz = 0;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&fb_hint_lock, flags);
	for (i = 0; i < FRAME_HINT_MAX_TASKS; i++) {
		if (fb_hint_tasks[i].pid) {
			sz += scnprintf(buf + sz, PAGE_SIZE - sz,
				"pid=%d prio=%u frames=%u avg_ns=%lld render=%d\n",
				fb_hint_tasks[i].pid,
				fb_hint_tasks[i].boost_prio,
				fb_hint_tasks[i].frame_count,
				ktime_to_ns(fb_hint_tasks[i].avg_frame_time),
				fb_hint_tasks[i].is_render_thread);
		}
	}
	spin_unlock_irqrestore(&fb_hint_lock, flags);

	if (sz == 0)
		sz = scnprintf(buf, PAGE_SIZE, "no registered tasks\n");

	return sz;
}

/*
 * frame_boost_tasks_store - Register/unregister frame hint tasks
 *
 * Format: "add <pid> <prio>" or "del <pid>"
 */
static ssize_t frame_boost_tasks_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	char cmd[16];
	pid_t pid;
	u32 prio;
	int ret;

	if (sscanf(buf, "add %d %u", &pid, &prio) == 2) {
		ret = frame_boost_register_task(pid, prio);
		if (ret)
			return ret;
	} else if (sscanf(buf, "del %d", &pid) == 1) {
		frame_boost_unregister_task(pid);
	} else {
		return -EINVAL;
	}

	return count;
}

static struct kobj_attribute frame_boost_tasks_attr =
	__ATTR(tasks, 0644, frame_boost_tasks_show, frame_boost_tasks_store);


static ssize_t touch_boost_enabled_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_touch_boost.enabled);
}

static ssize_t touch_boost_enabled_store(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 const char *buf, size_t count)
{
	u32 val;
	int ret;
	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return ret;
	fb_touch_boost.enabled = !!val;
	return count;
}
static struct kobj_attribute touch_boost_enabled_attr =
	__ATTR(touch_boost_enabled, 0644, touch_boost_enabled_show,
	       touch_boost_enabled_store);

static ssize_t touch_boost_duration_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", fb_touch_boost.duration_ms);
}

static ssize_t touch_boost_duration_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	u32 dur;
	int ret;
	ret = kstrtou32(buf, 10, &dur);
	if (ret)
		return ret;
	if (dur > 500)
		return -EINVAL;
	fb_touch_boost.duration_ms = dur;
	return count;
}
static struct kobj_attribute touch_boost_duration_attr =
	__ATTR(touch_boost_duration_ms, 0644, touch_boost_duration_show,
	       touch_boost_duration_store);
static struct attribute *frame_boost_attrs[] = {
	&frame_boost_mode_attr.attr,
	&frame_boost_target_fps_attr.attr,
	&frame_boost_duration_attr.attr,
	&frame_boost_min_util_attr.attr,
	&frame_boost_prefer_idle_attr.attr,
	&frame_boost_prefer_high_cap_attr.attr,
	&frame_boost_stats_attr.attr,
	&frame_boost_tasks_attr.attr,
	&touch_boost_enabled_attr.attr,
	&touch_boost_duration_attr.attr,
	NULL,
};

static struct attribute_group frame_boost_attr_group = {
	.attrs = frame_boost_attrs,
};

/*
 * frame_boost_sysfs_init - Create sysfs entries for frame boost
 *
 * Creates /sys/kernel/frame_boost/ with configuration and stats files.
 */
int __init frame_boost_sysfs_init(void)
{
	int ret;

	frame_boost_kobj = kobject_create_and_add("frame_boost", kernel_kobj);
	if (!frame_boost_kobj) {
		pr_err("frame_boost: Failed to create sysfs kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(frame_boost_kobj, &frame_boost_attr_group);
	if (ret) {
		pr_err("frame_boost: Failed to create sysfs group: %d\n", ret);
		kobject_put(frame_boost_kobj);
		return ret;
	}

	pr_info("frame_boost: Sysfs interface created at /sys/kernel/frame_boost/\n");
	return 0;
}

static int __init frame_boost_late_init(void)
{
	return frame_boost_sysfs_init();
}
late_initcall(frame_boost_late_init);
