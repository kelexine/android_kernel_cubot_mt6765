/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#ifndef _FS_FUSE_I_H
#define _FS_FUSE_I_H

#include <uapi/linux/android_fuse.h>
#include <linux/filter.h>
#include <linux/pagemap.h>
#include <linux/statfs.h>

#define FUSE_SUPER_MAGIC 0x65735546

#include <linux/fuse.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/poll.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/xattr.h>
#include <linux/pid_namespace.h>
#include <linux/refcount.h>
#include <linux/user_namespace.h>

/** Max number of pages that can be used in a single read request */
#define FUSE_MAX_PAGES_PER_REQ 32

/** Bias for fi->writectr, meaning new writepages must not be sent */
#define FUSE_NOWRITE INT_MIN

/** It could be as large as PATH_MAX, but would that have any uses? */
#define FUSE_NAME_MAX 1024

/** Number of dentries for each connection in the control filesystem */
#define FUSE_CTL_NUM_DENTRIES 5

/** Number of page pointers embedded in fuse_req */
#define FUSE_REQ_INLINE_PAGES 1

/** List of active connections */
extern struct list_head fuse_conn_list;

/** Global mutex protecting fuse_conn_list and the control filesystem */
extern struct mutex fuse_mutex;

/** Module parameters */
extern unsigned max_user_bgreq;
extern unsigned max_user_congthresh;

/* One forget request */
struct fuse_forget_link {
	struct fuse_forget_one forget_one;
	struct fuse_forget_link *next;
};

/** FUSE specific dentry data */
struct fuse_dentry {
	union {
		u64 time;
		struct rcu_head rcu;
	};
	struct path backing_path;
};

static inline struct fuse_dentry *get_fuse_dentry(const struct dentry *entry)
{
	return entry->d_fsdata;
}

static inline void fuse_dentry_settime(struct dentry *entry, u64 time)
{
	struct fuse_dentry *di = get_fuse_dentry(entry);

	if (di)
		di->time = time;
}

static inline u64 fuse_dentry_time(const struct dentry *entry)
{
	struct fuse_dentry *di = get_fuse_dentry(entry);

	return di ? di->time : 0;
}

#ifdef CONFIG_FUSE_BPF
static inline void get_fuse_backing_path(const struct dentry *d,
					  struct path *path)
{
	struct fuse_dentry *di = get_fuse_dentry(d);

	if (!di) {
		*path = (struct path) {};
		return;
	}

	*path = di->backing_path;
	path_get(path);
}
#endif

/** FUSE inode */
struct fuse_inode {
	/** Inode data */
	struct inode inode;

#ifdef CONFIG_FUSE_BPF
	/**
	 * Backing inode, if this inode is from a backing file system.
	 * If this is set, nodeid is 0.
	 */
	struct inode *backing_inode;

	/**
	 * bpf_prog, run on all operations to determine whether to pass through
	 * or handle in place
	 */
	struct bpf_prog *bpf;
#endif

	/** Unique ID, which identifies the inode between userspace
	 * and kernel */
	u64 nodeid;

	/** Number of lookups on this inode */
	u64 nlookup;

	/** The request used for sending the FORGET message */
	struct fuse_forget_link *forget;

	/** Time in jiffies until the file attributes are valid */
	u64 i_time;

	/** The sticky bit in inode->i_mode may have been removed, so
	    preserve the original mode */
	umode_t orig_i_mode;

	/** 64 bit inode number */
	u64 orig_ino;

	/** Version of last attribute change */
	u64 attr_version;

	/** Files usable in writepage.  Protected by fc->lock */
	struct list_head write_files;

	/** Writepages pending on truncate or fsync */
	struct list_head queued_writes;

	/** Number of sent writes, a negative bias (FUSE_NOWRITE)
	 * means more writes are blocked */
	int writectr;

	/** Waitq for writepage completion */
	wait_queue_head_t page_waitq;

	/** List of writepage requestst (pending or sent) */
	struct list_head writepages;

	/** Miscellaneous bits describing inode state */
	unsigned long state;

	/** Lock for serializing lookup and readdir for back compatibility*/
	struct mutex mutex;
};

/** FUSE inode state bits */
enum {
	/** Advise readdirplus  */
	FUSE_I_ADVISE_RDPLUS,
	/** Initialized with readdirplus */
	FUSE_I_INIT_RDPLUS,
	/** An operation changing file size is in progress  */
	FUSE_I_SIZE_UNSTABLE,
	/* Bad inode */
	FUSE_I_BAD,
};

struct fuse_conn;

/**
 * Reference to lower filesystem file for read/write operations handled in
 * passthrough mode
 */
struct fuse_passthrough {
	struct file *filp;
	struct cred *cred;
};

/** FUSE specific file data */
struct fuse_file {
	/** Fuse connection for this file */
	struct fuse_conn *fc;

	/** Request reserved for flush and release */
	struct fuse_req *reserved_req;

	/** Kernel file handle guaranteed to be unique */
	u64 kh;

	/** File handle used by userspace */
	u64 fh;

	/** Node id of this file */
	u64 nodeid;

	/** Refcount */
	refcount_t count;

	/** FOPEN_* flags returned by open */
	u32 open_flags;

	/** Entry on inode's write_files list */
	struct list_head write_entry;

	/** Container for data related to the passthrough functionality */
	struct fuse_passthrough passthrough;

#ifdef CONFIG_FUSE_BPF
	/**
	 * TODO: Reconcile with passthrough file
	 * backing file when in bpf mode
	 */
	struct file *backing_file;
#endif

	/** RB node to be linked on fuse_conn->polled_files */
	struct rb_node polled_node;

	/** Wait queue head for poll */
	wait_queue_head_t poll_wait;

	/** Has flock been performed on this file? */
	bool flock:1;
};

/** One input argument of a request */
struct fuse_in_arg {
	unsigned size;
	const void *value;
};

/** The request input */
struct fuse_in {
	/** The request header */
	struct fuse_in_header h;

	/** True if the data for the last argument is in req->pages */
	unsigned argpages:1;

	/** Number of arguments */
	unsigned numargs;

	/** Array of arguments */
	struct fuse_in_arg args[FUSE_MAX_IN_ARGS];
};

/** One output argument of a request */
struct fuse_arg {
	unsigned size;
	void *value;
};

/** The request output */
struct fuse_out {
	/** Header returned from userspace */
	struct fuse_out_header h;

	/*
	 * The following bitfields are not changed during the request
	 * processing
	 */

	/** Last argument is variable length (can be shorter than
	    arg->size) */
	unsigned argvar:1;

	/** Last argument is a list of pages to copy data to */
	unsigned argpages:1;

	/** Zero partially or not copied pages */
	unsigned page_zeroing:1;

	/** Pages may be replaced with new ones */
	unsigned page_replace:1;

	/** Number or arguments */
	unsigned numargs;

	/** Array of arguments */
	struct fuse_arg args[FUSE_MAX_OUT_ARGS];

	/* Path used for completing d_canonical_path */
	struct path *canonical_path;
};

/** FUSE page descriptor */
struct fuse_page_desc {
	unsigned int length;
	unsigned int offset;
};

