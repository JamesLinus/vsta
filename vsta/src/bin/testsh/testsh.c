/*
 * main.c
 *	Main routines for test shell
 */
#include <sys/types.h>
#include <sys/ports.h>
#include <sys/fs.h>
#include <stdio.h>
#include <ctype.h>
#include <std.h>
#include <fcntl.h>
#include <ctype.h>

extern char *__cwd;	/* Current working dir */
static void cd(), md(), quit(), ls(), pwd(), mount(), build();

/*
 * Table of commands
 */
struct {
	char *c_name;	/* Name of command */
	voidfun c_fn;	/* Function to process the command */
} cmdtab[] = {
	"cd", cd,
	"chdir", cd,
	"build", build,
	"exit", quit,
	"ls", ls,
	"md", md,
	"mkdir", md,
	"mount", mount,
	"pwd", pwd,
	"quit", quit,
	0, 0
};

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
 * build()
 *	Create a namer node
 */
static void
build(char *p)
{
	char *name, *val;
	int fd, x;

	if (!p || !p[0]) {
		printf("Usage: build <name> <val>\n");
		return;
	}
	name = p;
	p = strchr(p, ' ');
	if (!p) {
		printf("Missing value\n");
		return;
	}
	*p++ = '\0';
	val = p;
	while (isspace(*val)) {
		++val;
	}
	fd = open(name, O_WRITE|O_CREAT, 0);
	if (fd < 0) {
		perror(name);
		return;
	}
	x = write(fd, val, strlen(val));
	printf("Write of '%s' gives %d\n", val, x);
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
 * ls()
 *	Print contents of current directory
 */
static void
ls(void)
{
	int fd, x;
	char buf[256];

	fd = open(__cwd, O_READ);
	while ((x = read(fd, buf, sizeof(buf)-1)) > 0) {
		printf("%s", buf);
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
	char buf[128];
	int scrn, kbd;

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	(void)__fd_alloc(scrn);

	for (;;) {
		printf("%% "); fflush(stdout);
		gets(buf);
		do_cmd(buf);
	}
}
