/*
 * xargs.c
 *	Extract arguments and run command
 */
#include <stdio.h>
#include <std.h>

#define MAXARGS (512)

static char *args[MAXARGS];

int
main(int argc, char **argv)
{
	int base, x, eof = 0;

	/*
	 * Set up fixed arguments to command
	 */
	if (argc >= MAXARGS) {
		fprintf(stderr, "Too many arguments passed to %s\n", argv[0]);
		exit(1);
	}
	for (x = 1; x < argc; ++x) {
		args[x-1] = argv[x];
	}
	base = x-1;

	/*
	 * Read further arguments from stdin, until we max MAXARGS or
	 * reach EOF, at which point launch the command
	 */
	while (!eof) {
		/*
		 * Assemble lines into place
		 */
		for (x = base; x < MAXARGS; ++x) {
			char *p, buf[1024];

			/*
			 * Get next line, bail on EOF or error
			 */
			p = fgets(buf, sizeof(buf), stdin);
			if (p == 0) {
				eof = 1;
				break;
			}
			buf[strlen(buf)-1] = '\0';

			/*
			 * Duplicate string, assemble into args[]
			 */
			p = strdup(buf);
			if (p == 0) {
				perror(argv[0]);
				exit(1);
			}
			args[x] = p;
		}

		/* 
		 * If there were lines, run a command
		 */
		if (x > base) {
			pid_t pid;
			int st;

			/*
			 * NULL terminate
			 */
			args[x] = 0;

			/*
			 * Launch new process
			 */
			pid = fork();

			/*
			 * Child
			 */
			if (pid == 0) {
				execvp(args[0], args);
				perror(args[0]);
				_exit(1);
			}

			/*
			 * Failure
			 */
			if (pid < 0) {
				perror(argv[0]);
				exit(1);
			}

			/*
			 * Parent
			 */
			(void)wait(&st);
		}

		/*
		 * On EOF/error, all done
		 */
		if (eof) {
			break;
		}

		/*
		 * Free up memory for next time around
		 */
		for (x = base; (x < MAXARGS) && args[x]; ++x) {
			free(args[x]);
			args[x] = 0;
		}
	}
	return(0);
}
