/*
 * stat.c
 *	Implement stat operations on an open file
 */
#include <sys/fs.h>
#include <dos/dos.h>
#include <std.h>
#include <sys/param.h>

/*
 * inum()
 *	Synthesize an "inode" number for the node
 */
uint
inum(struct node *n)
{
	extern struct node *rootdir;

	/*
	 * Root dir--no cluster, just give a value of 0
	 */
	if (n == rootdir) {
		return(0);
	}

	/*
	 * Dir--use value of first cluster
	 */
	if (n->n_type == T_DIR) {
		return(n->n_clust->c_clust[0]);
	}

	/*
	 * File in root dir--again, no cluster for root dir.  Just
	 * use cluster value 1 (they start at 2, so this is available),
	 * and or in our slot.
	 */
	if (n->n_dir == rootdir) {
		return ((1 << 16) | n->n_slot);
	} else {
		/*
		 * Others--high 16 bits is cluster of file's directory,
		 * low 16 bits is our slot number.
		 */
		return ((n->n_dir->n_clust->c_clust[0] << 16) | n->n_slot);
	}
}

/*
 * isize()
 *	Calculate size of file from its "inode" information
 */
static uint
isize(struct node *n)
{
	extern uint dirents;

	if (n == rootdir) {
		return(sizeof(struct directory)*dirents);
	}
	return(n->n_clust->c_nclust*clsize);
}

/*
 * dos_stat()
 *	Build stat string for file, send back
 */
void
dos_stat(struct msg *m, struct file *f)
{
	char result[MAXSTAT];
	struct node *n = f->f_node;

	/*
	 * Directories
	 */
	if (n->n_type == T_DIR) {
		sprintf(result,
		 "perm=1/1\nacc=5/0/2\nsize=%d\ntype=d\nowner=0\ninode=%d\n",
			isize(n), inum(n));
	} else {
		/*
		 * Otherwise look up file and get dope
		 */
		sprintf(result,
		 "perm=1/1\nacc=5/0/2\nsize=%d\ntype=f\nowner=0\ninode=%d\n",
			n->n_len, inum(n));
	}
	m->m_buf = result;
	m->m_buflen = strlen(result);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dos_fid()
 *	Return ID for file
 */
void
dos_fid(struct msg *m, struct file *f)
{
	struct node *n = f->f_node;

	/*
	 * Only *files* get an ID (and thus can be mapped shared)
	 */
	if (n->n_type == T_DIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * arg is the inode value; arg1 is the size in pages
	 */
	m->m_arg = inum(n);
	m->m_arg1 = btop(isize(n));
	m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}
