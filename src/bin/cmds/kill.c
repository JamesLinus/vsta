/*
 * kill.c
 *	Send events to processes, process groups, or threads
 *
 * Can be used to kill a sequence of processes, for instance:
 *	kill -t 123 -p 456 -p 9 -g -p 10
 * Which would kill thread 123 within process 456, and then kill the
 * whole process with PID 9, and then kill the process group containing
 * PID 10.
 *
 * And, as always, you can finish with a plain old list of PID's.
 *
 */
#include <sys/fs.h>
#include <sys/syscall.h>
#include <std.h>
#include <stdio.h>
#include <getopt.h>

static char *progname;

static void
usage(void)
{
	fprintf(stderr, "Usage is: %s [-t <thread id>] [-g] "
		"[-e <event>] -p <pid> [...]\n", progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	pid_t tid = 0;
	int x, pgrp = 0;
	char *event = EKILL;

	progname = argv[0];
	while ((x = getopt(argc, argv, "gt:e:p:")) > 0) {
		switch (x) {
		case 'g':
			pgrp = 1;
			break;
		case 't':
			tid = atoi(optarg);
			break;
		case 'e':
			event = optarg;
			break;
		case 'p':
			notify(atoi(optarg), pgrp ? -1 : tid, event);
			pgrp = 0;
			tid = 0;
			break;
		default:
			usage();
			break;
		}
	}

	/*
	 * Take a trailing list of PID's
	 */
	tid = 0;
	for (x = optind; x < argc; ++x) {
		notify(atoi(argv[x]), pgrp ? -1 : 0, event);
	}

	return(0);
}
