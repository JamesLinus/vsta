/*
 * test6.c
 *	Test forking of threads
 *
 * Our use of stdio has to be done carefully; both threads share
 * the same address space, so they can harmfully race in their use
 * of the library routines.
 */
#include <sys/ports.h>
#include <sys/fs.h>
#include <std.h>

#define MYPORT 123

static void
child(void)
{
	int x, y;
	struct msg m;
	char buf[32];

	printf("Child.\n");
	x = msg_connect(MYPORT, ACC_WRITE);
	printf("Child: connect returns %d\n", x);
	for (;;) {
		m.m_op = FS_WRITE;
		m.m_nseg = m.m_buflen = m.m_arg = 0;
		y = msg_send(x, &m);
		printf("Send gets %d\n", y);
		gets(buf);
	}
}

main()
{
	int x, kbd, scrn;
	port_t port;
	struct msg m;

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	(void)__fd_alloc(scrn);
	printf("Create our own port\n");
	port = msg_port((port_name)MYPORT);
	printf("Got %d\nLaunch thread\n", port);
	x = tfork(child);
	if (x < 0) {
		perror("tfork");
		exit(1);
	}
	x = msg_receive(port, &m);
	printf("Parent, connect 0x%x retval %d\n", m.m_sender, x);
	msg_accept(m.m_sender);
	for (;;) {
		x = msg_receive(port, &m);
		printf("Parent, received %d\n", x);
		m.m_nseg = m.m_buflen = m.m_arg = 0;
		msg_reply(m.m_sender, &m);
	}
}
