/*
 * namer.c
 *	Stubs for talking to namer
 *
 * The namer routines are not syscalls nor even central system
 * routines.  They merely encapsulate a message-based conversation
 * with a namer daemon.  It could be written as a set of normal
 * filesystem operations, except that we prefer to not assume the
 * organization of the user's name space.
 */
#include <sys/fs.h>
#include <sys/ports.h>
#include <std.h>

/*
 * namer_register()
 *	Register the given port number under the name
 */
namer_register(char *buf, port_t uport)
{
	char *p;
	char numbuf[8], wdbuf[64];
	int x;
	port_t port;
	struct msg m;

	/*
	 * Skip leading '/'
	 */
	while (*buf == '/') {
		++buf;
	}

	/*
	 * Connect to name server
	 */
	port = msg_connect(PORT_NAMER, ACC_READ|ACC_WRITE);
	if (port < 0) {
		return(-1);
	}

	/*
	 * Create the directories leading down to the name
	 */
	while (p = strchr(buf, '/')) {
		/*
		 * Null-terminate name
		 */
		bcopy(buf, wdbuf, p-buf);
		wdbuf[p-buf] = '\0';
		++p;

		/*
		 * Send a creation message
		 */
		m.m_op = FS_OPEN;
		m.m_buf = wdbuf;
		m.m_buflen = strlen(wdbuf)+1;
		m.m_nseg = 1;
		m.m_arg = ACC_CREATE|ACC_DIR|ACC_WRITE;
		m.m_arg1 = 0;
		if (msg_send(port, &m) < 0) {
			msg_disconnect(port);
			return(-1);
		}

		/*
		 * Advance to next element
		 */
		buf = p;
	}

	/*
	 * Final element--should be file in the current directory
	 */
	m.m_op = FS_OPEN;
	m.m_buf = buf;
	m.m_buflen = strlen(buf)+1;
	m.m_nseg = 1;
	m.m_arg = ACC_CREATE|ACC_WRITE;
	m.m_arg1 = 0;
	if (msg_send(port, &m) < 0) {
		msg_disconnect(port);
		return(-1);
	}

	/*
	 * Write our port number here
	 */
	sprintf(numbuf, "%d\n", uport);
	m.m_op = FS_WRITE;
	m.m_buf = numbuf;
	m.m_arg = m.m_buflen = strlen(numbuf);
	m.m_arg1 = 0;
	x = msg_send(port, &m);
	msg_disconnect(port);
	return(x);
}

/*
 * namer_find()
 *	Given name, look up a port
 */
port_name
namer_find(char *buf)
{
	char *p;
	int x;
	port_t port;
	struct msg m;
	char numbuf[8], wdbuf[64];

	/*
	 * Skip leading '/'
	 */
	while (*buf == '/') {
		++buf;
	}

	/*
	 * Connect to name server
	 */
	port = msg_connect(PORT_NAMER, ACC_READ);
	if (port < 0) {
		return(-1);
	}

	/*
	 * Search the directories leading down to the name
	 */
	do {
		p = strchr(buf, '/');
		if (p) {
			/*
			 * Null-terminate name
			 */
			bcopy(buf, wdbuf, p-buf);
			wdbuf[p-buf] = '\0';
			++p;
		} else {
			strcpy(wdbuf, buf);
		}

		/*
		 * Send an open message
		 */
		m.m_op = FS_OPEN;
		m.m_buf = wdbuf;
		m.m_buflen = strlen(wdbuf)+1;
		m.m_nseg = 1;
		m.m_arg = ACC_DIR;
		m.m_arg1 = 0;
		if (msg_send(port, &m) < 0) {
			msg_disconnect(port);
			return(-1);
		}

		/*
		 * Advance to next element
		 */
		buf = p;
	} while (p);

	/*
	 * Read our port number from here
	 */
	m.m_op = FS_READ|M_READ;
	m.m_buf = numbuf;
	m.m_arg = m.m_buflen = sizeof(numbuf)-1;
	m.m_nseg = 1;
	m.m_arg1 = 0;
	x = msg_send(port, &m);
	msg_disconnect(port);
	if (x < 0) {
		return(-1);
	}
	numbuf[x] = '\0';
	return(atoi(numbuf));
}
