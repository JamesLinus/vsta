/*
 * test8.c
 *	Test of DMA access
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
	char buf[64];

	printf("Child.\n");
	x = msg_connect(MYPORT, ACC_WRITE);
	printf("Child: connect returns %d\n", x);
	for (;;) {
		bzero(buf, sizeof(buf));
		m.m_op = FS_READ;
		m.m_nseg = 1;
		m.m_buf = buf;
		m.m_arg = m.m_buflen = sizeof(buf);
		m.m_arg1 = 0;
		y = msg_send(x, &m);
		printf("Send gets %d, buf '%s'\n", y, buf);
		gets(buf);
	}
}

main()
{
	int x, kbd, scrn;
	port_t port;
	struct msg m;
	int gen = 0;

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
	if (enable_dma() < 0) {
		perror("enable_dma");
		exit(1);
	}
	x = msg_receive(port, &m);
	printf("Parent, connect 0x%x retval %d\n", m.m_sender, x);
	msg_accept(m.m_sender);
	for (;;) {
		x = msg_receive(port, &m);
		printf("Parent, received %d nseg %d buf 0x%x\n",
			x, m.m_nseg, m.m_buf);
		sprintf(m.m_buf, "Hello, world #%d", gen++);
		m.m_nseg = m.m_buflen = m.m_arg1 = 0;
		m.m_arg = strlen(m.m_buf);
		msg_reply(m.m_sender, &m);
	}
}
