/*
 * ls.c
 *	A simple ls utility
 */
#include <stdio.h>
#include <dirent.h>

static int lflag = 0;

/*
 * setopt()
 *	Set option flag
 */
static void
setopt(char c)
{
	switch (c) {
	case 'l':
		lflag = 1;
	default:
		fprintf(stderr, "Unknown option: %c\n", c);
		exit(1);
	}
}

/*
 * ls()
 *	Do ls with current options on named place
 */
static void
ls(char *path)
{
	DIR *d;
	struct dirent *de;

	/*
	 * Open access to named place
	 */
	d = opendir(path);
	if (d == 0) {
		perror(path);
		return;
	}

	/*
	 * Read elements
	 */
	while (de = readdir(d)) {
		printf("%s\n", de->d_name);
	}
	closedir(d);
}

main(argc, argv)
	int argc;
	char **argv;
{
	int x;

	/*
	 * Parse leading args
	 */
	for (x = 1; x < argc; ++x) {
		char *p;

		if (argv[x][0] != '-') {
			break;
		}
		for (p = &argv[x][1]; *p; ++p) {
			setopt(*p);
		}
	}

	/*
	 * Do ls on rest of file or dirnames
	 */
	for (; x < argc; ++x) {
		ls(argv[x]);
	}

	return(0);
}
