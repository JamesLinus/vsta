/*
 * main.c
 *	Main routines for test shell
 */
#include <sys/types.h>
#ifdef STAND
#include <sys/ports.h>
#endif
#include <sys/fs.h>
#include <stdio.h>
#include <ctype.h>
#include <std.h>
#include <fcntl.h>
#include <ctype.h>

extern char *__cwd;	/* Current working dir */
static void cd(), md(), quit(), ls(), pwd(), mount(), cat(), sleep(),
	sec(), null(), run(), do_wstat(), do_fork(), get(), set(),
	do_umount();
extern void run(), path();
static char *buf;	/* Utility page buffer */

/*
 * Table of commands
 */
struct {
	char *c_name;	/* Name of command */
	voidfun c_fn;	/* Function to process the command */
} cmdtab[] = {
	"cat", cat,
	"cd", cd,
	"chdir", cd,
	"env", get,
	"exit", quit,
	"fork", do_fork,
	"get", get,
	"ls", ls,
	"md", md,
	"mkdir", md,
	"mount", mount,
	"null", null,
	"path", path,
	"pwd", pwd,
	"quit", quit,
	"run", run,
	"sector", sec,
	"set", set,
	"sleep", sleep,
	"umount", do_umount,
	"wstat", do_wstat,
	0, 0
};

/*
 * do_umount()
 *	Unmount all stuff at a point
 */
static void
do_umount(char *p)
{
	int x;

	if (!p || !p[0]) {
		printf("Usage: umount <point>\n");
		return;
	}
	x = umount(p, -1);
	printf("umount returns: %d\n", x);
}

/*
 * get()
 *	Get an environment variable
 */
static void
get(char *p)
{
	char *q;
	extern char *getenv();

	if (!p || !p[0]) {
		printf("Usage: get <var>\n");
		return;
	}
	q = getenv(p);
	if (!q) {
		printf("%s is not set\n", p);
	} else {
		printf("%s=%s\n", p, q);
	}
}

/*
 * set()
 *	Set an environment variable
 */
static void
set(char *p)
{
	char *val;

	if (!p || !p[0]) {
		printf("Usage: set <var> <value>\n");
		return;
	}
	val = strchr(p, ' ');
	if (!val) {
		printf("Missing value\n");
		return;
	}
	*val++ = '\0';
	setenv(p, val);
	printf("%s=%s\n", p, val);
}

/*
 * do_fork()
 *	Fork, and have the child exit immediately
 */
static void
do_fork(void)
{
	int x, y;

	x = fork();
	if (x < 0) {
		perror("fork");
		return;
	}
	if (x == 0) {
		_exit(0);
	}
	y = waits((void *)0);
	printf("Child: %d, return stat %d\n", x, y);
}

/*
 * do_wstat()
 *	Write a stat message
 */
static void
do_wstat(char *p)
{
	int fd;
	char *q;

	if (!p || !p[0]) {
		printf("Usage: wstat <file> <msg>\n");
		return;
	}
	q = strchr(p, ' ');
	if (!q) {
		printf("Missing message\n");
		return;
	}
	*q++ = '\0';
	fd = open(p, O_RDWR);
	if (fd < 0) {
		perror(p);
		return;
	}
	strcat(q, "\n");
	if (wstat(__fd_port(fd), q) < 0) {
		perror(q);
	}
	close(fd);
}

/*
 * sec()
 *	Dump a sector from a device
 */
static void
sec(char *p)
{
	uint secnum;
	char *secp;
	int fd, x;

	if (!p || !p[0]) {
		printf("Usage is: sector <file> <sector>\n");
		return;
	}

	/*
	 * Parse sector number, default to first sector
	 */
	secp = strchr(p, ' ');
	if (!secp) {
		secnum = 0;
	} else {
		*secp++ = '\0';
		secnum = atoi(secp);
	}

	/*
	 * Open device
	 */
	if ((fd = open(p, 0)) < 0) {
		perror(p);
		return;
	}

	/*
	 * Set file position if needed
	 */
	if (secnum > 0) {
		lseek(fd, 512L * secnum, 0);
	}

	/*
	 * Read a block
	 */
	x = read(fd, buf, 512);
	printf("Read sector %d returns %d\n", secnum, x);
	if (x < 0) {
		perror("read");
	} else {
		extern void dump_s();

		dump_s(buf, (x > 128) ? 128 : x);
	}
	close(fd);
}

/*
 * sleep()
 *	Pause the requested amount of time
 */
static void
sleep(char *p)
{
	struct time tm;

	if (!p || !p[0]) {
		printf("Usage: sleep <secs>\n");
		return;
	}
	time_get(&tm);
	printf("Time now: %d sec / %d usec\n", tm.t_sec, tm.t_usec);
	tm.t_sec += atoi(p);
	time_sleep(&tm);
	printf("Back from sleep\n");
}

/*
 * md()
 *	Make a directory
 */
static void
md(char *p)
{
	if (!p || !p[0]) {
		printf("Usage is: mkdir <path>\n");
		return;
	}
	while (isspace(*p)) {
		++p;
	}
	if (mkdir(p) < 0) {
		perror(p);
	}
}

/*
 * null()
 *	File I/O, quiet
 */
