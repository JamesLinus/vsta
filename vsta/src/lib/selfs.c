/*
 * selfs.c
 *	Supporting routines for a server to support select()
 */
#include <sys/fs.h>
#include <stdio.h>
#include <std.h>
#define _SELFS_INTERNAL
#include <select.h>
#include <selfs.h>

static port_t selfs_port = -1;	/* Share a port across all clients */

/*
 * parse()
 *	Explode select wstat message
 *
 * Returns 1 if there's a problem, 0 otherwise.
 * TBD: Extract hostname
 * Note that we mung data types here, so that we can use atoi/strchr
 *  for extraction purposes.  sscanf() would have been better, but this
 *  module is used by boot servers, who use a small subset of the full
 *  C library in order to keep their image size modest.
 */
static int
parse(char *buf, uint *maskp, long *clidp, int *fdp, ulong *keyp)
{
#define ADV() buf = strchr(buf, ','); \
		if (!buf) { return(1); } else { ++buf; }
	*maskp = atoi(buf);
	ADV();
	*clidp = atoi(buf);
	ADV();
	*fdp = atoi(buf);
	ADV();
	*keyp = atoi(buf);
	ADV();
	return(0);
#undef ADV
}

/*
 * sc_wstat()
 *	Handle FS_WSTAT of "select" and "unselect" messages
 *
 * Returns 0 if it's handled here, 1 if not.
 * NOTE: we "handle" it here even if it's an error for one of our
 *  message types.
 */
int
sc_wstat(struct msg *m, struct selclient *scp, char *field, char *val)
{
	if (!strcmp(field, "select")) {
		extern port_t path_open();

		if (scp->sc_mask) {
			msg_err(m->m_sender, EBUSY);
			return(0);
		}
		if (parse(val, &scp->sc_mask, &scp->sc_clid,
				&scp->sc_fd, &scp->sc_key) ||
				 (!scp->sc_mask)) {
			msg_err(m->m_sender, EINVAL);
			return(0);
		}

		/*
		 * Protect against any unsupported bits
		 */
		if (scp->sc_nosupp & scp->sc_mask) {
			scp->sc_mask = 0;
			msg_err(m->m_sender, ENOTSUP);
			return(0);
		}

		/*
		 * TBD: use hostname for clustered case
		 */
		if (selfs_port < 0) {
			selfs_port = path_open("//fs/select", 0);
		}
		if (selfs_port < 0) {
			scp->sc_mask = 0;
			msg_err(m->m_sender, strerror());
			return(0);
		}
		scp->sc_needsel = 1;
		scp->sc_iocount = 0;

	/*
	 * Client no longer uses select() on this connection
	 */
	} else if (!strcmp(field, "unselect")) {
		sc_done(scp);
	} else {
		/*
		 * Not a message *we* handle...
		 */
		return(1);
	}

	/*
	 * Success
	 */
	m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
	return(0);
}

/*
 * sc_done()
 *	All done with this client
 */
void
sc_done(struct selclient *scp)
{
	scp->sc_mask = 0;
	scp->sc_needsel = 0;
}

/*
 * sc_event()
 *	Send a select event to the select server
 */
void
sc_event(struct selclient *scp, uint event)
{
	struct select_event se;
	struct msg m;

	/*
	 * If we're seeing new interesting data since the client last did
	 * I/O, send a select event.  Inhibit if they already have
	 * received an appropriate select event, or if the event is not
	 * of interest to this client.
	 */
	if ((!scp->sc_needsel) || !(event & scp->sc_mask)) {
		return;
	}

	/*
	 * Fill in the event
	 */
	se.se_clid = scp->sc_clid;
	se.se_key = scp->sc_key;
	se.se_index = scp->sc_fd;
	se.se_mask = event;
	se.se_iocount = scp->sc_iocount;

	/*
	 * Send it to the server
	 */
	m.m_op = FS_WRITE;
	m.m_buf = &se;
	m.m_arg = m.m_buflen = sizeof(se);
	m.m_nseg = 1;
	m.m_arg1 = 0;
	(void)msg_send(selfs_port, &m);

	/*
	 * Ok, don't need to bother the client until
	 * they do an I/O for this data.
	 */
	scp->sc_needsel = 0;
}

/*
 * sc_init()
 *	Initialize fields
 *
 * Setting them all to zeroes would appear to suffice, for now
 */
void
sc_init(struct selclient *scp)
{
	bzero(scp, sizeof(*scp));
}
