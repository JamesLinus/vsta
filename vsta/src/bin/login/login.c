/*
 * login.c
 *	A login program without an attitude
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <stdio.h>
#include <termios.h>
#include <passwd.h>
#include <fcntl.h>
#include <mnttab.h>
#include <string.h>

#define BANNER "/vsta/etc/banner"	/* Login banner path */
#define SYSMOUNT "/vsta/etc/fstab"	/* Global mounts */
#define MOUNTRC "mount.rc"		/* Per-user mounts */

/*
 * cat()
 *	Cat file if it exists
 */
static void
cat(char *path)
{
	int fd, x;
	char buf[128];

	if ((fd = open(path, O_READ)) < 0) {
		return;
	}
	while ((x = read(fd, buf, sizeof(buf))) > 0) {
		write(1, buf, x);
	}
	close(fd);
}

/*
 * get_str()
 *	Get a string, honor length, always null-terminate
 */
static void
get_str(char *buf, int buflen, int echo)
{
	int nstar[UI_STRLEN];
	int x, ret;
	char c;
	static char stars[] = "****";

	x = 0;
	buflen -= 1;	/* Leave room for terminating '\0' */
	for (;;) {
		/*
		 * Get next char.  If we lost access to the device,
		 * end the login attempt
		 */
		ret = read(0, &c, sizeof(c));
		if (ret < 0) {
			exit(1);
		}
		if (ret == 0) {
			sleep(1);
			buf[0] = '\0';
			return;
		}
		c &= 0x7f;

		/*
		 * Data--add to buf if room
		 */
		if ((c > ' ') && (c < 0x7f)) {
			if (x >= buflen) {
				continue;
			}
			buf[x] = c;
			if (echo) {
				write(1, &c, sizeof(c));
			} else {
				int y;

				/*
				 * Echo random number of *'s.  This gives
				 * positive feedback without necessarily
				 * telling a peeper how long the password
				 * is.
				 */
				y = (random() & 0x3)+1;
				write(1, stars, y);
				nstar[x] = y;
			}
			x += 1;
			continue;
		}

		/*
		 * End of line
		 */
		if ((c == '\n') || (c == '\r')) {
			write(1, "\r\n", 2);
			buf[x] = '\0';
			return;
		}

		/*
		 * Backspace
		 */
		if (c == '\b') {
			if (x > 0) {
				int y;

				x -= 1;
				y = (echo ? 1 : nstar[x]);
				while (y > 0) {
					write(1, "\b \b", 3);
					y -= 1;
				}
			}
			continue;
		}

		/*
		 * Erase line
		 */
		if ((c == '') || (c == '')) {
			x = 0;
			write(1, "\\\r\n", 3);
			continue;
		}

		/*
		 * Ignore other control chars
		 */
	}
}

/*
 * login()
 *	Log in as given account
 */
static void
login(struct uinfo *u)
{
	int x;
	port_t port;
	struct perm perm;
	char *p, buf[128];
	extern void zero_ids();

	/*
	 * Activate root abilities
	 */
	zero_ids(&perm, 1);
	perm.perm_len = 0;
	if (perm_ctl(1, &perm, (void *)0) < 0) {
		printf("login: can't enable root\n");
		exit(1);
	}

	/*
	 * Set our ID.  We skip slot 1, which is our superuser slot
	 * used to authorize the manipulation of all others.  Finish
	 * by setting 1--after this, we only hold the abilities of
	 * the user logging on.
	 */
	for (x = 0; x < PROCPERMS; ++x) {
		if (x == 1) {
			continue;
		}
		perm_ctl(x, &u->u_perms[x], (void *)0);
	}

	/*
	 * Initialize our environment.  Slot 0, our default ownership,
	 * is now set, so we will own the nodes which appear.
	 */
	setenv_init(u->u_env);

	/*
	 * Give up our powers
	 */
	perm_ctl(1, &u->u_perms[1], (void *)0);

	/*
	 * Re-initialize.  The /env server otherwise still believes
	 * we have vast privileges.
	 */
	setenv_init(u->u_env);

	/*
	 * Mount system default stuff.  Remove our root-capability
	 * entry from the mount table.
	 */
	port = mount_port("/");
	mount_init(SYSMOUNT);
	umount("/", port);

	/*
	 * Put some stuff into our environment.  We place it in the
	 * common part of our environment so all processes under this
	 * login will share it.
	 */
	sprintf(buf, "/%s/USER", u->u_env);
	setenv(buf, u->u_acct);
	sprintf(buf, "/%s/HOME", u->u_env);
	setenv(buf, u->u_home);

	/*
	 * If we can chdir to their home, set up their mount
	 * environment as requested.
	 */
	if (chdir(u->u_home) >= 0) {
		mount_init(MOUNTRC);
	} else {
		printf("Note: can not chdir to home: %s\n", u->u_home);
	}

	/*
	 * Create our own signal group
	 */
	(void)setsid();

	/*
	 * Launch their shell with a leading '-' in argv[0] to flag
	 * a login shell.
	 */
	p = strrchr(u->u_shell, '/');
	if (p && (strlen(p)+4 < sizeof(buf))) {
		sprintf(buf, "-%s", p+1);
		p = buf;
	} else {
		p = u->u_shell;
	}
	execl(u->u_shell, p, (char *)0);
	perror(u->u_shell);
	exit(1);
}

/*
 * do_login()
 *	First pass at getting login info
 */
static void
do_login(void)
{
	char acct[UI_STRLEN], passwd[UI_STRLEN], buf[UI_STRLEN];
	char *p;
	struct uinfo uinfo;

	for (;;) {
		printf("login: "); fflush(stdout);
		get_str(acct, sizeof(acct), 1);
		if (acct[0] != '\0') {
			break;
		}
	}
	if (getuinfo_name(acct, &uinfo)) {
		printf("Error; unknown account '%s'\n", acct);
		return;
	}
	if (!strcmp(uinfo.u_passwd, "*")) {
		printf("That account is disabled.\n");
		return;
	}
	if (uinfo.u_passwd[0]) {
		printf("password: "); fflush(stdout);
		get_str(passwd, sizeof(passwd), 0);
		if (strcmp(uinfo.u_passwd, passwd)) {
			printf("Incorrect password.\n");
			return;
		}
	}
	login(&uinfo);
}

/*
 * init_tty()
 *	Set up TTY for raw non-echo input
 */
static void
init_tty(void)
{
	struct termios t;

	tcgetattr(1, &t);
	t.c_lflag &= ~(ICANON|ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(1, TCSANOW, &t);
}

main(int argc, char **argv)
{
	/*
	 * Optionally let them specify device.  We access the
	 * device read/write, and attach it in this mode to
	 * stdin/stdout/stderr.  This lets scripts have an
	 * interactive FD no matter how I/O is redirected.
	 */
	if (argc > 1) {
		int x;
		port_t p;
		extern port_t path_open(char *, int);

		/*
		 * Access named server, get R/W access.
		 */
		for (x = 0; x <= 2; ++x) {
			close(x);
			p = path_open(argv[1], ACC_READ | ACC_WRITE);
			if (p < 0) {
				perror(argv[1]);
				exit(1);
			}
			__fd_alloc(p);
		}
	}

	srandom(time((long *)0));
	cat(BANNER);
	init_tty();
	for (;;) {
		do_login();
	}
}