static void
null(char *p)
{
	int fd, x;

	if (!p || !p[0]) {
		printf("Usage: null <name>\n");
		return;
	}
	if (!p[0]) {
		printf("Missing filename\n");
		return;
	}
	fd = open(p, O_READ);
	if (fd < 0) {
		perror(p);
		return;
	}
	while ((x = read(fd, buf, NBPG)) > 0) {
		/* write(1, buf, x) */ ;
	}
	close(fd);
}

/*
 * cat()
 *	File I/O
 */
static void
cat(char *p)
{
	char *name;
	int fd, x, output = 0;

	if (!p || !p[0]) {
		printf("Usage: cat [>] <name>\n");
		return;
	}
	if (*p == '>') {
		output = 1;
		++p;
		while (isspace(*p)) {
			++p;
		}
	}
	if (!p[0]) {
		printf("Missing filename\n");
		return;
	}
	name = p;
	fd = open(name, output ? (O_WRITE|O_CREAT) : O_READ, 0);
	if (fd < 0) {
		perror(name);
		return;
	}
	if (output) {
		while ((x = read(0, buf, NBPG)) > 0) {
			if (buf[0] == '\n') {
				break;
			}
			write(fd, buf, x);
		}
		if (x < 0) {
			perror("read");
		}
	} else {
		while ((x = read(fd, buf, NBPG)) > 0) {
			write(1, buf, x);
		}
	}
	close(fd);
}

/*
 * mount()
 *	Mount the given port number in a slot
 */
static void
mount(char *p)
{
	int port, x;

	while (isspace(*p)) {
		++p;
	}
	port = atoi(p);
	port = msg_connect(port, ACC_READ);
	if (port < 0) {
		printf("Bad port.\n");
		return;
	}
	p = strchr(p, ' ');
	if (!p) {
		printf("Usage: mount <port> <path>\n");
		return;
	}
	while (isspace(*p)) {
		++p;
	}
	x = mountport(p, port);
	if (x < 0) {
		perror(p);
	}
}

/*
 * quit()
 *	Bye bye
 */
static void
quit(void)
{
	exit(0);
}

/*
 * cd()
 *	Change directory
 */
static void
cd(char *p)
{
	static char *home = 0;
	extern char *getenv();

	if (!p || !p[0]) {
		if (home == 0) {
			p = getenv("HOME");
			if (p == 0) {
				printf("No HOME set\n");
				return;
			}
			home = p;
		} else {
			p = home;
		}
	}
	while (isspace(*p)) {
		++p;
	}
	if (chdir(p) < 0) {
		perror(p);
	} else {
		printf("New dir: %s\n", __cwd);
	}
}

/*
 * pwd()
 *	Print current directory
 */
static void
pwd(void)
{
	printf("%s\n", __cwd);
}

/*
 * ls_l()
 *	Do long listing of an entry
 */
static void
ls_l(char *p)
{
	int fd, first;
	char buf[MAXSTAT];
	struct msg m;
	char *cp;

	fd = open(p, O_READ);
	if (fd < 0) {
		perror(p);
		return;
	}
	m.m_op = FS_STAT|M_READ;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = MAXSTAT;
	m.m_nseg = 1;
	m.m_arg1 = 0;
	if (msg_send(__fd_port(fd), &m) <= 0) {
		printf("%s: stat failed\n", p);
		close(fd);
		return;
	}
	close(fd);
	printf("%s: ", p);
	cp = buf;
	first = 1;
	while (p = strchr(cp, '\n')) {
		*p++ = '\0';
		if (first) {
			first = 0;
		} else {
			printf(", ");
		}
		printf("%s", cp);
		cp = p;
	}
	printf("\n");
}

/*
 * ls()
 *	Print contents of current directory
 */
static void
ls(char *p)
{
	int fd, x, l = 0;
	char buf[256];

	/*
	 * Only -l supported
	 */
	if (p && p[0]) {
		if (strcmp(p, "-l")) {
			printf("Usage: ls [-l]\n");
			return;
		}
		l = 1;
	}

	/*
	 * Open current dir
	 */
	fd = open(__cwd, O_READ);
	if (fd < 0) {
		perror(__cwd);
		return;
	}
	while ((x = read(fd, buf, sizeof(buf)-1)) > 0) {
		char *cp;

		buf[x] = '\0';
		if (!l) {
			printf("%s", buf);
			continue;
		}
		cp = buf;
		while (p = strchr(cp, '\n')) {
			*p++ = '\0';
			ls_l(cp);
			cp = p;
		}
	}
	close(fd);
}

/*
 * do_cmd()
 *	Given command string, look up and invoke handler
 */
static void
do_cmd(str)
	char *str;
{
	int x, len, matches = 0, match;
	char *p;

	p = strchr(str, ' ');
	if (p) {
		len = p-str;
	} else {
		len = strlen(str);
	}

	for (x = 0; cmdtab[x].c_name; ++x) {
		if (!strncmp(cmdtab[x].c_name, str, len)) {
			++matches;
			match = x;
		}
	}
	if (matches == 0) {
		printf("No such command\n");
		return;
	}
	if (matches > 1) {
		printf("Ambiguous\n");
		return;
	}
	(*cmdtab[match].c_fn)(p ? p+1 : p);
}

/*
 * main()
 *	Get started
 */
main(void)
{
#ifdef STAND
	int scrn, kbd;

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	(void)__fd_alloc(scrn);
#endif

	buf = malloc(NBPG);
	if (buf == 0) {
		perror("testsh buffer");
		exit(1);
	}

	for (;;) {
		printf("%% "); fflush(stdout);
		gets(buf);
		do_cmd(buf);
	}
}
