/*
 * test.c
 *	Test code to drive the select server manually
 */
#include <stdio.h>
#include <sys/fs.h>
#include <fcntl.h>
#include <std.h>
#include <fdl.h>
#include "selfs.h"

static long clid;
static ulong key;

static void
reader(int dummy)
{
	int fd, x;
	struct select_complete sc;
	char *p;
	struct msg m;
	port_t port;

	/*
	 * Connect to the select server
	 */
	fd = open("//fs/select:client", 0);
	if (fd < 0) {
		perror("//fs/select:client");
		_exit(1);
	}
	port = __fd_port(fd);

	/*
	 * Extract our client ID and key for our server
	 */
	p = rstat(port, "clid");
	if (p == 0) {
		perror("rstat: clid");
		_exit(1);
	}
	clid = atoi(p);
	p = rstat(port, "key");
	if (p == 0) {
		perror("rstat: key");
		_exit(1);
	}
	key = atoi(p);

	/*
	 * Receive and display events
	 */
	for (;;) {
		m.m_op = FS_READ | M_READ;
		m.m_nseg = 1;
		m.m_buf = &sc;
		m.m_arg = m.m_buflen = sizeof(sc);
		m.m_arg1 = 10*1000;
		x = msg_send(port, &m);
		printf("Returned %d\n", x);
		if (x > 0) {
			printf(" index %u mask 0x%x count %lu\n",
				sc.sc_index, sc.sc_mask, sc.sc_iocount);
		}

		/*
		 * This lets us "get ahead" of the client
		 * request, if we want.
		 */
		sleep(2);
	}
	close(fd);
	_exit(1);
}

int
main(int argc, char **argv)
{
	int fd;
	int iocount = 1;

	tfork(reader, 0);
	fd = open("//fs/select:server", 0);
	if (fd < 0) {
		perror("//fs/select:server");
		exit(1);
	}
	printf("Hit return to send an event\n");
	for (;;) {
		char buf[80];
		struct select_event se;

		gets(buf);
		se.se_clid = clid;
		se.se_key = key;
		se.se_index = 123;
		se.se_mask = 0;
		se.se_iocount = iocount++;
		if (write(fd, &se, sizeof(se)) != sizeof(se)) {
			perror("write");
		}
	}
}
