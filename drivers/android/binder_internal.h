/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_BINDER_INTERNAL_H
#define _LINUX_BINDER_INTERNAL_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/uidgid.h>

#ifdef CONFIG_MTK_ENG_BUILD
#define BINDER_WATCHDOG		"v0.1"
#endif
#define BINDER_USER_TRACKING	1

#ifdef BINDER_USER_TRACKING
#include <linux/rtc.h>
#include <linux/time.h>
#endif

#ifdef BINDER_WATCHDOG
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/kthread.h>

/*****************************************************************************/
/* Payload layout of addService():                                           */
/*                                                                           */
/* Parcel header | IServiceManager.descriptor | Parcel header | Service name */
/* (Please refer ServiceManagerNative.java:addService())                     */
/* IServiceManager.descriptor is 'android.os.IServiceManager' interleaved    */
/* with character '\0'.                                                      */
/* that is, 'a', '\0', 'n', '\0', 'd', '\0', 'r', '\0', 'o', ...             */
/*                                                                           */
/* so the offset of Service name                                             */
/* = Parcel header x2 + strlen(android.os.IServiceManager) x2                */
/* = 8x2 + 26x2 = 68                                                         */
/*****************************************************************************/
#define MAX_SERVICE_NAME_LEN    32
#define MAGIC_SERVICE_NAME_OFFSET       68
#define MAX_ENG_TRANS_LOG_BUFF_LEN      10240

enum wait_on_reason {
	WAIT_ON_NONE = 0U,
	WAIT_ON_READ = 1U,
	WAIT_ON_EXEC = 2U,
	WAIT_ON_REPLY_READ = 3U,
};
#endif

struct binder_context {
	struct binder_node *binder_context_mgr_node;
	struct mutex context_mgr_node_lock;
	kuid_t binder_context_mgr_uid;
	const char *name;
};

/**
 * struct binder_device - information about a binder device node
 * @hlist:          list of binder devices (only used for devices requested via
 *                  CONFIG_ANDROID_BINDER_DEVICES)
 * @miscdev:        information about a binder character device node
 * @context:        binder context information
 * @binderfs_inode: This is the inode of the root dentry of the super block
 *                  belonging to a binderfs mount.
 */
struct binder_device {
	struct hlist_node hlist;
	struct miscdevice miscdev;
	struct binder_context context;
	struct inode *binderfs_inode;
	refcount_t ref;
};

/**
 * binderfs_mount_opts - mount options for binderfs
 * @max: maximum number of allocatable binderfs binder devices
 * @stats_mode: enable binder stats in binderfs.
 */
struct binderfs_mount_opts {
	int max;
	int stats_mode;
};

/**
 * binderfs_info - information about a binderfs mount
 * @ipc_ns:         The ipc namespace the binderfs mount belongs to.
 * @control_dentry: This records the dentry of this binderfs mount
 *                  binder-control device.
 * @root_uid:       uid that needs to be used when a new binder device is
 *                  created.
 * @root_gid:       gid that needs to be used when a new binder device is
 *                  created.
 * @mount_opts:     The mount options in use.
 * @device_count:   The current number of allocated binder devices.
 * @proc_log_dir:   Pointer to the directory dentry containing process-specific
 *                  logs.
 */
struct binderfs_info {
	struct ipc_namespace *ipc_ns;
	struct dentry *control_dentry;
	kuid_t root_uid;
	kgid_t root_gid;
	struct binderfs_mount_opts mount_opts;
	int device_count;
	struct dentry *proc_log_dir;
};

extern const struct file_operations binder_fops;

extern char *binder_devices_param;

#ifdef CONFIG_ANDROID_BINDERFS
extern bool is_binderfs_device(const struct inode *inode);
extern struct dentry *binderfs_create_file(struct dentry *dir, const char *name,
					   const struct file_operations *fops,
					   void *data);
extern void binderfs_remove_file(struct dentry *dentry);
#else
static inline bool is_binderfs_device(const struct inode *inode)
{
	return false;
}
static inline struct dentry *binderfs_create_file(struct dentry *dir,
					   const char *name,
					   const struct file_operations *fops,
					   void *data)
{
	return NULL;
}
static inline void binderfs_remove_file(struct dentry *dentry) {}
#endif

