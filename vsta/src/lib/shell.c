/*
 * shell.c
 *	Shell interface stuff
 */
#include <sys/wait.h>
#include <std.h>

/*
 * system()
 *	Launch a shell, return status
 */
int
system(const char *cmd)
{
	const char *argv[4];
	long pid;
	struct exitst w;

	argv[0] = "sh";
	argv[1] = "-c";
	argv[2] = cmd;
	argv[3] = 0;
	pid = fork();
	if (pid < 0) {
		return(-1);
	}
	if (pid == 0) {
		execv("/vsta/bin/sh", argv);
		_exit(-1);
	}
	waits(&w, 1);
	return(w.e_code);
}

/*
 * execvp()
 *	Interface to execv() with path honored
 */
execvp(const char *prog, const char **argv)
{
	char *pathbuf, *path, *p, *buf;
	int len;

	/*
	 * Absolute doesn't use path
	 */
	if ((prog[0] == '/') || !strncmp(prog, "./", 2) ||
			!strncmp(prog, "../", 3)) {
		return(execv(prog, argv));
	}

	/*
	 * Try to find path from environment.  Just use program
	 * name as-is if we don't have a path.
	 */
	pathbuf = path = getenv("PATH");
	if (path == 0) {
		return(execv(prog, argv));
	}

	/*
	 * Try each element
	 */
	len = strlen(prog);
	do {
		/*
		 * Find next path element
		 */
		p = strchr(path, ';');
		if (p == 0) {
			p = strchr(path, ':');
		}
		if (p) {
			*p++ = '\0';
		}

		/*
		 * Get temp buffer for full path
		 */
		buf = malloc(len+strlen(path)+1);
		if (buf == 0) {
			return(-1);
		}
		sprintf(buf, "%s/%s", path, prog);
		(void)execv(buf, argv);

		/*
		 * Didn't fly; free buffer and iterate
		 */
		free(buf);
		path = p;
	} while (path);

	/*
	 * All failed.  Free malloc()'ed buffer of PATH, and return
	 * failure.
	 */
	free(pathbuf);
	return(-1);
}

/*
 * execlp()
 *	execl(), with path
 */
execlp(const char *path, const char *arg0, ...)
{
	return(execvp(path, &arg0));
}
