/*
 * port.c
 *	Utilities for managing access to ports
 */
#include <sys/types.h>
#include <sys/ports.h>
#include <sys/fs.h>
#include <std.h>

/*
 * For mapping well-known-addresses into their port names
 */
static struct map {
	char *m_name;
	port_name m_addr;
} names[] = {
	{"NAMER", PORT_NAMER},
	{"TIMER", PORT_TIMER},
	{"ENV", PORT_ENV},
	{"CONS", PORT_CONS},
	{"SWAP", PORT_SWAP},
	{(char *)0, 0}
};

/*
 * path_open()
 *	Open named path
 *
 * String is either a namer name "foo/bar", or a named server plus
 * a path to walk within "tty/cons:0".
 */
port_t
path_open(char *path_ro, int mode)
{
	port_name pn;
	port_t p;
	char *path, *path2;

	/*
	 * Get a RW copy of the path
	 */
	path = alloca(strlen(path_ro)+1);
	if (path == 0) {
		return(-1);
	}
	strcpy(path, path_ro);

	/*
	 * If there's a path after the portname, split it off
	 */
	path2 = strchr(path, ':');
	if (path2) {
		*path2++ = '\0';
	}

	/*
	 * Numeric are used as-is
	 */
	if (isdigit(path[0])) {
		pn = atoi(path);

	/*
	 * Upper are well-known only
	 */
	} else if (isupper(path[0])) {
		int x;

		for (x = 0; names[x].m_name; ++x) {
			if (!strcmp(names[x].m_name, path)) {
				break;
			}
		}
		if (names[x].m_name == 0) {
			return(-1);
		}
		pn = names[x].m_addr;
	} else {
		/*
		 * Look up via namer for others
		 */
		pn = namer_find(path);
		if (pn < 0) {
			return(-1);
		}
	}

	/*
	 * Connect
	 */
	p = msg_connect(pn, path2 ? ACC_READ : mode);
	if (p < 0) {
		return(-1);
	}

	/*
	 * If there's a path within, walk it now
	 */
	if (path2) {
		struct msg m;
		char *q;

		do {
			q = strchr(path2, '/');
			if (q) {
				*q++ = '\0';
			}
			m.m_op = FS_OPEN;
			m.m_nseg = 1;
			m.m_buf = path2;
			m.m_buflen = strlen(path2)+1;
			m.m_arg = (q ? ACC_READ : mode);
			m.m_arg1 = 0;
			if (msg_send(p, &m) < 0) {
				msg_disconnect(p);
				return(-1);
			}
			path2 = q;
		} while (path2);
	}
	return(p);
}