#ifdef CONFIG_ANDROID_BINDERFS
extern int __init init_binderfs(void);
#else
static inline int __init init_binderfs(void)
{
	return 0;
}
#endif

int binder_stats_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_stats);

int binder_state_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_state);

int binder_transactions_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_transactions);

int binder_transaction_log_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_transaction_log);


#include <linux/atomic.h>
#include <uapi/linux/android/binder.h>
#include "binder_alloc.h"

/* ---- moved from binder.c so millet/binder_gki.c can see the private
 * structures (same layout as android-5.10 binder_internal.h) ---- */

enum binder_stat_types {
	BINDER_STAT_PROC,
	BINDER_STAT_THREAD,
	BINDER_STAT_NODE,
	BINDER_STAT_REF,
	BINDER_STAT_DEATH,
	BINDER_STAT_TRANSACTION,
	BINDER_STAT_TRANSACTION_COMPLETE,
	BINDER_STAT_COUNT
};

struct binder_stats {
	atomic_t br[_IOC_NR(BR_FAILED_REPLY) + 1];
	atomic_t bc[_IOC_NR(BC_REPLY_SG) + 1];
	atomic_t obj_created[BINDER_STAT_COUNT];
	atomic_t obj_deleted[BINDER_STAT_COUNT];
};

struct binder_work {
	struct list_head entry;

	enum binder_work_type {
		BINDER_WORK_TRANSACTION = 1,
		BINDER_WORK_TRANSACTION_COMPLETE,
		BINDER_WORK_RETURN_ERROR,
		BINDER_WORK_NODE,
		BINDER_WORK_DEAD_BINDER,
		BINDER_WORK_DEAD_BINDER_AND_CLEAR,
		BINDER_WORK_CLEAR_DEATH_NOTIFICATION,
	} type;
};

struct binder_error {
	struct binder_work work;
	uint32_t cmd;
};


struct binder_priority {
	unsigned int sched_policy;
	int prio;
};

struct binder_proc {
	struct hlist_node proc_node;
	struct rb_root threads;
	struct rb_root nodes;
	struct rb_root refs_by_desc;
	struct rb_root refs_by_node;
	struct list_head waiting_threads;
	int pid;
	struct task_struct *tsk;
	struct files_struct *files;
	struct mutex files_lock;
	const struct cred *cred;
	struct hlist_node deferred_work_node;
	int deferred_work;
	bool is_dead;

	struct list_head todo;
	struct binder_stats stats;
	struct list_head delivered_death;
	int max_threads;
	int requested_threads;
	int requested_threads_started;
	int tmp_ref;
	struct binder_priority default_priority;
	struct dentry *debugfs_entry;
	struct binder_alloc alloc;
	struct binder_context *context;
	spinlock_t inner_lock;
	spinlock_t outer_lock;
	struct dentry *binderfs_entry;
};

enum {
	BINDER_LOOPER_STATE_REGISTERED  = 0x01,
	BINDER_LOOPER_STATE_ENTERED     = 0x02,
	BINDER_LOOPER_STATE_EXITED      = 0x04,
	BINDER_LOOPER_STATE_INVALID     = 0x08,
	BINDER_LOOPER_STATE_WAITING     = 0x10,
	BINDER_LOOPER_STATE_POLL        = 0x20,
};

/**
 * struct binder_thread - binder thread bookkeeping
 * @proc:                 binder process for this thread
 *                        (invariant after initialization)
 * @rb_node:              element for proc->threads rbtree
 *                        (protected by @proc->inner_lock)
 * @waiting_thread_node:  element for @proc->waiting_threads list
 *                        (protected by @proc->inner_lock)
 * @pid:                  PID for this thread
 *                        (invariant after initialization)
 * @looper:               bitmap of looping state
 *                        (only accessed by this thread)
 * @looper_needs_return:  looping thread needs to exit driver
 *                        (no lock needed)
 * @transaction_stack:    stack of in-progress transactions for this thread
 *                        (protected by @proc->inner_lock)
 * @todo:                 list of work to do for this thread
 *                        (protected by @proc->inner_lock)
 * @process_todo:         whether work in @todo should be processed
 *                        (protected by @proc->inner_lock)
 * @return_error:         transaction errors reported by this thread
 *                        (only accessed by this thread)
 * @reply_error:          transaction errors reported by target thread
 *                        (protected by @proc->inner_lock)
 * @wait:                 wait queue for thread work
 * @stats:                per-thread statistics
 *                        (atomics, no lock needed)
 * @tmp_ref:              temporary reference to indicate thread is in use
 *                        (atomic since @proc->inner_lock cannot
 *                        always be acquired)
 * @is_dead:              thread is dead and awaiting free
 *                        when outstanding transactions are cleaned up
 *                        (protected by @proc->inner_lock)
 * @task:                 struct task_struct for this thread
 *
 * Bookkeeping structure for binder threads.
 */
