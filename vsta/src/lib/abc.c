/*
 * abc.c
 *	Asynchronous Buffer Cache
 *
 * Buffered block I/O interface down to a physical device.  Provides
 * both read-ahead and write-behind, while providing the illusion of
 * a synchronous block I/O device to its caller.
 */
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/syscall.h>
#include <sys/sched.h>		/* For ephemeral threads */
#include <std.h>
#include <unistd.h>
#include <fcntl.h>
#include <llist.h>
#include <hash.h>
#include <lock.h>
#include <time.h>
#include <stdio.h>
#include "abc.h"

/*
 * Sector size, and log2(size)
 */
#define SECSZ (512)
#define SECSHIFT (9)
#define EXTSIZ (128)

/*
 * Description of a particular buffer of data
 */
struct buf {
	volatile lock_t b_lock;	/* Mutex for FG/BG data structures */
	struct llist *b_list;	/* Linked under bufhead */
	void *b_data;		/* Actual data */
	daddr_t b_start;	/* Starting sector # */
	uint b_nsec;		/*  ...# SECSZ units contained */
	volatile uint b_flags;	/* Flags */
	volatile uint b_locks;	/* Count of locks on buf */
	void **b_handles;	/* Tags associated with B_DIRTY */
	uint b_nhandle;		/*  ...# pointed to */
};

/*
 * Bits in b_flags
 */
#define B_SEC0 0x1		/* 1st sector valid  */
#define B_SECS 0x2		/*  ...rest of sectors valid too */
#define B_DIRTY 0x4		/* Some sector in buffer is dirty */
#define B_WANT 0x8		/* Wanted by FG when !B_BUSY */
#define B_BUSY 0x10		/* Op in progress by BG */

/*
 * Useful macro
 */
#define BUSY(b) ((b)->b_flags & B_BUSY)

/*
 * Description of an operation which the FG asks the BG to do.
 * B_BUSY will be cleared at completion of the operation, as will
 * q_op.
 */
#define NQIO (32)
struct qio {
	struct buf *q_buf;	/* Buf to be used */
	volatile uint q_op;	/* Operation */
};
static struct qio qios[NQIO];	/* Ring of operations */
static volatile uint qnext;	/* Next position in ring */

/*
 * Operations
 */
#define Q_IDLE (0)		/* Flag that op is complete */
#define Q_FILLBUF (1)		/* Fill buffer */
#define Q_FLUSHBUF (2)		/* Write buffer out */

/*
 * Local variables
 */
static uint bufsize;		/* # sectors held in memory currently */
static struct hash *bufpool;	/* Hash daddr_t -> buf */
static struct llist allbufs;	/* Time-ordered list, for aging */
static port_t ioport;		/* I/O device */
static int can_dma;		/*  ...supports DMA? */
static uint coresec;		/* # sectors allowed in core at once */
static pid_t fg_pid, bg_pid;	/* Thread ID's for FG/BG */

static void _sync_buf(struct buf *b, int from_qio);

/*
 * get()
 *	Access buffer, interlocking with BG
 */
static void
get(struct buf *b)
{
	if (!(b->b_flags & B_BUSY)) {
		return;
	}
	p_lock(&b->b_lock);
	if (!(b->b_flags & B_BUSY)) {
		v_lock(&b->b_lock);
		return;
	}
	b->b_flags |= B_WANT;
	v_lock(&b->b_lock);
	mutex_thread(0);
	ASSERT_DEBUG(!(b->b_flags & (B_WANT|B_BUSY)), "get: still busy/wanted");
}

/*
 * stob()
 *	Convert sectors to bytes
 */
inline static uint
stob(uint nsec)
{
	return (nsec << SECSHIFT);
}

/*
 * read_secs()
 *	Do sector reads
 */
