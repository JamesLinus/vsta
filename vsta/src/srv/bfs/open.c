/*
 * Filename:	open.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 17th February 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Routines for opening, closing, creating  and deleting files
 *
 * Note that we don't allow subdirectories for BFS, which simplifies
 * things.
 */


#include <std.h>
#include <stdio.h>
#include <sys/assert.h>
#include "bfs.h"


extern struct super *sblock;
static int nwriters = 0;	/* # writers active */


/*
 * move_file()
 *	Transfer the current file of a struct file to the given inode
 */
static void
move_file(struct file *f, struct inode *i, int writing)
{
	ASSERT_DEBUG(f->f_inode->i_num == ROOTINODE, "move_file: not root");
	f->f_inode = i;
	f->f_pos = 0L;
	if (f->f_write == writing)
		nwriters += 1;
}


/*
 * bfs_open()
 *	Main entry for processing an open message
 */
void
bfs_open(struct msg *m, struct file *f)
{
	struct inode *i;
	int iexists = 0;

	/*
	 * Have to be in root dir to open down into a file
	 */
	if (f->f_inode->i_num != ROOTINODE) {
		msg_err(m->m_sender, ENOTDIR);
		return;
	}

	/*
	 * Check for permission
	 */
	if (m->m_arg & (ACC_WRITE|ACC_CREATE|ACC_DIR)) {
		/*
		 * No subdirs in a boot filesystem
		 */
		if (m->m_arg & ACC_DIR) {
			msg_err(m->m_sender, EINVAL);
			return;
		}

		/*
		 * Only one writer at a time
		 */
		if (nwriters > 0) {
			msg_err(m->m_sender, EBUSY);
			return;
		}

		/*
		 * Insufficient priveleges
		 */
		if (f->f_write == 0) {
			msg_err(m->m_sender, EPERM);
			return;
		}
	}

	/*
	 * Look up name
	 */
	i = ino_lookup(m->m_buf);
	if (i) {
		iexists = 1;
	}

	/*
	 * No such file--do they want to create?
	 */
	if (!iexists && !(m->m_arg & ACC_CREATE)) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * If it's a new file, allocate the entry now.
	 */
	if (!iexists) {
		if ((i = ino_new(m->m_buf)) == NULL) {
			msg_err(m->m_sender, strerror());
			return;
		}
	}

	/*
	 * If they want to use the existing file, set up the
	 * inode and let them go for it.  Note that this case
	 * MUST be iexists, or it would have been caught above.
	 */
	if (!(m->m_arg & ACC_CREATE)) {
		move_file(f, i, m->m_arg & ACC_WRITE);
	} else {
		/*
		 * Creation is desired.  If there's an existing file, free
		 * its storage.
		 */
		if (iexists) {
			blk_trunc(i);
			/* Marked bdirty() below */
		}

		/*
		 * Move pointers down to next free storage block
		 */
		i->i_start = 0;
		i->i_fsize = 0;
		ino_dirty(i);
		move_file(f, i, m->m_arg & ACC_WRITE);
	}
	
	ino_ref(i);
	m->m_buf = 0;
	m->m_buflen = 0;
	m->m_nseg = 0;
	m->m_arg1 = m->m_arg = 0;
	msg_reply(m->m_sender, m);
}


/*
 * bfs_close()
 *	Do closing actions on a file
 *
 * There is no FS_CLOSE message; this is entered in response to the
 * connection being terminated.
 */
void
bfs_close(struct file *f)
{
	/*
	 * A reference to the root dir needs no action
	 */
	if (f->f_inode->i_num == ROOTINODE)
		return;

	/*
	 * Free inode reference, decrement writers count
	 * if it was a writer.
	 */
	ino_deref(f->f_inode);
	if (f->f_write) {
		nwriters -= 1;
		bsync();
	}
}


/*
 * bfs_remove()
 *	Remove an entry in the current directory
 */
void
bfs_remove(struct msg *m, struct file *f)
{
	struct inode *i;

	/*
	 * Have to be in root dir, and have permission
	 */
	if (f->f_inode->i_num != ROOTINODE) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	if (!f->f_write) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Look up entry.  Bail if no such file.
	 */
	if ((i = ino_lookup(m->m_buf)) == NULL) {
		msg_err(m->m_sender, ESRCH);
		return;
	}



	if (i->i_refs > 0) {
		/*
		 * If we have more than one active reference to the inode
		 * we report a busy error
		 */
		msg_err(m->m_sender, EBUSY);
		return;
	}

	/*
	 * Zap the blocks
	 */
	blk_trunc(i);

	/*
	 * Flag it as an inactive entry
	 */
	i->i_name[0] = '\0';

	/*
	 * Flag the dir entry as dirty, and finish up.  Update
	 * the affected blocks to minimize damage from a crash.
	 */
	ino_dirty(i);
	bsync();
	ino_clear(i);

	/*
	 * Return success
	 */
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
