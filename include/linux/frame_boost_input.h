/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_FRAME_BOOST_INPUT_H
#define _LINUX_FRAME_BOOST_INPUT_H

#ifdef CONFIG_SCHED_FRAME_BOOST
extern void frame_boost_touch_event(void);
#else
static inline void frame_boost_touch_event(void) {}
#endif

#endif