struct fuse_args {
	struct {
		struct {
			uint32_t opcode;
			uint64_t nodeid;
		} h;
		unsigned numargs;
		struct fuse_in_arg args[FUSE_MAX_IN_ARGS];

	} in;
	struct {
		unsigned argvar:1;
		unsigned numargs;
		struct fuse_arg args[FUSE_MAX_OUT_ARGS];

		/* Path used for completing d_canonical_path */
		struct path *canonical_path;
	} out;
};

#define FUSE_ARGS(args) struct fuse_args args = {}

/** The request IO state (for asynchronous processing) */
struct fuse_io_priv {
	struct kref refcnt;
	int async;
	spinlock_t lock;
	unsigned reqs;
	ssize_t bytes;
	size_t size;
	__u64 offset;
	bool write;
	bool should_dirty;
	int err;
	struct kiocb *iocb;
	struct completion *done;
	bool blocking;
};

#define FUSE_IO_PRIV_SYNC(i) \
{					\
	.refcnt = KREF_INIT(1),		\
	.async = 0,			\
	.iocb = i,			\
}

/**
 * Request flags
 *
 * FR_ISREPLY:		set if the request has reply
 * FR_FORCE:		force sending of the request even if interrupted
 * FR_BACKGROUND:	request is sent in the background
 * FR_WAITING:		request is counted as "waiting"
 * FR_ABORTED:		the request was aborted
 * FR_INTERRUPTED:	the request has been interrupted
 * FR_LOCKED:		data is being copied to/from the request
 * FR_PENDING:		request is not yet in userspace
 * FR_SENT:		request is in userspace, waiting for an answer
 * FR_FINISHED:		request is finished
 * FR_PRIVATE:		request is on private list
 */
enum fuse_req_flag {
	FR_ISREPLY,
	FR_FORCE,
	FR_BACKGROUND,
	FR_WAITING,
	FR_ABORTED,
	FR_INTERRUPTED,
	FR_LOCKED,
	FR_PENDING,
	FR_SENT,
	FR_FINISHED,
	FR_PRIVATE,
};

/**
 * A request to the client
 *
 * .waitq.lock protects the following fields:
 *   - FR_ABORTED
 *   - FR_LOCKED (may also be modified under fc->lock, tested under both)
 */
struct fuse_req {
	/** This can be on either pending processing or io lists in
	    fuse_conn */
	struct list_head list;

	/** Entry on the interrupts list  */
	struct list_head intr_entry;

	/** refcount */
	refcount_t count;

	bool user_pages;

	/** Unique ID for the interrupt request */
	u64 intr_unique;

	/* Request flags, updated with test/set/clear_bit() */
	unsigned long flags;

	/** The request input */
	struct fuse_in in;

	/** The request output */
	struct fuse_out out;

	/** Used to wake up the task waiting for completion of request*/
	wait_queue_head_t waitq;

	/** Data for asynchronous requests */
	union {
		struct {
			struct fuse_release_in in;
			struct inode *inode;
		} release;
		struct fuse_init_in init_in;
		struct fuse_init_out init_out;
		struct cuse_init_in cuse_init_in;
		struct {
			struct fuse_read_in in;
			u64 attr_ver;
		} read;
		struct {
			struct fuse_write_in in;
			struct fuse_write_out out;
			struct fuse_req *next;
		} write;
		struct fuse_notify_retrieve_in retrieve_in;
	} misc;

	/** page vector */
	struct page **pages;

	/** page-descriptor vector */
	struct fuse_page_desc *page_descs;

	/** size of the 'pages' array */
	unsigned max_pages;

	/** inline page vector */
	struct page *inline_pages[FUSE_REQ_INLINE_PAGES];

	/** inline page-descriptor vector */
	struct fuse_page_desc inline_page_descs[FUSE_REQ_INLINE_PAGES];

	/** number of pages in vector */
	unsigned num_pages;

	/** File used in the request (or NULL) */
	struct fuse_file *ff;

	/** Inode used in the request or NULL */
	struct inode *inode;

	/** AIO control block */
	struct fuse_io_priv *io;

	/** Link on fi->writepages */
	struct list_head writepages_entry;

	/** Request completion callback */
	void (*end)(struct fuse_conn *, struct fuse_req *);

	/** Request is stolen from fuse_file->reserved_req */
	struct file *stolen_file;
};

struct fuse_iqueue {
	/** Connection established */
	unsigned connected;

	/** Lock protecting accesses to members of this structure */
	spinlock_t lock;

	/** Readers of the connection are waiting on this */
	wait_queue_head_t waitq;

	/** The next unique request id */
	u64 reqctr;

	/** The list of pending requests */
	struct list_head pending;

	/** Pending interrupts */
	struct list_head interrupts;

	/** Queue of pending forgets */
	struct fuse_forget_link forget_list_head;
	struct fuse_forget_link *forget_list_tail;

	/** Batching of FORGET requests (positive indicates FORGET batch) */
	int forget_batch;

	/** O_ASYNC requests */
	struct fasync_struct *fasync;
};

struct fuse_pqueue {
	/** Connection established */
	unsigned connected;

	/** Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/** The list of requests being processed */
	struct list_head processing;

	/** The list of requests under I/O */
	struct list_head io;
};

/**
 * Fuse device instance
 */
struct fuse_dev {
	/** Fuse connection for this device */
	struct fuse_conn *fc;

	/** Processing queue */
	struct fuse_pqueue pq;

	/** list entry on fc->devices */
	struct list_head entry;
};

/**
 * A Fuse connection.
 *
 * This structure is created, when the filesystem is mounted, and is
 * destroyed, when the client device is closed and the filesystem is
 * unmounted.
 */
struct fuse_conn {
	/** Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/** Refcount */
	refcount_t count;

	/** Number of fuse_dev's */
	atomic_t dev_count;

	struct rcu_head rcu;

	/** The user id for this mount */
	kuid_t user_id;

	/** The group id for this mount */
	kgid_t group_id;

	/** The pid namespace for this mount */
	struct pid_namespace *pid_ns;

	/** The user namespace for this mount */
	struct user_namespace *user_ns;

	/** Maximum read size */
	unsigned max_read;

	/** Maximum write size */
	unsigned max_write;

	/** Input queue */
	struct fuse_iqueue iq;

	/** The next unique kernel file handle */
	u64 khctr;

	/** rbtree of fuse_files waiting for poll events indexed by ph */
	struct rb_root polled_files;

	/** Maximum number of outstanding background requests */
	unsigned max_background;

	/** Number of background requests at which congestion starts */
	unsigned congestion_threshold;

	/** Number of requests currently in the background */
	unsigned num_background;

	/** Number of background requests currently queued for userspace */
	unsigned active_background;

	/** The list of background requests set aside for later queuing */
	struct list_head bg_queue;

	/** Flag indicating that INIT reply has been received. Allocating
	 * any fuse request will be suspended until the flag is set */
	int initialized;

	/** Flag indicating if connection is blocked.  This will be
	    the case before the INIT reply is received, and if there
	    are too many outstading backgrounds requests */
	int blocked;

	/** waitq for blocked connection */
	wait_queue_head_t blocked_waitq;

