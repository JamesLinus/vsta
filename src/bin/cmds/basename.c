/*
 * basename.c
 *	Print basename
 */
#include <stdio.h>
#include <string.h>

main(int argc, char **argv)
{
	int i;
	char *p;

	/*
	 * Usage
	 */
	if ((argc < 2) || (argc > 3)) {
		fprintf(stderr, "Usage is: %s <path> [ <suffix> ]\n",
			argv[0]);
		return(1);
	}

	/*
	 * Trim path
	 */
	p = strrchr(argv[1], '/');
	if (!p) {
		p = argv[1];
	} else {
		++p;
	}

	/*
	 * If there's a suffix to trim, match and trim as appropriate
	 */
	if (argc == 3) {
		int len = strlen(argv[2]), len2 = strlen(p);

		if ((len <= len2) && !strcmp(p + len2 - len, argv[2])) {
			fwrite(p, len2 - len, sizeof(char), stdout);
			putchar('\n');
			return(0);
		}
	}
	puts(p);
	return(0);
}
