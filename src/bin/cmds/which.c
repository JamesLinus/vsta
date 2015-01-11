/*
 * which.c
 *	Tell what path in your $PATH reaches an executable
 */
#include <stdio.h>
#include <stat.h>
#include <std.h>

static char **paths;	/* PATH elements */
static int longest;	/*  ...strlen() of longest in paths */

/*
 * which()
 *	Tell path reaches our executable, if any
 */
static void
which(char *prog)
{
	int x;
	char *p, *buf;
	struct stat sb;

	buf = alloca(longest + strlen(prog) + 10);
	for (x = 0; p = paths[x]; ++x) {
		sprintf(buf, "%s/%s", paths[x], prog);
		if (stat(buf, &sb) < 0) {
			continue;
		}
		if (sb.st_mode & 0111) {
			printf("%s\n", buf);
			return;
		}
	}
	fprintf(stderr, "%s: not found\n", prog);
}

int
main(int argc, char **argv)
{
	int x, npath = 0;
	char *p, *pn;

	/*
	 * Get PATH to search
	 */
	p = getenv("PATH");
	if (!p) {
		fprintf(stderr, "%s: no PATH\n", argv[0]);
		exit(1);
	}

	/*
	 * Explode PATH out into array of individual path elements
	 */
	while (p) {
		/*
		 * Point to separator if any
		 */
		pn = strchr(p, ':');
		if (pn) {
			*pn++ = '\0';
		}

		/*
		 * Grow "paths" to have room for another element
		 */
		paths = realloc(paths, (npath+2)*sizeof(char *));

		/*
		 * Put it in
		 */
		paths[npath++] = p;

		/*
		 * Keep track of longest path element
		 */
		x = strlen(p);
		if (x > longest) {
			longest = x;
		}

		/*
		 * Advance to next
		 */
		p = pn;
	}

	/*
	 * For each command, search
	 */
	for (x = 1; x < argc; ++x) {
		which(argv[x]);
	}
	return(0);
}