	/** waitq for reserved requests */
	wait_queue_head_t reserved_req_waitq;

	/** Connection established, cleared on umount, connection
	    abort and device release */
	unsigned connected;

	/** Connection aborted via sysfs */
	bool aborted;

	/** Connection failed (version mismatch).  Cannot race with
	    setting other bitfields since it is only set once in INIT
	    reply, before any other request, and never cleared */
	unsigned conn_error:1;

	/** Connection successful.  Only set in INIT */
	unsigned conn_init:1;

	/** Do readpages asynchronously?  Only set in INIT */
	unsigned async_read:1;

	/** Return an unique read error after abort.  Only set in INIT */
	unsigned abort_err:1;

	/** Do not send separate SETATTR request before open(O_TRUNC)  */
	unsigned atomic_o_trunc:1;

	/** Filesystem supports NFS exporting.  Only set in INIT */
	unsigned export_support:1;

	/** write-back cache policy (default is write-through) */
	unsigned writeback_cache:1;

	/** allow parallel lookups and readdir (default is serialized) */
	unsigned parallel_dirops:1;

	/** handle fs handles killing suid/sgid/cap on write/chown/trunc */
	unsigned handle_killpriv:1;

	/** Passthrough mode for read/write IO */
	unsigned int passthrough:1;

	/** BPF Only, no Daemon running */
	unsigned int no_daemon:1;
	/*
	 * The following bitfields are only for optimization purposes
	 * and hence races in setting them will not cause malfunction
	 */

	/** Is open/release not implemented by fs? */
	unsigned no_open:1;

	/** Is fsync not implemented by fs? */
	unsigned no_fsync:1;

	/** Is fsyncdir not implemented by fs? */
	unsigned no_fsyncdir:1;

	/** Is flush not implemented by fs? */
	unsigned no_flush:1;

	/** Is setxattr not implemented by fs? */
	unsigned no_setxattr:1;

	/** Is getxattr not implemented by fs? */
	unsigned no_getxattr:1;

	/** Is listxattr not implemented by fs? */
	unsigned no_listxattr:1;

	/** Is removexattr not implemented by fs? */
	unsigned no_removexattr:1;

	/** Are posix file locking primitives not implemented by fs? */
	unsigned no_lock:1;

	/** Is access not implemented by fs? */
	unsigned no_access:1;

	/** Is create not implemented by fs? */
	unsigned no_create:1;

	/** Is interrupt not implemented by fs? */
	unsigned no_interrupt:1;

	/** Is bmap not implemented by fs? */
	unsigned no_bmap:1;

	/** Is poll not implemented by fs? */
	unsigned no_poll:1;

	/** Do multi-page cached writes */
	unsigned big_writes:1;

	/** Don't apply umask to creation modes */
	unsigned dont_mask:1;

	/** Are BSD file locking primitives not implemented by fs? */
	unsigned no_flock:1;

	/** Is fallocate not implemented by fs? */
	unsigned no_fallocate:1;

	/** Is rename with flags implemented by fs? */
	unsigned no_rename2:1;

	/** Use enhanced/automatic page cache invalidation. */
	unsigned auto_inval_data:1;

	/** Does the filesystem support readdirplus? */
	unsigned do_readdirplus:1;

	/** Does the filesystem want adaptive readdirplus? */
	unsigned readdirplus_auto:1;

	/** Does the filesystem support asynchronous direct-IO submission? */
	unsigned async_dio:1;

	/** Is lseek not implemented by fs? */
	unsigned no_lseek:1;

	/** Does the filesystem support posix acls? */
	unsigned posix_acl:1;

	/** Check permissions based on the file mode or not? */
	unsigned default_permissions:1;

	/** Allow other than the mounter user to access the filesystem ? */
	unsigned allow_other:1;

	/** The number of requests waiting for completion */
	atomic_t num_waiting;

	/** Negotiated minor version */
	unsigned minor;

	/** Entry on the fuse_conn_list */
	struct list_head entry;

	/** Device ID from super block */
	dev_t dev;

	/** Dentries in the control filesystem */
	struct dentry *ctl_dentry[FUSE_CTL_NUM_DENTRIES];

	/** number of dentries used in the above array */
	int ctl_ndents;

	/** Key for lock owner ID scrambling */
	u32 scramble_key[4];

	/** Reserved request for the DESTROY message */
	struct fuse_req *destroy_req;

	/** Version counter for attribute changes */
	u64 attr_version;

	/** Called on final put */
	void (*release)(struct fuse_conn *);

	/** Super block for this connection. */
	struct super_block *sb;

	/** Read/write semaphore to hold when accessing sb. */
	struct rw_semaphore killsb;

	/** List of device instances belonging to this connection */
	struct list_head devices;

	/** IDR for passthrough requests */
	struct idr passthrough_req;

	/** Protects passthrough_req */
	spinlock_t passthrough_req_lock;
};

static inline struct fuse_conn *get_fuse_conn_super(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct fuse_conn *get_fuse_conn(struct inode *inode)
{
	return get_fuse_conn_super(inode->i_sb);
}

static inline struct fuse_inode *get_fuse_inode(struct inode *inode)
{
	return container_of(inode, struct fuse_inode, inode);
}

static inline u64 get_node_id(struct inode *inode)
{
	return get_fuse_inode(inode)->nodeid;
}

static inline void fuse_make_bad(struct inode *inode)
{
	set_bit(FUSE_I_BAD, &get_fuse_inode(inode)->state);
}

static inline bool fuse_is_bad(struct inode *inode)
{
	return unlikely(test_bit(FUSE_I_BAD, &get_fuse_inode(inode)->state));
}

/** Device operations */
extern const struct file_operations fuse_dev_operations;

extern const struct dentry_operations fuse_dentry_operations;
extern const struct dentry_operations fuse_root_dentry_operations;

/**
 * Inode to nodeid comparison.
 */
int fuse_inode_eq(struct inode *inode, void *_nodeidp);

/**
 * Get a filled in inode
 */
struct inode *fuse_iget_backing(struct super_block *sb,
				u64 nodeid,
				struct inode *backing_inode);
void fuse_init_dentry_root(struct dentry *root, struct file *backing_dir);
struct inode *fuse_iget(struct super_block *sb, u64 nodeid,
			int generation, struct fuse_attr *attr,
			u64 attr_valid, u64 attr_version);

int fuse_lookup_name(struct super_block *sb, u64 nodeid, const struct qstr *name,
		     struct fuse_entry_out *outarg,
		     struct dentry *entry,
		     struct inode **inode);

/**
 * Send FORGET command
 */
void fuse_queue_forget(struct fuse_conn *fc, struct fuse_forget_link *forget,
		       u64 nodeid, u64 nlookup);

struct fuse_forget_link *fuse_alloc_forget(void);

/* Used by READDIRPLUS */
void fuse_force_forget(struct file *file, u64 nodeid);

/**
 * Initialize READ or READDIR request
 */
void fuse_read_fill(struct fuse_req *req, struct file *file,
		    loff_t pos, size_t count, int opcode);

/**
 * Send OPEN or OPENDIR request
 */
int fuse_open_common(struct inode *inode, struct file *file, bool isdir);

struct fuse_file *fuse_file_alloc(struct fuse_conn *fc);
void fuse_file_free(struct fuse_file *ff);
void fuse_finish_open(struct inode *inode, struct file *file);

void fuse_sync_release(struct fuse_file *ff, int flags);

/**
 * Send RELEASE or RELEASEDIR request
 */
void fuse_release_common(struct file *file, bool isdir);

/**
 * Send FSYNC or FSYNCDIR request
 */
int fuse_fsync_common(struct file *file, loff_t start, loff_t end,
		      int datasync, int isdir);

/**
 * Notify poll wakeup
 */
int fuse_notify_poll_wakeup(struct fuse_conn *fc,
			    struct fuse_notify_poll_wakeup_out *outarg);

/**
 * Initialize file operations on a regular file
 */
void fuse_init_file_inode(struct inode *inode);

/**
 * Initialize inode operations on regular files and special files
 */
void fuse_init_common(struct inode *inode);

/**
 * Initialize inode and file operations on a directory
 */
void fuse_init_dir(struct inode *inode);

/**
 * Initialize inode operations on a symlink
 */
void fuse_init_symlink(struct inode *inode);

/**
 * Change attributes of an inode
 */
void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
			    u64 attr_valid, u64 attr_version);

