/*
 * Frame-aware scheduling boost for MT6853
 *
 * Copyright (C) 2024 - MT6853 kernel optimization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SCHED_FRAME_BOOST_H
#define _SCHED_FRAME_BOOST_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>

/* Frame boost modes */
#define FRAME_BOOST_DISABLED	0
#define FRAME_BOOST_LIGHT	1
#define FRAME_BOOST_AGGRESSIVE	2

/* Frame detection thresholds */
#define FRAME_DETECT_WINDOW_NS	20000000ULL	/* 20ms window */
#define FRAME_TARGET_FPS	60
#define FRAME_TARGET_NS		(1000000000ULL / FRAME_TARGET_FPS)
#define FRAME_LATENCY_THRESH_NS	16666666ULL	/* 16.67ms for 60fps */

/* Maximum number of tracked frame hint tasks */
#define FRAME_HINT_MAX_TASKS	16

/* Frame boost priority levels */
#define FRAME_BOOST_PRIO_LOW	1
#define FRAME_BOOST_PRIO_MED	2
#define FRAME_BOOST_PRIO_HIGH	3
#define FRAME_BOOST_PRIO_MAX	4

/* Per-task frame boost hint */
struct frame_hint {
	pid_t pid;
	u32 boost_prio;		/* boost priority level */
	ktime_t last_frame;	/* timestamp of last frame completion */
	ktime_t avg_frame_time;	/* moving average frame time */
	u32 frame_count;	/* frames completed in current window */
	bool is_render_thread;	/* whether this is a render thread */
};

/* Per-CPU frame boost state */
struct frame_boost_cpu {
	ktime_t window_start;	/* start of current detection window */
	u32 frames_in_window;	/* frame completions in window */
	u32 boost_level;	/* current boost level */
	bool boosted;		/* whether CPU is currently boosted */
	ktime_t boost_start;	/* when boost started */
	ktime_t boost_end;	/* when boost should end */
};

/* Global frame boost configuration */
struct frame_boost_config {
	u32 mode;		/* FRAME_BOOST_DISABLED/LIGHT/AGGRESSIVE */
	u32 target_fps;		/* target frames per second */
	u32 boost_duration_ms;	/* how long to boost after frame start */
	u32 boost_min_util;	/* minimum utilization during boost */
	u32 detect_window_ms;	/* frame detection window in ms */
	bool prefer_idle;	/* prefer idle CPUs during boost */
	bool prefer_high_cap;	/* prefer high-capacity CPUs during boost */
};

/* Frame boost statistics */
struct frame_boost_stats {
	atomic64_t total_frames;
	atomic64_t boosted_frames;
	atomic64_t boost_time_ns;
	atomic64_t missed_deadlines;
};

#ifdef CONFIG_SCHED_FRAME_BOOST

/* Initialize frame boost subsystem */
void frame_boost_init(void);

/* Called when a frame is submitted for rendering */
void frame_boost_frame_start(pid_t pid);

/* Called when a frame completes rendering */
void frame_boost_frame_done(pid_t pid);

/* Called when a touch input event is detected (pre-emptive boost) */
void frame_boost_touch_event(void);

/* Get current boost utilization for a CPU */
unsigned long frame_boost_cpu_util(int cpu);

/* Check if a task should get frame boost */
bool frame_boost_task_eligible(struct task_struct *p);

/* Get frame boost hint for task selection */
int frame_boost_get_hint(struct task_struct *p, int cpu);

/* Register a task as a frame hint task */
int frame_boost_register_task(pid_t pid, u32 boost_prio);

/* Unregister a frame hint task */
void frame_boost_unregister_task(pid_t pid);

/* Sysfs init for frame boost */
int frame_boost_sysfs_init(void);

#else /* !CONFIG_SCHED_FRAME_BOOST */

static inline void frame_boost_init(void) {}
static inline void frame_boost_frame_start(pid_t pid) {}
static inline void frame_boost_frame_done(pid_t pid) {}
static inline void frame_boost_touch_event(void) {}
static inline unsigned long frame_boost_cpu_util(int cpu) { return 0; }
static inline bool frame_boost_task_eligible(struct task_struct *p) { return false; }
static inline int frame_boost_get_hint(struct task_struct *p, int cpu) { return 0; }
static inline int frame_boost_register_task(pid_t pid, u32 prio) { return 0; }
static inline void frame_boost_unregister_task(pid_t pid) {}
static inline int frame_boost_sysfs_init(void) { return 0; }

#endif /* CONFIG_SCHED_FRAME_BOOST */

#endif /* _SCHED_FRAME_BOOST_H */
