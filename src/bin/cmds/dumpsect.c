/*
 * Filename:	dumpsect.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	6th March 1994
 * Last Update: 14th March 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description:	Utility to read a file sector.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * usage()
 *	When the user's tried something illegal we tell them what was valid
 */
static void
usage(char *util_name)
{
	fprintf(stderr,
		"Usage: %s <file_name> <sector_number> [ <nsects> ]\n",
		util_name);
	exit(1);
}

/*
 * print_stat()
 *	Print the stat field of the specified file, returning 0 for success
 *
 * We do a few checks, and only display data that was actually read - in other
 * words we take some notice of EOF markers
 */
static int
print_sect(char *name, int sect)
{
	FILE *fp;
	char dat[512];
	int x;

	/*
	* Open the specified file
	*/
	fp = fopen(name, "rb");
	if (fp == NULL) {
		perror(name);
		return 1;
	}

	/*
	* Position at the appropriate sector
	*/
	fseek(fp, sect * 512, SEEK_SET);
	x = fread(dat, 1, 512, fp); 
	fclose(fp);

	/*
	* Display the sector contents
	*/
	if (x > 0) {
		dump_s(dat, x);
	}
	return(0);
}

/*
 * main()
 *	Sounds like a good place to start things :-)
 */
int
main(int argc, char **argv)
{
	int exit_code = 0;
	int x, nsec, loc;

	/*
	* Do we have anything that looks vaguely reasonable
	*/
	if (argc < 3) {
		usage(argv[0]);
	}

	if (!strncmp(argv[2], "0x", 2)) {
		(void)sscanf(argv[2]+2, "%x", &loc);
	} else {
		(void)sscanf(argv[2], "%d", &loc);
	}
	if (argc > 3) {
		(void)sscanf(argv[3], "%d", &nsec);
	} else {
		nsec = 1;
	}
	for (x = 0; x < nsec; ++x,++loc) {
		if (nsec > 1) {
			if (x > 0) {
				printf("\n");
			}
			printf("Sector %d(0x%x):\n", loc, loc);
		}
		exit_code = print_sect(argv[1], loc);
	}

	return(exit_code);
}