void fuse_change_attributes_common(struct inode *inode, struct fuse_attr *attr,
				   u64 attr_valid);

/**
 * Initialize the client device
 */
int fuse_dev_init(void);

/**
 * Cleanup the client device
 */
void fuse_dev_cleanup(void);

int fuse_ctl_init(void);
void __exit fuse_ctl_cleanup(void);

/**
 * Allocate a request
 */
struct fuse_req *fuse_request_alloc(unsigned npages);

struct fuse_req *fuse_request_alloc_nofs(unsigned npages);

/**
 * Free a request
 */
void fuse_request_free(struct fuse_req *req);

/**
 * Get a request, may fail with -ENOMEM,
 * caller should specify # elements in req->pages[] explicitly
 */
struct fuse_req *fuse_get_req(struct fuse_conn *fc, unsigned npages);
struct fuse_req *fuse_get_req_for_background(struct fuse_conn *fc,
					     unsigned npages);

/*
 * Increment reference count on request
 */
void __fuse_get_request(struct fuse_req *req);

/**
 * Gets a requests for a file operation, always succeeds
 */
struct fuse_req *fuse_get_req_nofail_nopages(struct fuse_conn *fc,
					     struct file *file);

/**
 * Decrement reference count of a request.  If count goes to zero free
 * the request.
 */
void fuse_put_request(struct fuse_conn *fc, struct fuse_req *req);

/**
 * Send a request (synchronous)
 */
void fuse_request_send(struct fuse_conn *fc, struct fuse_req *req);

/**
 * Simple request sending that does request allocation and freeing
 */
ssize_t fuse_simple_request(struct fuse_conn *fc, struct fuse_args *args);

/**
 * Send a request in the background
 */
void fuse_request_send_background(struct fuse_conn *fc, struct fuse_req *req);

void fuse_request_send_background_locked(struct fuse_conn *fc,
					 struct fuse_req *req);

/* Abort all requests */
void fuse_abort_conn(struct fuse_conn *fc, bool is_abort);
void fuse_wait_aborted(struct fuse_conn *fc);

/**
 * Invalidate inode attributes
 */
void fuse_invalidate_attr(struct inode *inode);

void fuse_invalidate_entry_cache(struct dentry *entry);

void fuse_invalidate_atime(struct inode *inode);

/**
 * Acquire reference to fuse_conn
 */
struct fuse_conn *fuse_conn_get(struct fuse_conn *fc);

/**
 * Initialize fuse_conn
 */
void fuse_conn_init(struct fuse_conn *fc, struct user_namespace *user_ns);

/**
 * Release reference to fuse_conn
 */
void fuse_conn_put(struct fuse_conn *fc);

struct fuse_dev *fuse_dev_alloc(struct fuse_conn *fc);
void fuse_dev_free(struct fuse_dev *fud);

/**
 * Add connection to control filesystem
 */
int fuse_ctl_add_conn(struct fuse_conn *fc);

/**
 * Remove connection from control filesystem
 */
void fuse_ctl_remove_conn(struct fuse_conn *fc);

/**
 * Is file type valid?
 */
int fuse_valid_type(int m);

bool fuse_invalid_attr(struct fuse_attr *attr);

/**
 * Is current process allowed to perform filesystem operation?
 */
int fuse_allow_current_process(struct fuse_conn *fc);

u64 fuse_lock_owner_id(struct fuse_conn *fc, fl_owner_t id);

void fuse_update_ctime(struct inode *inode);

int fuse_update_attributes(struct inode *inode, struct file *file);

void fuse_flush_writepages(struct inode *inode);

void fuse_set_nowrite(struct inode *inode);
void fuse_release_nowrite(struct inode *inode);

u64 fuse_get_attr_version(struct fuse_conn *fc);

/**
 * File-system tells the kernel to invalidate cache for the given node id.
 */
int fuse_reverse_inval_inode(struct super_block *sb, u64 nodeid,
			     loff_t offset, loff_t len);

/**
 * File-system tells the kernel to invalidate parent attributes and
 * the dentry matching parent/name.
 *
 * If the child_nodeid is non-zero and:
 *    - matches the inode number for the dentry matching parent/name,
 *    - is not a mount point
 *    - is a file or oan empty directory
 * then the dentry is unhashed (d_delete()).
 */
int fuse_reverse_inval_entry(struct super_block *sb, u64 parent_nodeid,
			     u64 child_nodeid, struct qstr *name);

int fuse_do_open(struct fuse_conn *fc, u64 nodeid, struct file *file,
		 bool isdir);

/**
 * fuse_direct_io() flags
 */

/** If set, it is WRITE; otherwise - READ */
#define FUSE_DIO_WRITE (1 << 0)

/** CUSE pass fuse_direct_io() a file which f_mapping->host is not from FUSE */
#define FUSE_DIO_CUSE  (1 << 1)

ssize_t fuse_direct_io(struct fuse_io_priv *io, struct iov_iter *iter,
		       loff_t *ppos, int flags);
long fuse_do_ioctl(struct file *file, unsigned int cmd, unsigned long arg,
		   unsigned int flags);
long fuse_ioctl_common(struct file *file, unsigned int cmd,
		       unsigned long arg, unsigned int flags);
__poll_t fuse_file_poll(struct file *file, poll_table *wait);
int fuse_dev_release(struct inode *inode, struct file *file);

bool fuse_write_update_size(struct inode *inode, loff_t pos);

int fuse_flush_times(struct inode *inode, struct fuse_file *ff);
int fuse_write_inode(struct inode *inode, struct writeback_control *wbc);

int fuse_do_setattr(struct dentry *dentry, struct iattr *attr,
		    struct file *file);

void fuse_set_initialized(struct fuse_conn *fc);

void fuse_unlock_inode(struct inode *inode, bool locked);
bool fuse_lock_inode(struct inode *inode);

