/*
 * kill.c
 *	Kill utility, delivers events via proc filesystem
 */
#include "ps.h"
#include <stdio.h>

int
main(int argc, char **argv)
{
	int x, pid, fd, n;
	char *event = EKILL;
	uint elen = strlen(event);
	char path[32];

	/*
	 * Usage
	 */
	if (argc < 2) {
		fprintf(stderr,
			"Usage is: %s [-event] <pid> [ <pid>...]\n",
			argv[0]);
		exit(1);
	}

	/*
	 * Get access to /proc
	 */
	mount_procfs();

	/*
	 * Walk list of events and PIDs, delivering last event to
	 * last PID
	 */
	for (x = 1; x < argc; ++x) {
		if (argv[x][0] == '-') {
			event = argv[x]+1;
			elen = strlen(event);
		} else {
			pid = atoi(argv[1]);
			sprintf(path, "/proc/%d/note", pid);
			fd = open(path, O_WRONLY);
			if (fd == -1) {
				perror(path);
				exit(1);
			}
			n = write(fd, event, elen);
			if (n == -1) {
				perror(path);
				exit(1);
			}
		}
	}
	return(0);
}
