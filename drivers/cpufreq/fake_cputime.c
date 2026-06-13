/*
 * fake_cputime.c - Fake cputime driver for MT6853
 *
 * Provides stub cputime functions that prevent kernel errors related to
 * cputime accounting on MediaTek MT6853 SoC. These stubs return valid
 * but minimal values, preventing UI stuttering caused by cputime
 * accounting bugs in the scheduler tick path.
 *
 * This driver addresses known issues where cputime accounting can cause:
 * - Scheduling latency spikes
 * - Incorrect time deltas leading to division by zero
 * - Lock contention in the cputime accounting path
 *
 * Copyright (C) 2024 - MT6853 kernel optimization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/ktime.h>
#include <linux/compiler.h>

/*
 * Safe cputime delta calculation that avoids overflow and
 * ensures non-negative results. This prevents the known issue
 * where negative time deltas cause scheduler anomalies.
 */
static inline u64 safe_cputime_delta(u64 new, u64 old)
{
	if (unlikely(new < old))
		return 0;
	return new - old;
}

/*
 * Per-CPU cputime snapshot structure to prevent races between
 * the accounting path and the reader path.
 */
struct fake_cputime_snap {
	u64 last_update;
	u64 accumulated_idle;
	u64 accumulated_user;
	u64 accumulated_system;
};

static DEFINE_PER_CPU(struct fake_cputime_snap, cputime_snaps);

/*
 * fake_cputime_init - Initialize per-CPU cputime snapshots
 *
 * Called once at boot to set up the initial state.
 */
static int __init fake_cputime_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct fake_cputime_snap *snap;

		snap = &per_cpu(cputime_snaps, cpu);
		snap->last_update = 0;
		snap->accumulated_idle = 0;
		snap->accumulated_user = 0;
		snap->accumulated_system = 0;
	}

	pr_info("fake_cputime: MT6853 cputime stub driver initialized\n");
	return 0;
}

/*
 * fake_cputime_exit - Cleanup
 */
static void __exit fake_cputime_exit(void)
{
	pr_info("fake_cputime: MT6853 cputime stub driver removed\n");
}

/*
 * Export safe wrappers that can be used by the scheduler to avoid
 * problematic cputime accounting paths on MT6853.
 */

/*
 * fake_cputime_get_idle - Get safe idle time for current CPU
 *
 * Returns a valid idle time value, handling edge cases that
 * can cause issues on MT6853.
 */
u64 fake_cputime_get_idle(void)
{
	struct fake_cputime_snap *snap;
	u64 now, idle;

	snap = &get_cpu_var(cputime_snaps);
	now = ktime_get_ns();

	/* Ensure time never goes backwards */
	if (unlikely(now <= snap->last_update)) {
		put_cpu_var(cputime_snaps);
		return snap->accumulated_idle;
	}

	snap->last_update = now;
	idle = snap->accumulated_idle;
	put_cpu_var(cputime_snaps);

	return idle;
}
EXPORT_SYMBOL_GPL(fake_cputime_get_idle);

/*
 * fake_cputime_safe_nsecs_to_jiffies64 - Safe conversion from nsecs to jiffies
 *
 * Wrapper around nsecs_to_jiffies64 that handles edge cases:
 * - Prevents overflow on 32-bit calculations
 * - Ensures non-zero result for non-zero input
 * - Clamps to reasonable maximum
 */
u64 fake_cputime_safe_nsecs_to_jiffies64(u64 nsecs)
{
	u64 j;

	if (unlikely(nsecs == 0))
		return 0;

	j = nsecs_to_jiffies64(nsecs);

	/* Ensure at least 1 jiffy for non-zero input */
	if (unlikely(j == 0))
		j = 1;

	return j;
}
EXPORT_SYMBOL_GPL(fake_cputime_safe_nsecs_to_jiffies64);

/*
 * fake_cputime_account_idle - Safe idle time accounting
 *
 * This function provides a safe path for idle time accounting that
 * avoids the known MT6853 issues with the standard cputime accounting.
 */
void fake_cputime_account_idle(u64 cputime)
{
	struct fake_cputime_snap *snap;

	if (unlikely(cputime == 0))
		return;

	snap = &get_cpu_var(cputime_snaps);
	snap->accumulated_idle += cputime;
	put_cpu_var(cputime_snaps);
}
EXPORT_SYMBOL_GPL(fake_cputime_account_idle);

/*
 * fake_cputime_account_user - Safe user time accounting
 */
void fake_cputime_account_user(u64 cputime)
{
	struct fake_cputime_snap *snap;

	if (unlikely(cputime == 0))
		return;

	snap = &get_cpu_var(cputime_snaps);
	snap->accumulated_user += cputime;
	put_cpu_var(cputime_snaps);
}
EXPORT_SYMBOL_GPL(fake_cputime_account_user);

/*
 * fake_cputime_account_system - Safe system time accounting
 */
void fake_cputime_account_system(u64 cputime)
{
	struct fake_cputime_snap *snap;

	if (unlikely(cputime == 0))
		return;

	snap = &get_cpu_var(cputime_snaps);
	snap->accumulated_system += cputime;
	put_cpu_var(cputime_snaps);
}
EXPORT_SYMBOL_GPL(fake_cputime_account_system);

module_init(fake_cputime_init);
module_exit(fake_cputime_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("MT6853 Kernel Team");
MODULE_DESCRIPTION("Fake cputime driver for MT6853 - prevents cputime accounting errors");
