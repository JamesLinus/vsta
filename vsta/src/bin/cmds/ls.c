/*
 * ls.c
 *	A simple ls utility
 */
#include <stdio.h>
#include <dirent.h>
#include <std.h>

static int ndir;	/* # dirs being displayed */
static int cols = 80;	/* Columns on display */

static int lflag = 0;	/* -l flag */

/*
 * prcols()
 *	Print array of strings in columnar fashion
 */
static void
prcols(char **v)
{
	int maxlen, x, col, entcol, nelem;

	/*
	 * Scan once to find longest string
	 */
	maxlen = 0;
	for (nelem = 0; v[nelem]; ++nelem) {
		x = strlen(v[nelem]);
		if (x > maxlen) {
			maxlen = x;
		}
	}

	/*
	 * Calculate how many columns that makes, and how many
	 * entries will end up in each column.
	 */
	col = cols / (maxlen+1);
	entcol = nelem/col;
	if (nelem % col) {
		entcol += 1;
	}

	/*
	 * Dump out the strings
	 */
	for (x = 0; x < entcol; ++x) {
		int y;

		for (y = 0; y < col; ++y) {
			int idx;

			idx = (y*entcol)+x;
			if (idx < nelem) {
				printf("%s", v[idx]);
			} else {
				/*
				 * No more out here, so finish
				 * inner loop now.
				 */
				putchar('\n');
				break;
			}

			/*
			 * Pad all but last column--put newline
			 * after last column.
			 */
			if (y < (col-1)) {
				int l;

				for (l = strlen(v[idx]); l <= maxlen; ++l) {
					putchar(' ');
				}
			} else {
				putchar('\n');
			}
		}
	}
}

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
 * ls_l()
 *	List stuff with full stats
 */
static void
ls_l(DIR *d)
{
	/* TBD */
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
	char **v = 0;
	int nelem;

	/*
	 * Prefix with name of dir if multiples
	 */
	if (ndir > 1) {
		printf("%s:\n", path);
	}

	/*
	 * Open access to named place
	 */
	d = opendir(path);
	if (d == 0) {
		perror(path);
		return;
	}

	/*
	 * Long format?
	 */
	if (lflag) {
		ls_l(d);
		closedir(d);
		return;
	}

	/*
	 * Read elements
	 */
	nelem = 0;
	while (de = readdir(d)) {
		nelem += 1;
		v = realloc(v, sizeof(char *)*(nelem+1));
		if (v == 0) {
			perror("ls");
			exit(1);
		}
		if ((v[nelem-1] = strdup(de->d_name)) == 0) {
			perror("ls");
			exit(1);
		}
	}
	closedir(d);

	/*
	 * Dump them (if any)
	 */
	if (nelem > 0) {
		int x;

		/*
		 * Put terminating null, then print
		 */
		v[nelem] = 0;
		prcols(v);

		/*
		 * Free memory
		 */
		for (x = 0; x < nelem; ++x) {
			free(v[x]);
		}
		free(v);
	}
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
	ndir = argc-x;
	for (; x < argc; ++x) {
		ls(argv[x]);
	}

	return(0);
}
