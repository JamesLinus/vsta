/*
 * run.c
 *	Routines for running an external program
 */
#include <std.h>
#include <stdio.h>
#include <sys/wait.h>

static char *curpath = NULL;

/*
 * execvp()
 *	Exec using search path
 *
 * No return on success; returns -1 on failure to run.
 */
static
execvp(char *prog, char **argv)
{
	char *p, *q;
	char path[128];

	/*
	 * If has leading '/' or './' or '../' then use path as-is
	 */
	if ((prog[0] == '/') || !strncmp(prog, "./", 2) ||
			!strncmp(prog, "../", 3)) {
		return(execv(path, argv));
	}

	/*
	 * Otherwise try each prefix in current path and try to
	 * find an executable.
	 */
	p = curpath;
	while (p) {
		/*
		 * Find next path element seperator, copy into place
		 */
		q = strchr(p, ':');
		if (q) {
			bcopy(p, path, q-p);
			path[q-p] = '\0';
			++q;
		} else {
			strcpy(path, p);
		}
		sprintf(path+strlen(path), "/%s", prog);

		/*
		 * Try to run
		 */
		execv(path, argv);

		/*
		 * Advance to next element or end
		 */
		p = q;
	}
	return(-1);
}

/*
 * run()
 *	Fire up an executable
 */
void
run(char *p)
{
	char *q, **argv;
	int x = 1, bg = 0;
	int pid;
	char buf[128];
	struct exitst e;

	if (!p || !p[0]) {
		printf("Usage: run <file>\n");
		return;
	}

	/*
	 * Chop up into arguments
	 */
	if (q = strchr(p, ' ')) {
		*q++ = '\0';
	}
	argv = malloc((x+1) * sizeof(char **));
	argv[0] = p;
	while (q) {
		x += 1;
		argv = realloc(argv, (x+1) * sizeof(char **));
		argv[x-1] = q;
		if (q = strchr(q, ' ')) {
			*q++ = '\0';
		}
	}

	/*
	 * Trailing &--run in background
	 */
	if (!strcmp(argv[x-1], "&")) {
		bg = 1;
		x -= 1;
	}

	/*
	 * Null pointer terminates
	 */
	argv[x] = 0;

	/*
	 * Launch child
	 */
	pid = fork();
	if (pid == 0) {
		x = execvp(p, argv);
		perror(p);
		printf("Error code: %d\n", x);
	}
	if (bg) {
		printf("%d &\n", pid);
	} else {
		x = waits(&e);
		printf("pid %d status %d user %d system %d\n",
			pid, e.e_code, e.e_usr, e.e_sys);
	}
	free(argv);
}

/*
 * path()
 *	Set search path for executable
 */
void
path(char *p)
{
	if (!p || !p[0]) {
		printf("Path: %s\n", curpath ? curpath : "not set");
		return;
	}
	if (curpath) {
		free(curpath);
	}
	curpath = strdup(p);
}
