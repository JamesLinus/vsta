/*
 * strings.c
 *	Extract printable strings from binary stream
 */
#include <stdio.h>
#include <ctype.h>

static int err;

inline static int
printable(unsigned char c)
{
	return(!(c & 0x80) && (isprint(c) || isspace(c)));
}

static void
strings(char *file)
{
	FILE *fp;
	int c, inprint = 0, nprint = 0;
	const int RUNLEN = 4;
	char print[RUNLEN];

	fp = fopen(file, "rb");
	if (fp == NULL) {
		perror(file);
		err = 1;
		return;
	}
	while ((c = getc(fp)) != EOF) {
		if (inprint) {
			if (printable(c)) {
				putchar(c);
			} else {
				inprint = 0;
				putchar('\n');
			}
		/*
		 * When we see something printable, we wait to see at
		 * least four such in a row.  This avoids lots of noise
		 * where single bytes in a binary stream happen to have
		 * ASCII values.
		 */
		} else if (printable(c)) {
			print[nprint++] = c;
			if (nprint >= RUNLEN) {
				fwrite(print, 1, RUNLEN, stdout);
				nprint = 0;
				inprint = 1;
			}
		} else {
			nprint = 0;
		}
	}
	fclose(fp);
	if (inprint) {
		putchar('\n');
	}
}

main(int argc, char **argv)
{
	int x;

	for (x = 1; x < argc; ++x) {
		strings(argv[x]);
	}
	return(err);
}
