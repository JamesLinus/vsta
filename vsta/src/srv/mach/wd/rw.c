/*
 * rw.c
 *	Reads and writes to the hard disk
 */
#include <wd/wd.h>
#include <sys/fs.h>
#include <lib/llist.h>
#include <sys/assert.h>
#include <std.h>

extern void wd_readdir();

/*
 * Busy/waiter flags
 */
int busy;		/* Busy, and unit # */
int busy_unit;
struct llist waiters;	/* Who's waiting */

extern int upyet;	/* All partitioning read yet? */

/*
 * queue_io()
 *	Record parameters of I/O, queue to unit
 */
static
queue_io(struct msg *m, struct file *f)
{
	uint unit, cnt;
	ulong part_off;

	unit = NODE_UNIT(f->f_node);
	ASSERT_DEBUG(unit < NWD, "queue_io: bad unit");

	/*
	 * Get a block offset based on partition
	 */
	switch (get_offset(f->f_node, f->f_pos / SECSZ, &part_off, &cnt)) {
	case 0:			/* Everything's OK */
		break;
	case 1:			/* At end of partition (EOF) */
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		return(1);
	case 2:			/* Illegal offset */
		msg_err(m->m_sender, EINVAL);
		return(1);
	}

	/*
	 * If they didn't provide a buffer, generate one for
	 * ourselves.
	 */
	if (m->m_nseg == 0) {
		f->f_buf = malloc(m->m_arg);
		if (f->f_buf == 0) {
			msg_err(m->m_sender, ENOMEM);
			return(1);
		}
		f->f_local = 1;
	} else {
		f->f_buf = m->m_buf;
		f->f_local = 0;
	}
	f->f_count = m->m_arg / SECSZ;
	if (f->f_count > cnt) {
		f->f_count = cnt;
	}
	f->f_unit = unit;
	f->f_blkno = part_off;
	f->f_op = m->m_op;
	if ((f->f_list = ll_insert(&waiters, f)) == 0) {
		if (f->f_local) {
			free(f->f_buf);
		}
		msg_err(m->m_sender, ENOMEM);
		return(1);
	}
	return(0);
}

/*
 * run_queue()
 *	If there's stuff in queue, launch next
 *
 * If there's nothing, clears "busy"
 */
static void
run_queue(void)
{
	struct file *f;

	/*
	 * Nobody waiting--disk falls idle
	 */
	if (waiters.l_forw == &waiters) {
		busy = 0;
		return;
	}

	/*
	 * Remove next from list
	 */
	f = waiters.l_forw->l_data;
	ll_delete(waiters.l_forw);

	/*
	 * Launch I/O
	 */
	busy = 1;
	busy_unit = f->f_unit;
	wd_io(f->f_op, f, f->f_unit, f->f_blkno, f->f_buf, f->f_count);
}

/*
 * wd_rw()
 *	Do I/O to the disk
 *
 * m_arg specifies how much they want.  It must be in increments
 * of sector sizes, or we EINVAL'em out of here.
 */
void
wd_rw(struct msg *m, struct file *f)
{
	/*
	 * Sanity check operations on directories
	 */
	if (m->m_op == FS_READ) {
		if (f->f_node == ROOTDIR) {
			wd_readdir(m, f);
			return;
		}
	} else {
		/* FS_WRITE */
		if ((f->f_node == ROOTDIR) || (m->m_nseg != 1)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
	}

	/*
	 * Check size and alignment
	 */
	if ((m->m_arg & (SECSZ-1)) ||
			(m->m_arg > MAXIO) ||
			(f->f_pos & (SECSZ-1))) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Check permission
	 */
	if (((m->m_op == FS_READ) && !(f->f_flags & ACC_READ)) ||
			((m->m_op == FS_WRITE) && !(f->f_flags & ACC_WRITE))) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Queue I/O to unit
	 */
	if (!queue_io(m, f) && !busy) {
		run_queue();
	}
}

/*
 * iodone()
 *	Called from disk level when an I/O is completed
 *
 * This routine has two modes; before upyet, we are just iteratively
 * reading first sectors of configured disks, and firing up our
 * partition interpreter to get partitioning information.  Once
 * we've done this for all disks, this routine becomes your standard
 * "finish one, start another" handler.
 */
void
iodone(void *tran, int result)
{
	int x;
	uint unit;
	struct file *f;

	ASSERT_DEBUG(tran != 0, "iodone: null tran");

	/*
	 * Special case before we're "upyet"
	 */
	if (!upyet) {
		extern char *secbuf;
		extern int configed[];

		/*
		 * "tran" is just our unit #, casted
		 */
		unit = ((uint)tran) - 1;
		ASSERT(unit < NWD, "iodone: !upyet bad unit");
		init_part(unit, secbuf);

		/*
		 * Find next unit to process
		 */
		for (x = unit+1; x < NWD; ++x) {
			if (configed[x]) {
				wd_io(FS_READ, (void *)(x+1),
					x, 0L, secbuf, 1);
				return;
			}
		}

		/*
		 * We've covered all--ready for clients!
		 */
		upyet = 1;
		return;
	}

	/*
	 * I/O completion
	 */
	f = tran;
	if (result == -1) {
		/*
		 * I/O error; complete this operation
		 */
		msg_err(f->f_sender, EIO);
	} else {
		struct msg m;

		/*
		 * Success.  Return count of bytes processed and
		 * buffer if local.
		 */
		m.m_arg = f->f_count * SECSZ;
		if (f->f_local) {
			m.m_nseg = 1;
			m.m_buf = f->f_buf;
			m.m_buflen = m.m_arg;
		} else {
			m.m_nseg = 0;
		}
		m.m_arg1 = 0;
		msg_reply(f->f_sender, &m);
	}

	/*
	 * Free local buffer, clear busy field
	 */
	if (f->f_local) {
		free(f->f_buf);
	}
	f->f_list = 0;

	/*
	 * Run next request, if any
	 */
	run_queue();
}

/*
 * rw_init()
 *	Initialize our queue data structure
 */
void
rw_init(void)
{
	ll_init(&waiters);
}