static void
read_secs(daddr_t start, void *buf, uint nsec)
{
	struct msg m;

	m.m_op = FS_ABSREAD | (can_dma ? 0 : M_READ);
	m.m_nseg = 1;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = stob(nsec);
	m.m_arg1 = stob(start);
	if (msg_send(ioport, &m) < 0) {
		ASSERT(0, "read_secs: io");
	}
}

/*
 * write_secs()
 *	Do sector writes
 */
static void
write_secs(daddr_t start, void *buf, uint  nsec)
{
	struct msg m;

	m.m_op = FS_ABSWRITE;
	m.m_nseg = 1;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = stob(nsec);
	m.m_arg1 = stob(start);
	if (msg_send(ioport, &m) < 0) {
		ASSERT(0, "write_secs: io");
	}
}

/*
 * free_buf()
 *	Release buffer storage, remove from hash
 */
static void
free_buf(struct buf *b)
{
	ASSERT_DEBUG(b->b_list, "free_buf: null b_list");
	ASSERT_DEBUG(b->b_locks == 0, "free_buf: locks");
	ll_delete(b->b_list);
	(void)hash_delete(bufpool, b->b_start);
	bufsize -= b->b_nsec;
	ASSERT_DEBUG(b->b_data, "free_buf: null b_data");
	free(b->b_data);
	if (b->b_handles) {
		free(b->b_handles);
	}
	free(b);
}

/*
 * exec_qio()
 *	Do the actions specified by a qio operation
 */
static void
exec_qio(struct buf *b, int op)
{
	ASSERT_DEBUG(BUSY(b), "exec_qio: !busy");
	switch (op) {
	case Q_FILLBUF:
		if (b->b_flags & B_SEC0) {
			read_secs(b->b_start + 1,
				(char *)b->b_data + SECSZ,
				b->b_nsec - 1);
		} else {
			read_secs(b->b_start,
				b->b_data, b->b_nsec);
		}
		b->b_flags |= (B_SEC0|B_SECS);
		break;

	case Q_FLUSHBUF:
		_sync_buf(b, 1);
		break;

	default:
		ASSERT_DEBUG(0, "bg_thread: qio");
		break;
	}
	ASSERT_DEBUG(BUSY(b), "exec_qio: went !busy");
}

/*
 * busywait()
 *	Wait and sleep for a location to return to zero
 */
static void
busywait(volatile uint *ptr)
{
	while (*ptr) {
		__msleep(10);
	}
}

/*
 * qio()
 *	Queue an operation for the BG thread
 */
static void
qio(struct buf *b, uint op)
{
	struct qio *q;
	uint next;

	/*
	 * This buffer is busy until op complete
	 */
	ASSERT_DEBUG(!BUSY(b), "qio: busy");
	b->b_flags |= B_BUSY;

	/*
	 * Get next ring element
	 */
	next = qnext;
	q = &qios[next];
	if (++next >= NQIO) {
		next = 0;
	}
	qnext = next;

	/*
	 * Wait for it to be ready
	 */
	busywait(&q->q_op);

	/*
	 * Fill it in
	 */
	q->q_buf = b;
	q->q_op = op;

	/*
	 * Release BG to do its thing
	 */
	mutex_thread(bg_pid);
}

/*
 * age_buf()
 *	Find the next available buf header, flush and free it
 *
 * Since this is basically a paging algorithm, it can become arbitrarily
 * complex.  The algorithm here tries to be simple, yet somewhat fair.
 */
static void
age_buf(void)
{
	struct llist *l;
	struct buf *b;

	/*
	 * Pick the oldest buf which isn't locked.
	 */
	for (l = allbufs.l_back; l != &allbufs; l = l->l_back) {
		/*
		 * Only skip if wired or active
		 */
		b = l->l_data;
		if (b->b_locks || BUSY(b)) {
			continue;
		}

		ASSERT_DEBUG(b->b_lock == 0, "age_buf: lock");
		if (!(b->b_flags & B_DIRTY)) {
			/*
			 * Remove from list, update data structures
			 */
			free_buf(b);
			return;
		}

		/*
		 * Sync out data in background
		 */
		qio(b, Q_FLUSHBUF);
	}
	ASSERT_DEBUG(bufsize <= coresec, "age_buf: buffers too large");
}

