/*
 * Filename:	rmdir.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	24th May 1994
 * Last Update: 24th May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1 port)
 *
 * Description:	Utility to remove directories
 */
#include <stdio.h>
#include <stdlib.h>

/*
 * usage()
 *	When the user's tried something illegal we tell them what was valid
 */
static void
usage(void)
{
	fprintf(stderr, "Usage: rmdir <dir_name> ...\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int x, exitcode;

	/*
	* Scan for command line options
	*/
	if (argc == 1) {
		usage();
	}

	/*
	 * Remove directories
	 */
	exitcode = 0;
	for (x = 1; x < argc; x++) {
		if (rmdir(argv[x]) < 0) {
			perror(argv[x]);
			exitcode = 1;
		}
	}

	/*
	 * Return overall result
	 */
	return(exitcode);
}
