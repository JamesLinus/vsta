/*
 * uname.c
 *	uname for VSTa
 */
#include <stdio.h>
#include <paths.h>
#include <ctype.h>
#include <string.h>

/*
 * my_puts()
 *	Put out string, with leading blank after first
 */
#define my_puts(cond, str) if (cond) { \
	if (!lead) { lead = 1; } else { putchar(' '); } \
	fputs(str, stdout); }

static char *prog;	/* argv[0], global */
static char lead;	/* Put out leading blank yet? */

/*
 * Flags
 */
static char mflag, nflag, rflag, sflag;

/*
 * usage()
 *	Tell how to use, then exit
 */
static void
usage(void)
{
	fprintf(stderr, "Usage is: %s [ -amnrsv ]\n", prog);
	exit(1);
}

/*
 * get_node()
 *	Get our hostname from gethostname()
 */
static char *
get_node(void)
{
	static char buf[128];

	(void)gethostname(buf, sizeof(buf));
	return(buf);
}

/*
 * get_ver()
 *	Return version string from our welcome file
 */
static char *
get_ver(void)
{
	FILE *fp;
	char *ver = NULL, *p, buf[128];

	if (fp = fopen(_PATH_BANNER, "r")) {
		while (fgets(buf, sizeof(buf), fp)) {
			p = strstr(buf, "VSTa v");
			if (!p) {
				continue;
			}
			p += 6;
			if (!isdigit(*p)) {
				continue;
			}
			ver = p-1;
			while (isdigit(*p) || (*p == '.')) {
				++p;
			}
			*p = '\0';
			break;
		}
	}
	fclose(fp);
	if (!ver) {
		return("v???");
	}
	return(ver);
}

int
main(int argc, char **argv)
{
	int x, y;

	prog = argv[0];
	for (x = 1; x < argc; ++x) {
		if (argv[x][0] != '-') {
			usage();
		}
		for (y = 1; argv[x][y]; ++y) {
			switch (argv[x][y]) {
			case 'a':
				mflag = nflag = rflag = sflag = 1;
				break;
			case 'm':
				mflag = 1;
				break;
			case 'n':
				nflag = 1;
				break;
			case 'r':
			case 'v':
				rflag = 1;
				break;
			case 's':
				sflag = 1;
				break;
			default:
				usage();
			}
		}
	}

	/*
	 * Default?
	 */
	if (!mflag && !nflag && !rflag && !sflag) {
		sflag = 1;
	}

	/*
	 * Dump out what's requested
	 */
	my_puts(sflag, "VSTa");
	my_puts(nflag, get_node());
	my_puts(rflag, get_ver());
#include "./mach/cpu.c"
	putchar('\n');
	return(0);
}
