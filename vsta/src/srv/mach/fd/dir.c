/*
 * dir.c
 *	Do readdir() operation
 */
#include <sys/fs.h>
#include <fd/fd.h>

extern void *malloc();

extern struct floppy floppies[];

/*
 * fd_readdir()
 *	Fill in buffer with list of supported names
 */
void
fd_readdir(struct msg *m, struct file *f)
{
	int nfd, x, entries;
	char *buf, *p;

	/* 
	 * Count up number of floppy units configured
	 */
	for (x = f->f_pos, nfd = 0; x < NFD; ++x) {
		if (floppies[x].f_state == F_NXIO)
			continue;
		++nfd;
	}

	/*
	 * Calculate # of entries which will fit.  4 is the chars
	 * in "fdX\n".
	 */
	entries = m->m_arg/4;

	/*
	 * Take smaller of available and fit
	 */
	if (nfd > entries)
		nfd = entries;

	/*
	 * Get a temp buffer to build into
	 */
	if ((buf = malloc(entries*4+1)) == 0) {
		msg_err(f->f_sender, ENOMEM);
		return;
	}

	/*
	 * Assemble entries
	 */
	for (p = buf, x = f->f_pos; (x < NFD) && (nfd > 0); ++x) {
		if (floppies[x].f_state == F_NXIO)
			continue;
		sprintf(p, "fd%d\n", x);
		p += 4;
	}

	/*
	 * Send results
	 */
	m->m_buf = buf;
	m->m_buflen = p-buf;
	m->m_nseg = 1;
	m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
	free(buf);
}

/*
 * fd_open()
 *	Move from root dir down into a device
 */
void
fd_open(struct msg *m, struct file *f)
{
	struct floppy *fl;

	if (f->f_unit != ROOTDIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	if ((strlen(m->m_buf) == 3) && !strncmp(m->m_buf, "fd", 2)) {
		char c = ((char *)(m->m_buf))[2];

		if ((c >= '0') && (c < ('0'+NFD))) {
			fl = &floppies[c-'0'];
			if (fl->f_state != F_NXIO) {
				f->f_unit = fl->f_unit;
				fl->f_opencnt += 1;
				m->m_buflen = m->m_nseg =
					m->m_arg1 = m->m_arg = 0;
				msg_reply(m->m_sender, m);
				return;
			}
		}
	}
	msg_err(m->m_sender, ESRCH);
}
