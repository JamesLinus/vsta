#include <sys/ports.h>
#include <sys/fs.h>

main()
{
	int x;
	static char msg[] = "Hello, world.\n";
	struct msg m;

	x = msg_connect(PORT_CONS, ACC_WRITE);
	for (;;) {
		m.m_op = FS_WRITE;
		m.m_nseg = 1;
		m.m_buf = msg;
		m.m_arg = m.m_buflen = strlen(msg);
		m.m_arg1 = 0;
		msg_send(x, &m);
	}
}
