/*
 * Filename:	stat.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 20th February 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Implement stat operations on an open file
 */


#include <std.h>
#include <stdio.h>
#include <sys/param.h>
#include "bfs.h"


extern struct super *sblock;


/*
 * bfs_stat()
 *	Build stat string for file, send back
 */
void
bfs_stat(struct msg *m, struct file *f)
{
	char result[MAXSTAT];
	struct inode *i;

	i = f->f_inode;
	sprintf(result,
	 	"perm=1/1\nacc=5/0/2\nsize=%d\ntype=%c\nowner=0\n" \
	 	"inode=%d\nctime=%u\nmtime=%u",
		i->i_fsize, (i->i_num == ROOTINODE) ? 'd' : 'f',
		i->i_num, i->i_ctime, i->i_mtime);
	sprintf(&result[strlen(result)],
		"\nstart blk=%d\nmgd blks=%d\ni_refs=%d\n",
		i->i_start, i->i_blocks, i->i_refs);
	sprintf(&result[strlen(result)],
		"prev=%d\nnext=%d\n",
		i->i_prev, i->i_next);

	m->m_buf = result;
	m->m_buflen = strlen(result);
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
