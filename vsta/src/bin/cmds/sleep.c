/*
 * sleep.c
 *	Fall asleep for a bit
 */
#include <stdio.h>

main(int argc, char **argv)
{
	int secs;

	if (argc != 2) {
		fprintf(stderr, "Usage is: %s <seconds>\n", argv[0]);
		exit(1);
	}
	if (sscanf(argv[1], "%d", &secs) != 1) {
		fprintf(stderr, "Invalid number: %s\n", argv[1]);
		exit(1);
	}
	(void)sleep(secs);
	exit(0);
}