/*
 * find_buf()
 *	Given starting sector #, return pointer to buf
 */
struct buf *
find_buf(daddr_t d, uint nsec, int flags)
{
	struct buf *b;

	ASSERT_DEBUG(nsec > 0, "find_buf: zero");
	ASSERT_DEBUG(nsec <= EXTSIZ, "find_buf: too big");

	/*
	 * If we can find it, this is easy
	 */
	b = hash_lookup(bufpool, d);
	if (b) {
		return(b);
	}

	/*
	 * Get a buf struct
	 */
	b = malloc(sizeof(struct buf));
	if (b == 0) {
		return(0);
	}

	/*
	 * Make room in our buffer cache if needed
	 */
	while ((bufsize+nsec) > coresec) {
		age_buf();
	}

	/*
	 * Get the buffer space
	 */
	b->b_data = malloc(stob(nsec));
	if (b->b_data == 0) {
		free(b);
		return(0);
	}

	/*
	 * Add us to pool, and mark us very new
	 */
	b->b_list = ll_insert(&allbufs, b);
	if (b->b_list == 0) {
		free(b->b_data);
		free(b);
		return(0);
	}
	if (hash_insert(bufpool, d, b)) {
		ll_delete(b->b_list);
		free(b->b_data);
		free(b);
		return(0);
	}

	/*
	 * Fill in the rest & return
	 */
	init_lock(&b->b_lock);
	b->b_start = d;
	b->b_nsec = nsec;
	b->b_locks = 0;
	b->b_handles = 0;
	b->b_nhandle = 0;
	if (flags & ABC_FILL) {
		b->b_flags = 0;
	} else {
		b->b_flags = B_SEC0 | B_SECS;
	}
	bufsize += nsec;

	/*
	 * If ABC_BG, initiate fill now
	 */
	if (flags & ABC_BG) {
		qio(b, Q_FILLBUF);
	}

	return(b);
}

/*
 * resize_buf()
 *	Indicate that the cached region is changing to newsize
 *
 * If "fill" is non-zero, the incremental contents are filled from disk.
 * Otherwise the buffer space is left uninitialized.
 *
 * Returns 0 on success, 1 on error.
 */
int
resize_buf(daddr_t d, uint newsize, int fill)
{
	char *p;
	struct buf *b;

	ASSERT_DEBUG(newsize <= EXTSIZ, "resize_buf: too large");
	ASSERT_DEBUG(newsize > 0, "resize_buf: zero");

	/*
	 * If it isn't currently buffered, we don't care yet
	 */
	if (!(b = hash_lookup(bufpool, d))) {
		return(0);
	}

	/*
	 * Current activity--move to end of age list
	 */
	ll_movehead(&allbufs, b->b_list);

	/*
	 * Resize to current size is a no-op
	 */
	if (newsize == b->b_nsec) {
		return(0);
	}

	/*
	 * Ok, we're going to do it, interlock
	 */
	get(b);

	/*
	 * Get the buffer space
	 */
	ASSERT_DEBUG(!(fill && (newsize < b->b_nsec)),
		"resize_buf: fill && shrink");
	p = realloc(b->b_data, stob(newsize));
	if (p == 0) {
		return(1);
	}
	b->b_data = p;

	/*
	 * If needed, fill from disk
	 */
	if (fill && (b->b_flags & B_SECS)) {
		ASSERT_DEBUG(newsize > b->b_nsec,
			"resize_buf: fill when shrinking");
		read_secs(b->b_start + b->b_nsec, p + stob(b->b_nsec),
			newsize - b->b_nsec);
	}

	/*
	 * Update buf and return success
	 */
	bufsize = (int)bufsize + ((int)newsize - (int)b->b_nsec);
	b->b_nsec = newsize;
	while (bufsize > coresec) {
		age_buf();
	}
	return(0);
}

