/*
 * test7.c
 *	Test forking of processes
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

	printf("Child.\n");
	x = msg_connect(MYPORT, ACC_WRITE);
	printf("Child: connect returns %d\n", x);
	for (;;) {
		m.m_op = FS_WRITE;
		m.m_nseg = m.m_buflen = m.m_arg = 0;
		y = msg_send(x, &m);
		printf("Send gets %d\n", y);
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
	printf("Got %d\nLaunch process\n", port);
	x = fork();
	if (x < 0) {
		perror("fork");
		exit(1);
	}
	if (x == 0) {
		child();
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
