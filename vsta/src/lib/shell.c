/*
 * shell.c
 *	Shell interface stuff
 */
#include <sys/wait.h>

/*
 * system()
 *	Launch a shell, return status
 */
system(char *cmd)
{
	char *argv[4];
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
		execv("/bin/sh", argv);
		_exit(-1);
	}
	waits(&w, 1);
	return(w.e_code);
}