/*
 * index_buf()
 *	Get a pointer to a run of data under a particular buf entry
 *
 * As a side effect, move us to front of list to make us relatively
 * undesirable for aging.
 */
void *
index_buf(struct buf *b, uint index, uint nsec)
{
	ASSERT_DEBUG((index+nsec) <= b->b_nsec, "index_buf: too far");

	get(b);
	ll_movehead(&allbufs, b->b_list);
	if ((index == 0) && (nsec == 1)) {
		/*
		 * Only looking at 1st sector.  See about reading
		 * only 1st sector, if we don't yet have it.
		 */
		if ((b->b_flags & B_SEC0) == 0) {
			/*
			 * Load the sector, mark it as present
			 */
			read_secs(b->b_start, b->b_data, 1);
			b->b_flags |= B_SEC0;
		}
	} else if ((b->b_flags & B_SECS) == 0) {
		/*
		 * Otherwise if we don't have the whole buffer, get
		 * it now.  Don't read in sector 0 if we already
		 * have it.
		 */
		if (b->b_flags & B_SEC0) {
			read_secs(b->b_start + 1, (char *)b->b_data + SECSZ,
				b->b_nsec - 1);
		} else {
			read_secs(b->b_start, b->b_data, b->b_nsec);
		}
		b->b_flags |= (B_SEC0|B_SECS);
	}
	return((char *)b->b_data + stob(index));
}

/*
 * bg_thread()
 *	Endless loop to take QIO operations and execute them
 */
static void
bg_thread(int dummy)
{
	uint next = 0, want;
	struct qio *q;
	struct buf *b;

	/*
	 * Become ephemeral
	 */
	(void)sched_op(SCHEDOP_EPHEM, 0);

	/*
	 * Endless loop, serving background requests
	 */
	for (;;) {
		/*
		 * Get next operation
		 */
		mutex_thread(0);
		q = &qios[next++];
		if (next >= NQIO) {
			next = 0;
		}

		/*
		 * Execute it
		 */
		exec_qio(b = q->q_buf, q->q_op);

		/*
		 * Flag completion
		 */
		q->q_op = 0;
		p_lock(&b->b_lock);
		want = b->b_flags & B_WANT;
		b->b_flags &= ~(B_BUSY | B_WANT);
		v_lock(&b->b_lock);
		if (want) {
			mutex_thread(fg_pid);
		}
	}
}

/*
 * init_buf()
 *	Initialize the buffering system
 */
void
init_buf(port_t arg_ioport, int arg_coresec)
{
	char *p;

	/*
	 * Record args
	 */
	ioport = arg_ioport;
	coresec = arg_coresec;

	/*
	 * Initialize data structures
	 */
	ll_init(&allbufs);
	bufpool = hash_alloc(coresec / 8);
	bufsize = 0;
	ASSERT_DEBUG(bufpool, "init_buf: bufpool");
	fg_pid = gettid();

	/*
	 * Record whether DMA is supported
	 */
	p = rstat(ioport, "dma");
	can_dma = p && atoi(p);

	/*
	 * Spin off background thread
	 */
	bg_pid = tfork(bg_thread, 0);
}

/*
 * dirty_buf()
 *	Mark the given buffer dirty
 *
 * If a handle is given, mark the dirty buffer with this handle
 */
void
dirty_buf(struct buf *b, void *handle)
{
	void **p, **zp;
	uint x;

	/* 
	 * Mark buffer dirty
	 */
	get(b);
	b->b_flags |= B_DIRTY;

	/*
	 * No handle -> done
	 */
	if (!handle) {
		return;
	}

	/*
	 * See if this handle is already tagged
	 */
	p = b->b_handles;
	zp = 0;
	for (x = 0; x < b->b_nhandle; ++x) {
		/*
		 * Yup, all done
		 */
		if (p[x] == handle) {
			return;
		}

		/*
		 * Record an open position if found
		 */
		if (p[x] == 0) {
			zp = &p[x];
		}
	}

	/*
	 * Not tagged, but there's a position here
	 */
	if (zp) {
		*zp = handle;
		return;
	}

	/*
	 * Grow the array, and save us at the end
	 */
	b->b_handles =
		realloc(b->b_handles,
			(b->b_nhandle + 1) * sizeof(void *));
	b->b_handles[b->b_nhandle] = handle;
	b->b_nhandle += 1;
}

