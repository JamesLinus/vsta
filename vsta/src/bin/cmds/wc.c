/*
 * wc.c
 *	Count words (and lines and characters)
 */
#include <stdio.h>
#include <ctype.h>

static int lflag, wflag, cflag;
static ulong ltotal, wtotal, ctotal;

static void
usage(char *prog)
{
	fprintf(stderr, "Usage is: %s [-lwc]\n", prog);
	exit(1);
}

/*
 * total()
 *	Display totals in format selected
 */
static void
total(ulong chars, ulong words, ulong lines)
{
	if (!lflag && !wflag && !cflag) {
		printf("%U %U %U\n", chars, words, lines);
	} else {
		if (cflag) {
			printf("%U", chars);
		}
		if (wflag) {
			printf("%s%U", cflag ? " " : "", words);
		}
		if (lflag) {
			printf("%s%U", (cflag || wflag) ? " " : "", lines);
		}
		printf("\n");
	}
}

/*
 * do_wc()
 *	Do actual wc operation
 */
static void
do_wc(FILE *fp)
{
	char *p, c, buf[512];
	ulong lines, words, chars;
	int inword;

	buf[sizeof(buf)-1] = '\0';
	lines = words = chars = 0;
	inword = 0;
	while (fgets(buf, sizeof(buf)-1, fp)) {
		for (p = buf; c = *p; ++p) {
			chars += 1;
			if (c == '\n') {
				lines += 1;
			}
			if (isspace(c)) {
				if (inword) {
					inword = 0;
					words += 1;
				}
			} else if (!inword) {
				inword = 1;
			}
		}
	}
	if (inword) {
		words += 1;
		lines += 1;
	}
	ltotal += lines;
	wtotal += words;
	ctotal += chars;
	total(chars, words, lines);
}

int
main(int argc, char **argv)
{
	int x;

	for (x = 1; x < argc; ++x) {
		if (argv[x][0] != '-') {
			int errs = 0;

			for ( ; x < argc; ++x) {
				FILE *fp;

				fp = fopen(argv[x], "r");
				if (fp == NULL) {
					perror(argv[x]);
					errs = 1;
				} else {
					printf("%s: ", argv[x]);
					do_wc(fp);
					fclose(fp);
				}
			}
			printf("total: ");
			total(ctotal, wtotal, ltotal);
			return(errs);
		}
		switch (argv[x][1]) {
		case 'l':
			lflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
	do_wc(stdin);
	return(0);
}
