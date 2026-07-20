/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

/* Memory management vendor hooks */
struct vm_fault;
DECLARE_RESTRICTED_HOOK(android_rvh_do_anonymous_page,
	TP_PROTO(struct vm_fault *vmf),
	TP_ARGS(vmf), 1);

#endif /* _TRACE_HOOK_MM_H */

#include <trace/define_trace.h>