int fuse_setxattr(struct inode *inode, const char *name, const void *value,
		  size_t size, int flags);
ssize_t fuse_getxattr(struct inode *inode, const char *name, void *value,
		      size_t size);
ssize_t fuse_listxattr(struct dentry *entry, char *list, size_t size);
int fuse_removexattr(struct inode *inode, const char *name);
extern const struct xattr_handler *fuse_xattr_handlers[];
extern const struct xattr_handler *fuse_acl_xattr_handlers[];
extern const struct xattr_handler *fuse_no_acl_xattr_handlers[];

struct posix_acl;
struct posix_acl *fuse_get_acl(struct inode *inode, int type);
int fuse_set_acl(struct inode *inode, struct posix_acl *acl, int type);

#ifdef CONFIG_OPLUS_FEATURE_ACM
void acm_fuse_init_cache(void);
void acm_fuse_free_cache(void);
#endif

/* passthrough.c */
int fuse_passthrough_open(struct fuse_dev *fud, u32 lower_fd);
int fuse_passthrough_setup(struct fuse_conn *fc, struct fuse_file *ff,
			   struct fuse_open_out *openarg);
void fuse_passthrough_release(struct fuse_passthrough *passthrough);
ssize_t fuse_passthrough_read_iter(struct kiocb *iocb, struct iov_iter *to);
ssize_t fuse_passthrough_write_iter(struct kiocb *iocb, struct iov_iter *from);
ssize_t fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma);

/* backing.c */

/*
 * Dummy io passed to fuse_bpf_backing when io operation needs no scratch space
 */
struct fuse_dummy_io {
	int unused;
};

struct fuse_open_io {
	struct fuse_open_in foi;
	struct fuse_open_out foo;
};

int fuse_open_initialize(struct fuse_bpf_args *fa, struct fuse_open_io *foi,
			 struct inode *inode, struct file *file, bool isdir);
int fuse_open_backing(struct fuse_bpf_args *fa,
		      struct inode *inode, struct file *file, bool isdir);
void *fuse_open_finalize(struct fuse_bpf_args *fa,
		       struct inode *inode, struct file *file, bool isdir);

struct fuse_create_open_io {
	struct fuse_create_in fci;
	struct fuse_entry_out feo;
	struct fuse_open_out foo;
};

int fuse_create_open_initialize(
		struct fuse_bpf_args *fa, struct fuse_create_open_io *fcoi,
		struct inode *dir, struct dentry *entry,
		struct file *file, unsigned int flags, umode_t mode);
int fuse_create_open_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry,
		struct file *file, unsigned int flags, umode_t mode);
void *fuse_create_open_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry,
		struct file *file, unsigned int flags, umode_t mode);

int fuse_mknod_initialize(
		struct fuse_bpf_args *fa, struct fuse_mknod_in *fmi,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev);
int fuse_mknod_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev);
void *fuse_mknod_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev);

int fuse_mkdir_initialize(
		struct fuse_bpf_args *fa, struct fuse_mkdir_in *fmi,
		struct inode *dir, struct dentry *entry, umode_t mode);
int fuse_mkdir_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode);
void *fuse_mkdir_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode);

int fuse_rmdir_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *fmi,
		struct inode *dir, struct dentry *entry);
int fuse_rmdir_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry);
void *fuse_rmdir_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry);

int fuse_rename2_initialize(struct fuse_bpf_args *fa, struct fuse_rename2_in *fri,
			    struct inode *olddir, struct dentry *oldent,
			    struct inode *newdir, struct dentry *newent,
			    unsigned int flags);
int fuse_rename2_backing(struct fuse_bpf_args *fa,
			 struct inode *olddir, struct dentry *oldent,
			 struct inode *newdir, struct dentry *newent,
			 unsigned int flags);
void *fuse_rename2_finalize(struct fuse_bpf_args *fa,
			    struct inode *olddir, struct dentry *oldent,
			    struct inode *newdir, struct dentry *newent,
			    unsigned int flags);

int fuse_rename_initialize(struct fuse_bpf_args *fa, struct fuse_rename_in *fri,
			   struct inode *olddir, struct dentry *oldent,
			   struct inode *newdir, struct dentry *newent);
int fuse_rename_backing(struct fuse_bpf_args *fa,
			struct inode *olddir, struct dentry *oldent,
			struct inode *newdir, struct dentry *newent);
void *fuse_rename_finalize(struct fuse_bpf_args *fa,
			   struct inode *olddir, struct dentry *oldent,
			   struct inode *newdir, struct dentry *newent);

int fuse_unlink_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *fmi,
		struct inode *dir, struct dentry *entry);
int fuse_unlink_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry);
void *fuse_unlink_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry);

int fuse_link_initialize(struct fuse_bpf_args *fa, struct fuse_link_in *fli,
			  struct dentry *entry, struct inode *dir,
			  struct dentry *newent);
int fuse_link_backing(struct fuse_bpf_args *fa, struct dentry *entry,
		      struct inode *dir, struct dentry *newent);
void *fuse_link_finalize(struct fuse_bpf_args *fa, struct dentry *entry,
			 struct inode *dir, struct dentry *newent);

int fuse_release_initialize(struct fuse_bpf_args *fa, struct fuse_release_in *fri,
			    struct inode *inode, struct file *file);
int fuse_releasedir_initialize(struct fuse_bpf_args *fa,
			struct fuse_release_in *fri,
			struct inode *inode, struct file *file);
int fuse_release_backing(struct fuse_bpf_args *fa,
			 struct inode *inode, struct file *file);
void *fuse_release_finalize(struct fuse_bpf_args *fa,
			    struct inode *inode, struct file *file);

int fuse_flush_initialize(struct fuse_bpf_args *fa, struct fuse_flush_in *ffi,
			  struct file *file, fl_owner_t id);
int fuse_flush_backing(struct fuse_bpf_args *fa, struct file *file, fl_owner_t id);
void *fuse_flush_finalize(struct fuse_bpf_args *fa,
			  struct file *file, fl_owner_t id);

struct fuse_lseek_io {
	struct fuse_lseek_in fli;
	struct fuse_lseek_out flo;
};

int fuse_lseek_initialize(struct fuse_bpf_args *fa, struct fuse_lseek_io *fli,
			  struct file *file, loff_t offset, int whence);
int fuse_lseek_backing(struct fuse_bpf_args *fa, struct file *file, loff_t offset, int whence);
void *fuse_lseek_finalize(struct fuse_bpf_args *fa, struct file *file, loff_t offset, int whence);

struct fuse_copy_file_range_io {
	struct fuse_copy_file_range_in fci;
	struct fuse_write_out fwo;
};

int fuse_copy_file_range_initialize(struct fuse_bpf_args *fa,
				   struct fuse_copy_file_range_io *fcf,
				   struct file *file_in, loff_t pos_in,
				   struct file *file_out, loff_t pos_out,
				   size_t len, unsigned int flags);
int fuse_copy_file_range_backing(struct fuse_bpf_args *fa,
				 struct file *file_in, loff_t pos_in,
				 struct file *file_out, loff_t pos_out,
				 size_t len, unsigned int flags);
