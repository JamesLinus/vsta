/*
 * echo.c
 *	Echo arguments back
 */
#include <stdio.h>

int
main(int argc, char **argv)
{
	int x, nflag = 0;

	if (strcmp(argv[1], "-n") == 0) {
		nflag = 1;
		argv += 1;
		argc -= 1;
	}
	for (x = 1; x < argc; ++x) {
		if (x > 1) {
			printf(" %s", argv[x]);
		} else {
			printf("%s", argv[x]);
		}
	}
	if (!nflag) {
		putchar('\n');
	}
	return(0);
}
