#include <sys/fs.h>
#include <fcntl.h>
#include <stdio.h>
#include <select.h>

/*
 * readit()
 *	Read mouse status, and display on stdout
 */
static void
readit(int mouse)
{
	char *statstr;

	statstr = fd_rstat(mouse, NULL);
	printf("%d %d %d %3d %3d\n",
		atoi(__fieldval(statstr, "left")),
		atoi(__fieldval(statstr, "middle")),
		atoi(__fieldval(statstr, "right")),
		atoi(__fieldval(statstr, "dx")),
		atoi(__fieldval(statstr, "dy")));
}

/*
 * test_select()
 *	Test operation of select()'ing for events
 */
static void
test_select(int mousefd)
{
	fd_set s;
	int ret;
	struct timeval to;

	for (;;) {
		FD_ZERO(&s); FD_SET(mousefd, &s);
		to.tv_sec = 2;
		to.tv_usec = 0;
		ret = select(mousefd+1, &s, NULL, NULL, &to);
		if (ret > 0) {
			printf("Mouse event\n");
			readit(mousefd);
			continue;
		}
		if (ret == 0) {
			printf("Timeout\n");
		}
		perror("select");
		sleep(1);
	}
}

int 
main(int argc, char **argv)
{
	int mousefd;
	port_t mouse;
	struct msg m;

	mousefd = open("//srv/mouse", O_RDONLY);
	if (mousefd < 0) {
		perror("//srv/mouse");
		exit(1);
	}
	mouse = __fd_port(mousefd);

	/*
	 * Test select() support
	 */
	if (argc > 1) {
		if (strcmp(argv[1], "-s")) {
			fprintf(stderr, "Unknown flag: %s\n", argv[1]);
			exit(1);
		}
		test_select(mousefd);
	}

	for (;;) {
		int ret;

		/*
		 * Ask to block until something happens on the mouse
		 */
		m.m_op = FS_READ | M_READ;
		m.m_arg = 1;
		m.m_arg1 = m.m_nseg = 0;
		ret = msg_send(mouse, &m);
		if (ret < 0) {
			perror("srv/mouse");
			exit(1);
		}

		/*
		 * Get status, and display
		 */
		readit(mousefd);
	}
}
