/*
 * rw.c
 *	Reads and writes to the parallel device
 */
#include <sys/msg.h>
#include <sys/assert.h>
#include <sys/seg.h>
#include <sys/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>
#include "par.h"
#include "par_port.h"

extern struct par_port printer;
extern int timeout;
extern char par_sysmsg[];

/*
 * wait_printer()
 *	Returns P_OK if printer can accept next character, or P_TIMEOUT
 *	if an error condition lasted longer than the timeout value.
 *	wait_printer() is the only place where error messages are logged
 *	to screen.
 *
 *	par_isready() is supposed to return P_TIMEOUT only
 *	when the printer buffer fills up. In this case we
 *	sleep for 100ms to let the printer catch up again.
 *	These 100ms should be short compared to the time a
 *	fast printer with a small buffer needs to catch up.
 */
static int
wait_printer(void)
{
	time_t tstart = time(NULL);
	int notified = 0;
	while (1) {
		switch (par_isready(&printer)) {
		case P_OK:
			if (notified) {
				syslog(LOG_INFO, "%s parallel port ok",
				       par_sysmsg);
			}
			return P_OK;
		case P_ERROR:
			if (!notified) {
				notified = 1;
				if (!printer.quiet) {
					syslog(LOG_WARNING, "%s %s",
					       par_sysmsg,
					       printer.last_error);
				}
			}
			break;
		case P_TIMEOUT:
			break;
		}
		if ((time(NULL) - tstart) > timeout) {
			if (!printer.quiet) {
				syslog(LOG_ERR, "%s parallel port timed out",
				       par_sysmsg);
			}
			return P_TIMEOUT;
		}
		__msleep(100);
	}
}

/*
 * par_write()
 *	Write bytes to port.
 *	NOTE: It is not checked, if the last byte was written
 *	successfully. I can't see how one should know if the
 *	error condition showed up *before* or *after* a character
 *	was written. An error condition can mean that the previous
 *	character could not be written as well as that further
 *	writes will fail (the latter being more likely).
 */
void
par_write(struct msg *m, struct file *fl)
{
	int bytes_written = 0;

	if (P_OK != wait_printer()) {
		msg_err(m->m_sender, EIO);
		return;
	}

	while (bytes_written < m->m_arg) {
		if (P_OK != wait_printer()) {
			m->m_nseg = m->m_arg1 = m->m_buflen = 0;
			m->m_arg = bytes_written;
			msg_reply(m->m_sender, m);
			return;
		}
		par_putc(&printer,
			 ((char *)(m->m_seg[0].s_buf))[bytes_written]);
		bytes_written++;
	}

	/*
	 * complete with success
	 */
	m->m_nseg = m->m_arg1 = m->m_buflen = 0;
	msg_reply(m->m_sender, m);
}
