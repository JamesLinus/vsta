/*
 * mkdir.c
 *	Create directories
 */
#include <std.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

static int pflag, errs;

/*
 * do_mkdir()
 *	mkdir() action, perhaps creating leading path elements
 */
static void
do_mkdir(char *p)
{
	char *q;

	if (mkdir(p) >= 0) {
		return;
	}
	if (!pflag) {
		perror(p);
		errs = 1;
		return;
	}
	q = p;
	for (;;) {
		char *buf;

		q = strchr(q, '/');
		if (q) {
			int len;

			len = q - p;
			buf = malloc(len + 1);
			bcopy(p, buf, len);
			buf[len] = '\0';
		} else {
			buf = p;
		}
		if ((mkdir(buf) < 0) && (errno != EEXIST)) {
			perror(buf);
			if (q) {
				free(buf);
			}
			errs = 1;
			return;
		}
		if (!q) {
			return;
		}
		free(buf);
		q += 1;
	}
}

int
main(int argc, char **argv)
{
	int x;
	
	/*
	 * -p: create path elements
	 */
	if (!strcmp(argv[1], "-p")) {
		++argv;
		--argc;
		pflag = 1;
	}
	for (x = 1; x < argc; ++x) {
		do_mkdir(argv[x]);
	}
	return(errs);
}