struct binder_thread {
	struct binder_proc *proc;
	struct rb_node rb_node;
	struct list_head waiting_thread_node;
	int pid;
	int looper;              /* only modified by this thread */
	bool looper_need_return; /* can be written by other thread */
	struct binder_transaction *transaction_stack;
	struct list_head todo;
	bool process_todo;
	struct binder_error return_error;
	struct binder_error reply_error;
	wait_queue_head_t wait;
	struct binder_stats stats;
	atomic_t tmp_ref;
	bool is_dead;
	struct task_struct *task;
};

struct binder_transaction {
	int debug_id;
	struct binder_work work;
	struct binder_thread *from;
	struct binder_transaction *from_parent;
	struct binder_proc *to_proc;
	struct binder_thread *to_thread;
	struct binder_transaction *to_parent;
	unsigned need_reply:1;
	/* unsigned is_dead:1; */	/* not used at the moment */

	struct binder_buffer *buffer;
	unsigned int	code;
	unsigned int	flags;
	struct binder_priority	priority;
	struct binder_priority	saved_priority;
	bool    set_priority_called;
	kuid_t	sender_euid;
	binder_uintptr_t security_ctx;
	/**
	 * @lock:  protects @from, @to_proc, and @to_thread
	 *
	 * @from, @to_proc, and @to_thread can be set to NULL
	 * during thread teardown
	 */
	spinlock_t lock;
#ifdef BINDER_WATCHDOG
	enum wait_on_reason wait_on;
	enum wait_on_reason bark_on;
	struct rb_node rb_node;         /* by bark_time */
	struct timespec bark_time;
	struct timespec exe_timestamp;
	char service[MAX_SERVICE_NAME_LEN];
	pid_t fproc;
	pid_t fthrd;
	pid_t tproc;
	pid_t tthrd;
	unsigned int log_idx;
#endif
#ifdef BINDER_USER_TRACKING
	struct timespec timestamp;
	struct timeval tv;
#endif
#ifdef CONFIG_MTK_TASK_TURBO
	struct task_struct *inherit_task;
#endif

};

/* ---- end moved structures ---- */

struct binder_transaction_log_entry {
	int debug_id;
	int debug_id_done;
	int call_type;
	int from_proc;
	int from_thread;
	int target_handle;
	int to_proc;
	int to_thread;
	int to_node;
	int data_size;
	int offsets_size;
	int return_error_line;
	uint32_t return_error;
	uint32_t return_error_param;
	const char *context_name;
#ifdef BINDER_WATCHDOG
	unsigned int code;
	char service[MAX_SERVICE_NAME_LEN];
	int fd;
	struct timespec readstamp;
	struct timespec endstamp;
	unsigned int cur;
#endif
#ifdef BINDER_USER_TRACKING
	struct timespec timestamp;
	struct timeval tv;
#endif
};
#ifdef BINDER_WATCHDOG
struct binder_timeout_log_entry {
	enum wait_on_reason r;
	pid_t from_proc;
	pid_t from_thrd;
	pid_t to_proc;
	pid_t to_thrd;
	unsigned int over_sec;
	struct timespec ts;
	struct timeval tv;
	unsigned int code;
	char service[MAX_SERVICE_NAME_LEN];
	int debug_id;
};
#endif

struct binder_transaction_log {
	atomic_t cur;
	bool full;
#ifdef BINDER_WATCHDOG
	unsigned int size;
	struct binder_transaction_log_entry *entry;
#else
	struct binder_transaction_log_entry entry[32];
#endif
};

extern struct binder_transaction_log binder_transaction_log;
extern struct binder_transaction_log binder_transaction_log_failed;
#endif /* _LINUX_BINDER_INTERNAL_H */
