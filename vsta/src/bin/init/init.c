/*
 * init.c
 *	Do initial program fire-up
 *
 * This includes mounting default filesystems and then working
 * out way through the init table, running stuff as needed.
 *
 * We go out of our way to read all input in units of lines; our
 * C library "get line" routines understand both VSTa (\n) and
 * DOS (\r\n) end-of-line conventions, and map to just '\n' for us.
 */
#include <sys/types.h>
#include <sys/ports.h>
#include <sys/fs.h>
#include <stdio.h>
#include <std.h>
#include <namer/namer.h>

#define INITTAB "/vsta/etc/inittab"	/* Table of stuff to run */
#define FSTAB "/vsta/etc/fstab"		/* Filesystems at boot */

/*
 * mount_fs()
 *	Read initial mount from fstab, put in our mount table
 */
static void
mount_fs(void)
{
	FILE *fp;
	char *r, buf[80], *point, *path;
	port_t p;
	port_name pn;
	int nmount = 0;

	if ((fp = fopen(FSTAB, "r")) == NULL) {
		return;
	}
	while (fgets(buf, sizeof(buf)-1, fp)) {
		/*
		 * Get null-terminated string
		 */
		buf[strlen(buf)-1] = '\0';
		if ((buf[0] == '\0') || (buf[0] == '#')) {
			continue;
		}

		/*
		 * Break into two parts
		 */
		point = strchr(buf, ' ');
		if (point == NULL) {
			continue;
		}
		++point;

		/*
		 * See if we want to walk down into the port
		 * before mounting.
		 */
		path = strchr(buf, ':');
		if (path) {
			*path++ = '\0';
		}

		/*
		 * Look up via namer
		 */
		pn = namer_find(buf);
		if (pn < 0) {
			printf("init: can't find: %s\n", buf);
			continue;
		}
		p = msg_connect(pn, ACC_READ);
		if (p < 0) {
			printf("init: can't connect to: %s\n", buf);
		}

		/*
		 * If there's a path within, walk it now
		 */
		if (path) {
			struct msg m;
			char *q;

			do {
				q = strchr(path, '/');
				if (q) {
					*q++ = '\0';
				}
				m.m_op = FS_OPEN;
				m.m_nseg = 1;
				m.m_buf = path;
				m.m_buflen = strlen(path)+1;
				m.m_arg = ACC_READ;
				m.m_arg1 = 0;
				if (msg_send(p, &m) < 0) {
					printf("Bad path under %s: %s\n",
						buf, path);
					msg_disconnect(p);
					continue;
				}
				path = q;
			} while (path);
		}

		/*
		 * Mount port in its place
		 */
		mountport(point, p);
		if (nmount++ == 0) {
			printf("Mounting:");
		}
		printf(" %s", point); fflush(stdout);
	}
	fclose(fp);
	if (nmount > 0) {
		printf("\n");
	}
}

main(void)
{
	port_t p;
	port_name pn;

	/*
	 * A moment (1.5 sec) to let servers establish their ports
	 */
	__msleep(2500);

	/*
	 * Connect to console display and keyboard
	 */
	p = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(p);
	p = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(p);
	(void)__fd_alloc(p);

	/*
	 * Mount /namer, /time and /env in their accustomed places
	 */
	p = msg_connect(PORT_NAMER, ACC_READ);
	if (p >= 0) {
		mountport("/namer", p);
	}
	p = msg_connect(PORT_ENV, ACC_READ);
	if (p >= 0) {
		mountport("/env", p);
	}
	p = msg_connect(PORT_TIMER, ACC_READ);
	if (p >= 0) {
		mountport("/time", p);
	}

	/*
	 * Root filesystem
	 */
	pn = namer_find("fs/root");
	if (pn < 0) {
		printf("init: can't find root\n");
		exit(1);
	}
	p = msg_connect(pn, ACC_READ);
	if (p < 0) {
		printf("init: can't connect to root\n");
		exit(1);
	}
	mountport("/vsta", p);

	/*
	 * Initialize environment
	 */
	setenv_init("");

	/*
	 * Mount others
	 */
	mount_fs();

	/*
	 * Launch login XXX
	 */
	execl("/vsta/bin/login", "login", (char *)0);
	perror("login");
	exit(1);
}