/*
 * lock_buf()
 *	Lock down the buf; make sure it won't go away
 */
void
lock_buf(struct buf *b)
{
	b->b_locks += 1;
	ASSERT_DEBUG(b->b_locks > 0, "lock_buf: overflow");
}

/*
 * unlock_buf()
 *	Release previously taken lock
 */
void
unlock_buf(struct buf *b)
{
	ASSERT_DEBUG(b->b_locks > 0, "unlock_buf: underflow");
	b->b_locks -= 1;
}

/*
 * _sync_buf()
 *	Sync back buffer if dirty
 *
 * Write back the 1st sector, or the whole buffer, as appropriate
 */
static void
_sync_buf(struct buf *b, int from_qio)
{
	ASSERT_DEBUG(b->b_flags & (B_SEC0 | B_SECS), "sync_buf: not ref'ed");

	/*
	 * Skip it if not dirty
	 */
	if (!(b->b_flags & B_DIRTY)) {
		return;
	}

	/*
	 * Do the I/O--whole buffer, or just 1st sector if that was
	 * the only sector referenced.
	 */
	if (!from_qio) {
		get(b);
	}
	if (b->b_flags & B_SECS) {
		write_secs(b->b_start, b->b_data, b->b_nsec);
	} else {
		write_secs(b->b_start, b->b_data, 1);
	}
	p_lock(&b->b_lock);
	b->b_flags &= ~B_DIRTY;
	v_lock(&b->b_lock);

	/*
	 * If there are possible handles, clear them too
	 */
	if (b->b_handles) {
		bzero(b->b_handles, b->b_nhandle * sizeof(void *));
	}
}

/*
 * sync_buf()
 *	User-called wrapper to _sync_buf()
 */
void
sync_buf(struct buf *b)
{
	_sync_buf(b, 0);
}

/*
 * inval_buf()
 *	Clear out (without sync'ing) some buffer data
 *
 * This routine will handle multiple buffer entries, but "d" must
 * point to an aligned beginning of such an entry.
 */
void
inval_buf(daddr_t d, uint len)
{
	struct buf *b;

	for (;;) {
		b = hash_lookup(bufpool, d);
		if (b) {
			get(b);
			free_buf(b);
		}
		if (len <= EXTSIZ) {
			break;
		}
		d += EXTSIZ;
		len -= EXTSIZ;
	}
}

/*
 * sync()
 *	Write dirty buffers to disk
 *
 * If handle is not NULL, sync all buffers dirtied with this handle.
 * Otherwise sync all dirty buffers.
 */
void
sync_bufs(void *handle)
{
	struct llist *l;
	uint x;

	for (l = LL_NEXT(&allbufs); l != &allbufs; l = LL_NEXT(l)) {
		struct buf *b = l->l_data;

		/*
		 * Not dirty--easy
		 */
		if (!(b->b_flags & B_DIRTY)) {
			continue;
		}

		/*
		 * Interlock
		 */
		get(b);

		/*
		 * Not dirty after interlock--still easy
		 */
		if (!(b->b_flags & B_DIRTY)) {
			continue;
		}

		/* 
		 * No handle, just sync dirty buffers
		 */
		if (!handle) {
			qio(b, Q_FLUSHBUF);
			continue;
		}

		/*
		 * Check for match.
		 */
		for (x = 0; x < b->b_nhandle; ++x) {
			if (b->b_handles[x] == handle) {
				qio(b, Q_FLUSHBUF);
				break;
			}
		}
	}
}
