/*
 * test.c
 *	Test code to drive semaphores manually
 */
#include <stdio.h>
#include <sys/fs.h>

static port_t p;

static void
send(int op)
{
	struct msg m;

	m.m_op = op;
	m.m_nseg = m.m_arg = m.m_arg1 = 0;
	if (msg_send(p, &m) < 0) {
		perror("send");
	}
}

main(int argc, char **argv)
{
	char buf[128];

	if (argc < 2) {
		printf("Usage is: %s <semid>\n", argv[0]);
		exit(1);
	}
	sprintf(buf, "fs/sema:%s", argv[1]);
	p = path_open(buf, ACC_READ | ACC_WRITE | ACC_CREATE);
	if (p < 0) {
		perror(buf);
		exit(1);
	}
	printf("Enter (r)ead, w(rite), v(release), or q(uit)\n");
	for (;;) {
		printf(">>"); fflush(stdout);
		gets(buf);
		switch (*buf) {
		case 'r':
			send(FS_READ);
			break;
		case 'w':
			send(FS_WRITE);
			break;
		case 'v':
			send(FS_SEEK);
			break;
		case 'q':
			exit(0);
		default:
			break;
		}
	}
}
