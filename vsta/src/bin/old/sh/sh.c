/*
 * sh.c
 *	A very basic shell
 */
#include <sys/types.h>
#include <stdio.h>
#include <std.h>
#include <ctype.h>

static int dashc,	/* Flag -c command line */
	status;		/* Return status from command */

/*
 * getline()
 *	Get a line of arbitrary length
 *
 * Returns a malloc()'ed buffer which must be freed by the caller
 * when he's done looking at it.
 */
static char *
getline(void)
{
	char *buf = 0;
	int c, len;

	len = 0;
	while ((c = getchar()) != EOF) {
		char *p;

		if ((c == '\n') || (c == '\r')) {
			break;
		}
		len += 1;
		p = realloc(buf, len+1);
		if (p == 0) {
			free(buf);
			return(0);
		}
		buf = p;
		buf[len-1] = c;
	}
	if (buf) {
		buf[len] = '\0';
	}
	return(buf);
}

/*
 * explode()
 *	Return vectors to each word in given buffer
 *
 * Modifies buffer to null-terminate each "word"
 */
static char **
explode(char *buf)
{
	char **argv = 0, **arg2;
	int len = 0;

	for (;;) {
		/*
		 * Skip forward to next word
		 */
		while (isspace(*buf)) {
			++buf;
		}

		/*
		 * End of string?
		 */
		if (*buf == '\0') {
			break;
		}

		/*
		 * New word--add to vector
		 */
		len += 1;
		arg2 = realloc(argv, (len+1) * sizeof(char *));
		if (arg2 == 0) {
			free(argv);
			return(0);
		}
		argv = arg2;
		argv[len-1] = buf;

		/*
		 * Quoted string--assemble until closing quote
		 */
		if (*buf == '"') {
			argv[len-1] += 1;
			for (++buf; *buf && (*buf != '"'); ++buf) {
				/*
				 * Allow backslash-quoting within
				 * string (to embed quotes, usually)
				 */
				if (*buf == '\\') {
					if (buf[1]) {
						++buf;
					}
				}
			}
		} else {
			/*
			 * Walk to end of word
			 */
			while (*buf && !isspace(*buf)) {
				++buf;
			}
		}

		/*
		 * And null-terminate
		 */
		if (*buf) {
			*buf++ = '\0';
		}
	}

	/*
	 * Add null pointer to end of vectors
	 */
	if (argv) {
		argv[len] = 0;
	}
	return(argv);
}

/*
 * run()
 *	Fork and execvp() out to the executable
 */
static void
run(char **argv, int bg)
{
	pid_t pid;
	long l;
	int x;

	if (!dashc) {
		pid = fork();
		if (pid < 0) {
			perror("sh");
			return;
		}
	} else {
		pid = 0;
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		perror(argv[0]);
		exit(1);
	}
	if (!bg) {
		x = wait((long *)&l);
		printf("sh: pid %d status 0x%x\n", x, l);
	}
}

/*
 * builtin()
 *	Run built-ins
 *
 * Returns 1 if it was recognized & run; 0 otherwise
 */
static
builtin(char **argv)
{
	char *cmd = argv[0];
	int narg;

	for (narg = 0; argv[narg]; ++narg)
		;
	if (!strcmp(cmd, "path")) {
		if (narg != 2) {
			printf("Usage is: path <list>\n");
			status = 1;
			return(1);
		}
		setenv("PATH", argv[1]);
		return(1);
	} else if (!strcmp(cmd, "cd")) {
		if (narg != 2) {
			printf("Usage is: cd <path>\n");
			status = 1;
			return(1);
		}
		if (chdir(argv[1]) < 0) {
			status = 1;
			perror(argv[1]);
		}
		return(1);
	} else if (!strcmp(cmd, "set")) {
		if (narg != 3) {
			printf("Usage is: set <var> <value>\n");
			status = 1;
			return(1);
		}
		setenv(argv[1], argv[2]);
		return(1);
	} else if (!strcmp(cmd, "exit")) {
		if (narg > 1) {
			exit(atoi(argv[1]));
		} else {
			exit(0);
		}
		/*NOTREACHED*/
	} else {
		return(0);
	}
}

main(int argc, char **argv)
{
	char *line;
	int bg = 0;

	if ((argc > 2) && !strcmp(argv[1], "-c")) {
		dashc = 1;
	} else {
		dashc = 0;
	}
	for (;;) {
		if (!dashc) {
			printf("$ "); fflush(stdout);
			line = getline();
			if (line == 0) {
				clearerr(stdin);
				continue;
			}
		} else {
			line = argv[2];
		}
		argv = explode(line);
		for (argc = 0; argv[argc]; ++argc)
			;
		if (!strcmp(argv[argc-1], "&")) {
			bg = 1;
			argv[argc-1] = 0;
		}
		status = 0;
		if (builtin(argv)) {
			/* builtin() runs it */ ;
		} else {
			run(argv, bg);
		}
		if (dashc) {
			exit(status);
		}
		free(argv);
		free(line);
	}
}
