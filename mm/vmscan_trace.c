// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

#define CREATE_TRACE_POINTS
#include <trace/events/vmscan.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_direct_reclaim_begin);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_direct_reclaim_end);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_kswapd_sleep);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_kswapd_wake);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_wakeup_kswapd);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_lru_isolate);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_lru_shrink_inactive);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_lru_shrink_active);
EXPORT_TRACEPOINT_SYMBOL_GPL(mm_vmscan_inactive_list_is_low);
