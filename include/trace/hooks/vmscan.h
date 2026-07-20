/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct list_head;
struct pglist_data;
struct scan_control;
struct reclaim_stat;

DECLARE_HOOK(android_vh_shrink_page_list,
	TP_PROTO(struct list_head *page_list, struct pglist_data *pgdat,
		 struct scan_control *sc, struct reclaim_stat *stat,
		 unsigned long *nr_reclaimed),
	TP_ARGS(page_list, pgdat, sc, stat, nr_reclaimed));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
