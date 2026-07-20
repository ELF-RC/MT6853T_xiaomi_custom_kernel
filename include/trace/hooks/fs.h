/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_do_sys_open,
	TP_PROTO(int dfd, const char __user *filename, int flags, umode_t mode, int *fd),
	TP_ARGS(dfd, filename, flags, mode, fd), 1);

DECLARE_HOOK(android_rvh_vfs_read,
	TP_PROTO(struct file *file, char __user *buf, size_t count, loff_t *pos, ssize_t *ret),
	TP_ARGS(file, buf, count, pos, ret));

DECLARE_HOOK(android_rvh_vfs_write,
	TP_PROTO(struct file *file, const char __user *buf, size_t count, loff_t *pos, ssize_t *ret),
	TP_ARGS(file, buf, count, pos, ret));

#endif /* _TRACE_HOOK_FS_H */

#include <trace/define_trace.h>