void *fuse_copy_file_range_finalize(struct fuse_bpf_args *fa,
				    struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out,
				    size_t len, unsigned int flags);

int fuse_fsync_initialize(struct fuse_bpf_args *fa, struct fuse_fsync_in *ffi,
		   struct file *file, loff_t start, loff_t end, int datasync);
int fuse_fsync_backing(struct fuse_bpf_args *fa,
		   struct file *file, loff_t start, loff_t end, int datasync);
void *fuse_fsync_finalize(struct fuse_bpf_args *fa,
		   struct file *file, loff_t start, loff_t end, int datasync);
int fuse_dir_fsync_initialize(struct fuse_bpf_args *fa, struct fuse_fsync_in *ffi,
		   struct file *file, loff_t start, loff_t end, int datasync);

struct fuse_getxattr_io {
	struct fuse_getxattr_in fgi;
	struct fuse_getxattr_out fgo;
};

int fuse_getxattr_initialize(
		struct fuse_bpf_args *fa, struct fuse_getxattr_io *fgio,
		struct dentry *dentry, const char *name, void *value,
		size_t size);
int fuse_getxattr_backing(
		struct fuse_bpf_args *fa,
		struct dentry *dentry, const char *name, void *value,
		size_t size);
void *fuse_getxattr_finalize(
		struct fuse_bpf_args *fa,
		struct dentry *dentry, const char *name, void *value,
		size_t size);

int fuse_listxattr_initialize(struct fuse_bpf_args *fa,
			       struct fuse_getxattr_io *fgio,
			       struct dentry *dentry, char *list, size_t size);
int fuse_listxattr_backing(struct fuse_bpf_args *fa, struct dentry *dentry,
			   char *list, size_t size);
void *fuse_listxattr_finalize(struct fuse_bpf_args *fa, struct dentry *dentry,
			      char *list, size_t size);

int fuse_setxattr_initialize(struct fuse_bpf_args *fa,
			     struct fuse_setxattr_in *fsxi,
			     struct dentry *dentry, const char *name,
			     const void *value, size_t size, int flags);
int fuse_setxattr_backing(struct fuse_bpf_args *fa, struct dentry *dentry,
			  const char *name, const void *value, size_t size,
			  int flags);
void *fuse_setxattr_finalize(struct fuse_bpf_args *fa, struct dentry *dentry,
			     const char *name, const void *value, size_t size,
			     int flags);

int fuse_removexattr_initialize(struct fuse_bpf_args *fa,
				struct fuse_dummy_io *unused,
				struct dentry *dentry, const char *name);
int fuse_removexattr_backing(struct fuse_bpf_args *fa,
			     struct dentry *dentry, const char *name);
void *fuse_removexattr_finalize(struct fuse_bpf_args *fa,
				struct dentry *dentry, const char *name);

struct fuse_read_iter_out {
	uint64_t ret;
};
struct fuse_file_read_iter_io {
	struct fuse_read_in fri;
	struct fuse_read_iter_out frio;
};

int fuse_file_read_iter_initialize(
		struct fuse_bpf_args *fa, struct fuse_file_read_iter_io *fri,
		struct kiocb *iocb, struct iov_iter *to);
int fuse_file_read_iter_backing(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *to);
void *fuse_file_read_iter_finalize(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *to);

struct fuse_write_iter_out {
	uint64_t ret;
};
struct fuse_file_write_iter_io {
	struct fuse_write_in fwi;
	struct fuse_write_out fwo;
	struct fuse_write_iter_out fwio;
};

int fuse_file_write_iter_initialize(
		struct fuse_bpf_args *fa, struct fuse_file_write_iter_io *fwio,
		struct kiocb *iocb, struct iov_iter *from);
int fuse_file_write_iter_backing(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *from);
void *fuse_file_write_iter_finalize(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *from);

ssize_t fuse_backing_mmap(struct file *file, struct vm_area_struct *vma);

int fuse_file_fallocate_initialize(struct fuse_bpf_args *fa,
		struct fuse_fallocate_in *ffi,
		struct file *file, int mode, loff_t offset, loff_t length);
int fuse_file_fallocate_backing(struct fuse_bpf_args *fa,
		struct file *file, int mode, loff_t offset, loff_t length);
void *fuse_file_fallocate_finalize(struct fuse_bpf_args *fa,
		struct file *file, int mode, loff_t offset, loff_t length);

struct fuse_lookup_io {
	struct fuse_entry_out feo;
	struct fuse_entry_bpf feb;
};

int fuse_handle_backing(struct fuse_entry_bpf *feb, struct inode **backing_inode,
			struct path *backing_path);
int fuse_handle_bpf_prog(struct fuse_entry_bpf *feb, struct inode *parent,
			 struct bpf_prog **bpf);

int fuse_lookup_initialize(struct fuse_bpf_args *fa, struct fuse_lookup_io *feo,
	       struct inode *dir, struct dentry *entry, unsigned int flags);
int fuse_lookup_backing(struct fuse_bpf_args *fa, struct inode *dir,
			  struct dentry *entry, unsigned int flags);
struct dentry *fuse_lookup_finalize(struct fuse_bpf_args *fa, struct inode *dir,
			   struct dentry *entry, unsigned int flags);
int fuse_revalidate_backing(struct dentry *entry, unsigned int flags);

int fuse_canonical_path_initialize(struct fuse_bpf_args *fa,
				   struct fuse_dummy_io *fdi,
				   const struct path *path,
				   struct path *canonical_path);
int fuse_canonical_path_backing(struct fuse_bpf_args *fa, const struct path *path,
				struct path *canonical_path);
void *fuse_canonical_path_finalize(struct fuse_bpf_args *fa,
				   const struct path *path,
				   struct path *canonical_path);

struct fuse_getattr_io {
	struct fuse_getattr_in fgi;
	struct fuse_attr_out fao;
};
int fuse_getattr_initialize(struct fuse_bpf_args *fa, struct fuse_getattr_io *fgio,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags);
int fuse_getattr_backing(struct fuse_bpf_args *fa,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags);
void *fuse_getattr_finalize(struct fuse_bpf_args *fa,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags);

struct fuse_setattr_io {
	struct fuse_setattr_in fsi;
	struct fuse_attr_out fao;
};

int fuse_setattr_initialize(struct fuse_bpf_args *fa, struct fuse_setattr_io *fsi,
		struct dentry *dentry, struct iattr *attr, struct file *file);
int fuse_setattr_backing(struct fuse_bpf_args *fa,
		struct dentry *dentry, struct iattr *attr, struct file *file);
void *fuse_setattr_finalize(struct fuse_bpf_args *fa,
		struct dentry *dentry, struct iattr *attr, struct file *file);

int fuse_statfs_initialize(struct fuse_bpf_args *fa, struct fuse_statfs_out *fso,
		struct dentry *dentry, struct kstatfs *buf);
int fuse_statfs_backing(struct fuse_bpf_args *fa,
		struct dentry *dentry, struct kstatfs *buf);
void *fuse_statfs_finalize(struct fuse_bpf_args *fa,
		struct dentry *dentry, struct kstatfs *buf);

