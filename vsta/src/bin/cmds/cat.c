/*
 * cat.c
 *
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
 */
#include <stdio.h>
#include <std.h>
#include <ctype.h>
#include <fcntl.h>
#include <stat.h>

static int bflag, eflag, nflag, sflag, tflag, vflag;
static int rval;
static char *filename;

static void cook_args(), cook_buf(), raw_args(), raw_cat();

int
main(int argc, char **argv)
{
	extern int optind;
	int ch;

	while ((ch = getopt(argc, argv, "benstuv")) != EOF)
		switch (ch) {
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = vflag = 1;	/* -t implies -v */
			break;
		case 'u':
			setbuf(stdout, (char *)NULL);
			break;
		case 'v':
			vflag = 1;
			break;
		case '?':
			(void)fprintf(stderr,
			    "usage: cat [-benstuv] [-] [file ...]\n");
			exit(1);
		}
	argv += optind;

	if (bflag || eflag || nflag || sflag || tflag || vflag) {
		cook_args(argv);
	} else {
		raw_args(argv);
	}
	if (fclose(stdout)) {
		perror("stdout");
	}
	exit(rval);
}

static void
cook_args(argv)
	char **argv;
{
	FILE *fp;

	fp = stdin;
	filename = "stdin";
	do {
		if (*argv) {
			if (!strcmp(*argv, "-"))
				fp = stdin;
			else if (!(fp = fopen(*argv, "r"))) {
				perror(*argv);
				++argv;
				continue;
			}
			filename = *argv++;
		}
		cook_buf(fp);
		if (fp != stdin)
			(void)fclose(fp);
	} while (*argv);
}

static void
cook_buf(fp)
	FILE *fp;
{
	int ch, gobble, line, prev;

	line = gobble = 0;
	for (prev = '\n'; (ch = getc(fp)) != EOF; prev = ch) {
		if (prev == '\n') {
			if (ch == '\n') {
				if (sflag) {
					if (!gobble && putchar(ch) == EOF)
						break;
					gobble = 1;
					continue;
				}
				if (nflag && !bflag) {
					(void)fprintf(stdout, "%6d\t", ++line);
					if (ferror(stdout))
						break;
				}
			} else if (nflag) {
				(void)fprintf(stdout, "%6d\t", ++line);
				if (ferror(stdout))
					break;
			}
		}
		gobble = 0;
		if (ch == '\n') {
			if (eflag)
				if (putchar('$') == EOF)
					break;
		} else if (ch == '\t') {
			if (tflag) {
				if (putchar('^') == EOF || putchar('I') == EOF)
					break;
				continue;
			}
		} else if (vflag) {
			if (!isascii(ch)) {
				if (putchar('M') == EOF || putchar('-') == EOF)
					break;
				ch = toascii(ch);
			}
			if (iscntrl(ch)) {
				if (putchar('^') == EOF ||
				    putchar(ch == '\177' ? '?' :
				    ch | 0100) == EOF)
					break;
				continue;
			}
		}
		if (putchar(ch) == EOF)
			break;
	}
	if (ferror(fp)) {
		perror("");
		clearerr(fp);
	}
	if (ferror(stdout)) {
		perror("stdout");
	}
}

static void
raw_args(char **argv)
{
	int fd;

	fd = fileno(stdin);
	filename = "stdin";
	do {
		if (*argv) {
			if (!strcmp(*argv, "-")) {
				fd = fileno(stdin);
			} else if ((fd = open(*argv, O_RDONLY, 0)) < 0) {
				perror(*argv);
				++argv;
				continue;
			}
			filename = *argv++;
		}
		raw_cat(fd);
		if (fd != fileno(stdin)) {
			(void)close(fd);
		}
	} while (*argv);
}

static void
raw_cat(int rfd)
{
	int nr, nw, off, wfd;
	static int bsize;
	static char *buf;
	struct stat sbuf;

	wfd = fileno(stdout);
	if (!buf) {
		if (fstat(wfd, &sbuf) < 0) {
			perror(filename);
			return;
		}
		bsize = MAX(sbuf.st_blksize, 1024);
		if (!(buf = malloc((uint)bsize))) {
			perror("malloc");
			return;
		}
	}
	while ((nr = read(rfd, buf, bsize)) > 0) {
		for (off = 0; off < nr; nr -= nw, off += nw) {
			if ((nw = write(wfd, buf + off, nr)) < 0) {
				perror("stdout");
				return;
			}
		}
	}
	if (nr < 0) {
		perror(filename);
	}
}
