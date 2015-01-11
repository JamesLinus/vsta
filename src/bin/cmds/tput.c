/*
 * tput.c
 *	Emit the specified termcap string
 */
#include <std.h>
#include <stdio.h>
#include <termcap.h>

static int errs;	/* Flag if any errors */

/*
 * myputc()
 *	Put out a char to stdout
 */
static int
myputc(char c)
{
	putchar(c);
}

/*
 * emit()
 *	Ask termcap for the requested capability string, print it
 */
static void
emit(char *cap)
{
	char *p, *q, buf[1024];
	int x;

	q = buf;
	p = tgetstr(cap, &q);
	if (p) {
		tputs(p, 0, (void *)myputc);
		return;
	}
	x = tgetnum(cap);
	if (x >= 0) {
		printf("%d", x);
		return;
	}
	printf("OOPS");
	errs = 1;
}

int
main(int argc, char **argv)
{
	int x;
	char *term, buf[4096];

	term = getenv("TERM");
	if (term == 0) {
		return(0);
	}
	if (!tgetent(buf, term)) {
		printf("Can't find '%s'\n", term);
		return(1);
	}
	for (x = 1; x < argc; ++x) {
		emit(argv[x]);
	}
	fflush(stdout);
	return(errs);
}