int fuse_get_link_initialize(struct fuse_bpf_args *fa, struct fuse_dummy_io *dummy,
		struct inode *inode, struct dentry *dentry,
		struct delayed_call *callback, const char **out);
int fuse_get_link_backing(struct fuse_bpf_args *fa,
		struct inode *inode, struct dentry *dentry,
		struct delayed_call *callback, const char **out);
void *fuse_get_link_finalize(struct fuse_bpf_args *fa,
		struct inode *inode, struct dentry *dentry,
		struct delayed_call *callback, const char **out);

int fuse_symlink_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *unused,
		struct inode *dir, struct dentry *entry, const char *link, int len);
int fuse_symlink_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, const char *link, int len);
void *fuse_symlink_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, const char *link, int len);

struct fuse_read_io {
	struct fuse_read_in fri;
	struct fuse_read_out fro;
};

int fuse_readdir_initialize(struct fuse_bpf_args *fa, struct fuse_read_io *frio,
			    struct file *file, struct dir_context *ctx,
			    bool *force_again, bool *allow_force, bool is_continued);
int fuse_readdir_backing(struct fuse_bpf_args *fa,
			 struct file *file, struct dir_context *ctx,
			 bool *force_again, bool *allow_force, bool is_continued);
void *fuse_readdir_finalize(struct fuse_bpf_args *fa,
			    struct file *file, struct dir_context *ctx,
			    bool *force_again, bool *allow_force, bool is_continued);

int fuse_access_initialize(struct fuse_bpf_args *fa, struct fuse_access_in *fai,
			   struct inode *inode, int mask);
int fuse_access_backing(struct fuse_bpf_args *fa, struct inode *inode, int mask);
void *fuse_access_finalize(struct fuse_bpf_args *fa, struct inode *inode, int mask);

/*
 * FUSE caches dentries and attributes with separate timeout.  The
 * time in jiffies until the dentry/attributes are valid is stored in
 * dentry->d_fsdata and fuse_inode->i_time respectively.
 */

/*
 * Calculate the time in jiffies until a dentry/attributes are valid
 */
static inline u64 time_to_jiffies(u64 sec, u32 nsec)
{
	if (sec || nsec) {
		struct timespec64 ts = {
			sec,
			min_t(u32, nsec, NSEC_PER_SEC - 1)
		};

		return get_jiffies_64() + timespec64_to_jiffies(&ts);
	} else
		return 0;
}

static inline u64 attr_timeout(struct fuse_attr_out *o)
{
	return time_to_jiffies(o->attr_valid, o->attr_valid_nsec);
}

static inline u64 entry_attr_timeout(struct fuse_entry_out *o)
{
	return time_to_jiffies(o->attr_valid, o->attr_valid_nsec);
}

static inline bool update_mtime(unsigned int ivalid, bool trust_local_mtime)
{
	/* Always update if mtime is explicitly set  */
	if (ivalid & ATTR_MTIME_SET)
		return true;

	/* Or if kernel i_mtime is the official one */
	if (trust_local_mtime)
		return true;

	/* If it's an open(O_TRUNC) or an ftruncate(), don't update */
	if ((ivalid & ATTR_SIZE) && (ivalid & (ATTR_OPEN | ATTR_FILE)))
		return false;

	/* In all other cases update */
	return true;
}

void fuse_fillattr(struct inode *inode, struct fuse_attr *attr,
			  struct kstat *stat);

static inline void iattr_to_fattr(struct fuse_conn *fc, struct iattr *iattr,
			   struct fuse_setattr_in *arg, bool trust_local_cmtime)
{
	unsigned int ivalid = iattr->ia_valid;

	if (ivalid & ATTR_MODE)
		arg->valid |= FATTR_MODE,   arg->mode = iattr->ia_mode;
	if (ivalid & ATTR_UID)
		arg->valid |= FATTR_UID,    arg->uid = from_kuid(fc->user_ns, iattr->ia_uid);
	if (ivalid & ATTR_GID)
		arg->valid |= FATTR_GID,    arg->gid = from_kgid(fc->user_ns, iattr->ia_gid);
	if (ivalid & ATTR_SIZE)
		arg->valid |= FATTR_SIZE,   arg->size = iattr->ia_size;
	if (ivalid & ATTR_ATIME) {
		arg->valid |= FATTR_ATIME;
		arg->atime = iattr->ia_atime.tv_sec;
		arg->atimensec = iattr->ia_atime.tv_nsec;
		if (!(ivalid & ATTR_ATIME_SET))
			arg->valid |= FATTR_ATIME_NOW;
	}
	if ((ivalid & ATTR_MTIME) && update_mtime(ivalid, trust_local_cmtime)) {
		arg->valid |= FATTR_MTIME;
		arg->mtime = iattr->ia_mtime.tv_sec;
		arg->mtimensec = iattr->ia_mtime.tv_nsec;
		if (!(ivalid & ATTR_MTIME_SET) && !trust_local_cmtime)
			arg->valid |= FATTR_MTIME_NOW;
	}
	if ((ivalid & ATTR_CTIME) && trust_local_cmtime) {
		arg->valid |= FATTR_CTIME;
		arg->ctime = iattr->ia_ctime.tv_sec;
		arg->ctimensec = iattr->ia_ctime.tv_nsec;
	}
}

static inline int finalize_attr(struct inode *inode, struct fuse_attr_out *outarg,
				u64 attr_version, struct kstat *stat)
{
	int err = 0;

	if (fuse_invalid_attr(&outarg->attr) ||
	    ((inode->i_mode ^ outarg->attr.mode) & S_IFMT)) {
		fuse_make_bad(inode);
		err = -EIO;
	} else {
		fuse_change_attributes(inode, &outarg->attr,
				       attr_timeout(outarg),
				       attr_version);
		if (stat)
			fuse_fillattr(inode, &outarg->attr, stat);
	}
	return err;
}

static inline void convert_statfs_to_fuse(struct fuse_kstatfs *attr, struct kstatfs *stbuf)
{
	attr->bsize   = stbuf->f_bsize;
	attr->frsize  = stbuf->f_frsize;
	attr->blocks  = stbuf->f_blocks;
	attr->bfree   = stbuf->f_bfree;
	attr->bavail  = stbuf->f_bavail;
	attr->files   = stbuf->f_files;
	attr->ffree   = stbuf->f_ffree;
	attr->namelen = stbuf->f_namelen;
	/* fsid is left zero */
}

static inline void convert_fuse_statfs(struct kstatfs *stbuf, struct fuse_kstatfs *attr)
{
	stbuf->f_type    = FUSE_SUPER_MAGIC;
	stbuf->f_bsize   = attr->bsize;
	stbuf->f_frsize  = attr->frsize;
	stbuf->f_blocks  = attr->blocks;
	stbuf->f_bfree   = attr->bfree;
	stbuf->f_bavail  = attr->bavail;
	stbuf->f_files   = attr->files;
	stbuf->f_ffree   = attr->ffree;
	stbuf->f_namelen = attr->namelen;
	/* fsid is left zero */
}

#ifdef CONFIG_FUSE_BPF
struct fuse_err_ret {
	void *result;
	bool ret;
};

