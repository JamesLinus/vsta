/*
 * rw.c
 *	Reads and writes to the hard disk
 */
#include <stdio.h>
#include <sys/fs.h>
#include <llist.h>
#include <sys/assert.h>
#include <stdlib.h>
#include <syslog.h>
#include "wd.h"

extern void wd_readdir();

/*
 * Busy/waiter flags
 */
int busy;			/* Busy, and unit # */
int busy_unit;
struct llist waiters;		/* Who's waiting */
char *secbuf = NULL;

extern uint partundef;		/* All partitioning read yet? */
extern char configed[];
extern struct disk disks[];
extern struct wdparms parm[];


static int update_partition_list(int, int);


/*
 * queue_io()
 *	Record parameters of I/O, queue to unit
 */
static int
queue_io(struct msg *m, struct file *f)
{
	uint unit, cnt;
	ulong part_off;

	unit = NODE_UNIT(f->f_node);
	ASSERT_DEBUG(unit < NWD, "queue_io: bad unit");

	/*
	 * Get a block offset based on partition
	 */
	switch (dpart_get_offset(disks[unit].d_parts, NODE_SLOT(f->f_node),
				 f->f_pos / SECSZ, &part_off, &cnt)) {
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
	if ((m->m_arg & (SECSZ - 1)) ||
			(m->m_arg > MAXIO) ||
			(m->m_arg == 0) ||
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
 * This routine has two modes; before partundef is 0, we are just iteratively
 * reading first sectors of configured disks, and firing up our partition
 * interpreter to get partitioning information.  Once we've done this for all
 * disks, this routine becomes your standard "finish one, start another"
 * handler.
 */
void
iodone(void *tran, int result)
{
	uint unit;
	struct file *f;

	ASSERT_DEBUG(tran != 0, "iodone: null tran");

	/*
	 * Special case when the partition entries are undefined
	 */
	if (partundef) {
		/*
		 * "tran" is just our unit #, casted
		 */
		unit = ((uint)tran) - 1;
		ASSERT(unit < NWD, "iodone: partundef bad unit");
		if (update_partition_list(unit, 0) == 0) {
			return;
		}

		/*
		 * OK, have we now read all of the partition data?
		 */
		if (partundef) {
			int i;

			for (i = 0; i < NWD; i++) {
				if (partundef & (1 << i)) {
					if (configed[i]) {
						update_partition_list(i, 1);
						return;
					} else {
						partundef &= (0xffffffff
							      ^ (1 << i));
					}
				}
			}		
		}
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


/*
 * rw_readpartitions()
 *	Read the drive partition tables for the specified disk
 *
 * We start with the assumption that any parameters we already have must be
 * invalidated, so we perform the invalidation first.  Note that if other
 * partition lists have been invalidated elsewhere that these will be
 * refetched when we fetch these
 */
void
rw_readpartitions(int unit)
{
	/*
	 * First, bounce any requests to non-configured drives
	 */
	if (!configed[unit]) {
		return;
	}

	/*
	 * Ensure that we have a suitable sector buffer with which to work
	 */
	if (secbuf == NULL) {
		secbuf = malloc(SECSZ * 2);
		if (secbuf == NULL) {
			syslog(LOG_ERR, "wd: sector buffer");
			return;
		}
		secbuf = (char *)roundup((long)secbuf, SECSZ);
	}

	/*
	 * Reset the partition valid flag for this unit
	 */
	partundef |= (1 << unit);

	/*
	 * We now issue successive calls to read any unknown partition
	 * information
	 */
	update_partition_list(unit, 1);
}


/*
 * update_partition_list()
 *	Update the partition data for the specified disk unit
 *
 * This routine is called to carry out the management of disk partition
 * data.  It is responsible for issuing read requests and tracking which
 * device is being talked to
 *
 * Returns 1 when the update is complete, 0 if it's only partway done.
 */
static int
update_partition_list(int unit, int initiating)
{
	static int sector_num, next_part;

	/*
	 * Are we initiating a partition read?
	 */
	if (initiating) {
		/*
		 * OK, zero down the reference markers, establish the
		 * "whole disk" parameters and do the 1st read
		 */
		dpart_init_whole("wd", unit, parm[unit].w_size,
				 disks[unit].d_parts);
		sector_num = 0;
		next_part = FIRST_PART;
		wd_io(FS_READ, (void *)(unit + 1), unit, 0L, secbuf, 1);
		return 0;
	} else {
		/*
		 * We must bein the middle of an update so sort out the
		 * table manipulations and start any further reads
		 */
		if (dpart_init("wd", unit, secbuf, &sector_num,
			       disks[unit].d_parts, &next_part) == 0) {
			sector_num = 0;
		}
		if (sector_num != 0) {
			wd_io(FS_READ, (void *)(unit + 1), unit,
			      sector_num, secbuf, 1);
			return 0;
		} else {
			partundef &= (0xffffffff ^ (1 << unit));
			return 1;
	        }
	}
}
