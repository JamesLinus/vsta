/*
 * stat.c
 *	Implement stat operations on an open file
 */
#include <bfs/bfs.h>
#include <sys/param.h>

extern char *strerror();

/*
 * bfs_stat()
 *	Build stat string for file, send back
 */
void
bfs_stat(struct msg *m, struct file *f)
{
	char result[MAXSTAT];
	struct dirent d;

	/*
	 * Root is hard-coded
	 */
	if (f->f_inode == ROOTINO) {
		sprintf(result,
		 "perm=1/1\nacc=5/0/2\nsize=%d\ntype=d\nowner=1/1\ninode=-1\n",
			NDIRBLOCKS*BLOCKSIZE);
	} else {
		/*
		 * Otherwise look up file and get dope
		 */
		if (dir_copy(f->f_inode->i_num, &d)) {
			msg_err(m->m_sender, strerror());
			return;
		}
		sprintf(result,
		 "perm=1/1\nacc=5/0/2\nsize=%d\ntype=f\nowner=1/1\ninode=%d\n",
			d.d_len, d.d_inum);
	}
	m->m_buf = result;
	m->m_buflen = strlen(result);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
