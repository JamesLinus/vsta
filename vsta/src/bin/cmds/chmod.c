/*
 * chmod.c
 *	Change mode of files, POSIX style
 */
#include <stdio.h>

int
main(int argc, char **argv)
{
	char c;
	int mode, x, errs = 0;

	if (argc < 3) {
		fprintf(stderr,
			"Usage is: %s <mode> <file> [<file>...]\n",
			argv[0]);
		exit(1);
	}

	/*
	 * An octal number?  Straight numeric mode.
	 */
	c = argv[1][0];
	if ((c >= '0') && (c <= '7')) {
		if (sscanf(argv[1], "%o", &mode) != 1) {
			fprintf(stderr, "Bad octal value: %s\n", argv[1]);
			exit(1);
		}
	} else {
		fprintf(stderr, "Symbolic mode not supported yet\n");
		exit(1);
	}

	/*
	 * Assign the values
	 */
	for (x = 2; x < argc; ++x) {
		if (chmod(argv[x], mode) < 0) {
			errs = 1;
			perror(argv[x]);
		}
	}

	return(errs);
}
