/*
 * Filename:	open.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 8th April 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Routines for opening, closing, creating  and deleting files
 *
 * Note that we don't allow subdirectories for bfs, which simplifies
 * things.
 */


#include <hash.h>
#include <std.h>
#include <stdio.h>
#include <sys/assert.h>
#include "bfs.h"


extern struct super *sblock;
static int nwriters = 0;	/* Number of writers active */
static struct hash *rename_pending = NULL;
				/* Tabulate pending renames */


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


/*
 * do_rename()
 *	Actually perform the file rename
 *
 * This is much simpler than most fs's as we simply change the source inode's
 * name field - we have no links or subdirectories to worry about!
 */
static char *
do_rename(char *src, char *dest)
{
	struct inode *isrc, *idest;
	
	/*
	 * Look up the entries
	 */
	if ((isrc = ino_lookup(src)) == NULL) {
		/*
		 * If we can't find the source file fail!
		 */
		return ESRCH;
	}
	if ((idest = ino_lookup(dest)) != NULL) {
		/*
		 * If the destination exists, remove the existing file
		 */
		blk_trunc(idest);
		idest->i_name[0] = '\0';
		ino_dirty(idest);
		ino_clear(idest);
	}
	strncpy(isrc->i_name, dest, BFSNAMELEN);
	isrc->i_name[BFSNAMELEN - 1] = '\0';
	ino_dirty(isrc);

	return NULL;
}


/*
 * bfs_rename()
 *	Rename a file
 */
void
bfs_rename(struct msg *m, struct file *f)
{
	struct file *f2;
	char *errstr;

	/*
	 * Sanity
	 */
	if ((m->m_arg1 == 0) || !valid_fname(m->m_buf, m->m_buflen)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * On first use, create the rename-pending hash
	 */
	if (rename_pending == 0) {
		rename_pending = hash_alloc(16);
		if (rename_pending == 0) {
			msg_err(m->m_sender, strerror());
			return;
		}
	}

	/*
	 * Phase 1--register the source of the rename
	 */
	if (m->m_arg == 0) {
		/*
		 * Transaction ID collision?
		 */
		if (hash_lookup(rename_pending, m->m_arg1)) {
			msg_err(m->m_sender, EBUSY);
			return;
		}

		/*
		 * Insert in hash
		 */
		if (hash_insert(rename_pending, m->m_arg1, f)) {
			msg_err(m->m_sender, strerror());
			return;
		}

		/*
		 * Flag open file as being involved in this
		 * pending operation.
		 */
		f->f_rename_id = m->m_arg1;
		f->f_rename_msg = *m;
		return;
	}

	/*
	 * Otherwise it's the completion
	 */
	f2 = hash_lookup(rename_pending, m->m_arg1);
	if (f2 == 0) {
		msg_err(m->m_sender, ESRCH);
		return;
	}
	(void)hash_delete(rename_pending, m->m_arg1);

	/*
	 * Do our magic
	 */
	errstr = do_rename(f2->f_rename_msg.m_buf, m->m_buf);
	if (errstr) {
		msg_err(m->m_sender, errstr);
		msg_err(f2->f_rename_msg.m_sender, errstr);
	} else {
		m->m_nseg = m->m_arg = m->m_arg1 = 0;
		msg_reply(m->m_sender, m);
		msg_reply(f2->f_rename_msg.m_sender, m);
	}

	/*
	 * Clear state
	 */
	f2->f_rename_id = 0;
}


/*
 * cancel_rename()
 *	Cancel an ongoing file rename
 */
void
cancel_rename(struct file *f)
{
	(void)hash_delete(rename_pending, f->f_rename_id);
	f->f_rename_id = 0;
}