int __init fuse_bpf_init(void);
void fuse_bpf_cleanup(void);

ssize_t fuse_bpf_simple_request(struct fuse_conn *fc, struct fuse_bpf_args *args);

/*
 * expression statement to wrap the backing filter logic
 * struct inode *inode: inode with bpf and backing inode
 * typedef io: (typically complex) type whose components fuse_args can point to.
 *	An instance of this type is created locally and passed to initialize
 * void initialize(struct fuse_bpf_args *fa, io *in_out, args...): function that sets
 *	up fa and io based on args
 * int backing(struct fuse_bpf_args *fa, args...): function that actually performs
 *	the backing io operation
 * void *finalize(struct fuse_bpf_args *, args...): function that performs any final
 *	work needed to commit the backing io
 */
static inline int fuse_bpf_run(struct bpf_prog *prog, struct fuse_bpf_args *fba)
{
	int ret;

	migrate_disable();
	ret = BPF_PROG_RUN(prog, fba);
	migrate_enable();
	return ret;
}

static inline rwf_t iocb_to_rw_flags(int ifl, int mask)
{
	rwf_t flags = 0;

	ifl &= mask;
	if (ifl & IOCB_APPEND)
		flags |= RWF_APPEND;
	if (ifl & IOCB_DSYNC)
		flags |= RWF_DSYNC;
	if (ifl & IOCB_HIPRI)
		flags |= RWF_HIPRI;
	if (ifl & IOCB_NOWAIT)
		flags |= RWF_NOWAIT;
	if (ifl & IOCB_SYNC)
		flags |= RWF_SYNC;

	return flags;
}

static inline void kiocb_clone(struct kiocb *kiocb, struct kiocb *kiocb_src,
			       struct file *filp)
{
	*kiocb = (struct kiocb){
		.ki_filp = filp,
		.ki_flags = kiocb_src->ki_flags,
		.ki_hint = kiocb_src->ki_hint,
		.ki_ioprio = kiocb_src->ki_ioprio,
		.ki_pos = kiocb_src->ki_pos,
	};
}

static inline ssize_t vfs_iocb_iter_read(struct file *file, struct kiocb *iocb,
					 struct iov_iter *iter)
{
	return call_read_iter(file, iocb, iter);
}

static inline ssize_t vfs_iocb_iter_write(struct file *file, struct kiocb *iocb,
					  struct iov_iter *iter)
{
	return call_write_iter(file, iocb, iter);
}

#define fuse_bpf_backing(inode, io, initialize, backing, finalize,	\
			 args...)					\
({									\
	struct fuse_err_ret fer = {0};					\
	int ext_flags;							\
	struct fuse_inode *fuse_inode = get_fuse_inode(inode);		\
	struct fuse_conn *fc = get_fuse_conn(inode);			\
	io feo = {0};							\
	struct fuse_bpf_args fa = {0}, fa_backup = {0};			\
	bool locked;							\
	ssize_t res;							\
	void *err;							\
	int i;								\
	bool initialized = false;					\
									\
	do {								\
		if (!fuse_inode || !fuse_inode->backing_inode)		\
			break;						\
									\
		err = ERR_PTR(initialize(&fa, &feo, args));		\
		if (err) {						\
			fer = (struct fuse_err_ret) {			\
				err,					\
				true,					\
			};						\
			break;						\
		}							\
		initialized = true;					\
									\
		fa_backup = fa;						\
		fa.opcode |= FUSE_PREFILTER;				\
		for (i = 0; i < fa.in_numargs; ++i)			\
			fa.out_args[i] = (struct fuse_bpf_arg) {	\
				.size = fa.in_args[i].size,		\
				.value = (void *)fa.in_args[i].value,	\
			};						\
		fa.out_numargs = fa.in_numargs;				\
									\
		ext_flags = fuse_inode->bpf ?				\
			fuse_bpf_run(fuse_inode->bpf, &fa) :		\
			FUSE_BPF_BACKING;				\
		if (ext_flags < 0) {					\
			fer = (struct fuse_err_ret) {			\
				ERR_PTR(ext_flags),			\
				true,					\
			};						\
			break;						\
		}							\
									\
		if (ext_flags & FUSE_BPF_USER_FILTER) {			\
			locked = fuse_lock_inode(inode);		\
			res = fuse_bpf_simple_request(fc, &fa);		\
			fuse_unlock_inode(inode, locked);		\
			if (res < 0) {					\
				fer = (struct fuse_err_ret) {		\
					ERR_PTR(res),			\
					true,				\
				};					\
				break;					\
			}						\
		}							\
									\
		if (!(ext_flags & FUSE_BPF_BACKING))			\
			break;						\
									\
		fa.opcode &= ~FUSE_PREFILTER;				\
		for (i = 0; i < fa.in_numargs; ++i)			\
			fa.in_args[i] = (struct fuse_bpf_in_arg) {	\
				.size = fa.out_args[i].size,		\
				.value = fa.out_args[i].value,		\
			};						\
		for (i = 0; i < fa_backup.out_numargs; ++i)		\
			fa.out_args[i] = (struct fuse_bpf_arg) {	\
				.size = fa_backup.out_args[i].size,	\
				.value = fa_backup.out_args[i].value,	\
			};						\
		fa.out_numargs = fa_backup.out_numargs;			\
									\
		fer = (struct fuse_err_ret) {				\
			ERR_PTR(backing(&fa, args)),			\
			true,						\
		};							\
		if (IS_ERR(fer.result))					\
			fa.error_in = PTR_ERR(fer.result);		\
		if (!(ext_flags & FUSE_BPF_POST_FILTER))		\
			break;						\
									\
		fa.opcode |= FUSE_POSTFILTER;				\
		for (i = 0; i < fa.out_numargs; ++i)			\
			fa.in_args[fa.in_numargs++] =			\
				(struct fuse_bpf_in_arg) {		\
					.size = fa.out_args[i].size,	\
					.value = fa.out_args[i].value,	\
				};					\
		ext_flags = fuse_bpf_run(fuse_inode->bpf, &fa);		\
		if (ext_flags < 0) {					\
			fer = (struct fuse_err_ret) {			\
				ERR_PTR(ext_flags),			\
				true,					\
			};						\
			break;						\
		}							\
		if (!(ext_flags & FUSE_BPF_USER_FILTER))		\
			break;						\
									\
		fa.out_args[0].size = fa_backup.out_args[0].size;	\
		fa.out_args[1].size = fa_backup.out_args[1].size;	\
		fa.out_numargs = fa_backup.out_numargs;			\
		locked = fuse_lock_inode(inode);			\
		res = fuse_bpf_simple_request(fc, &fa);			\
		fuse_unlock_inode(inode, locked);			\
		if (res < 0) {						\
			fer.result = ERR_PTR(res);			\
			break;						\
		}							\
	} while (false);						\
									\
	if (initialized && fer.ret) {					\
		err = finalize(&fa, args);				\
		if (err)						\
			fer.result = err;				\
	}								\
									\
	fer;								\
})

#endif /* CONFIG_FUSE_BPF */

#endif /* _FS_FUSE_I_H */

