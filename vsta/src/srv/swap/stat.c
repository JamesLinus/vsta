/*
 * stat.c
 *	Implement stat operations on the swap device
 */
#include <swap/swap.h>
#include <sys/param.h>

extern ulong total_swap, free_swap;

/*
 * swap_stat()
 *	Build stat string for file, send back
 */
void
swap_stat(struct msg *m, struct file *f)
{
	char result[MAXSTAT];

	/*
	 * Root is hard-coded
	 */
	sprintf(result,
	 "perm=1/1\nacc=0/4/2\nsize=%ld\ntype=f\nowner=1/1\nfree=%ld\n",
		total_swap, free_swap);
	m->m_buf = result;
	m->m_arg = m->m_buflen = strlen(result);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
