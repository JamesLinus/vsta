#include <sys/fs.h>
#include <stdio.h>

int 
main(void)
{
	port_t mouse;
	struct msg m;
	char *statstr;

	mouse = path_open("srv/mouse", ACC_READ);
	if (mouse < 0) {
		perror("srv/mouse");
		exit(1);
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
		statstr = rstat(mouse, NULL);
		printf("%d %d %d %3d %3d\n",
			atoi(__fieldval(statstr, "left")),
			atoi(__fieldval(statstr, "middle")),
			atoi(__fieldval(statstr, "right")),
			atoi(__fieldval(statstr, "x")),
			atoi(__fieldval(statstr, "y")));
	}
}
