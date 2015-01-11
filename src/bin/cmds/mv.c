/*
 * mv.c
 *	Move files
 */
#include <stdio.h>
#include <stat.h>
#include <std.h>

/*
 * usage()
 *	Tell how to use
 */
static void
usage(void)
{
	fprintf(stderr, "Usage is: mv <src> [<src> ...] <dest>\n");
	exit(1);
}

main(int argc, char **argv)
{
	struct stat sb;
	int errs;

	if (argc < 3) {
		usage();
	}

	/*
	 * If moving a bunch of files to a directory, last element
	 * has to be a directory.
	 */
	if (stat(argv[argc-1], &sb) < 0) {
		/*
		 * a, b, c -> d		But d doesn't exist
		 */
		if (argc > 3) {
			fprintf(stderr, "Last argument should be dir: %s\n",
				argv[argc-1]);
			exit(1);
		}

		/*
		 * a -> b
		 */
		if (rename(argv[1], argv[2]) < 0) {
			perror(argv[2]);
			exit(1);
		}
		exit(0);
	}

	/*
	 * If dest is a directory, move each into it
	 */
	errs = 0;
	if ((sb.st_mode & S_IFMT) == S_IFDIR) {
		int x, len;
		char *base;

		argc -= 1;
		base = argv[argc];
		len = strlen(base)+1;
		for (x = 1; x < argc; ++x) {
			char *dest;

			dest = malloc(len + strlen(argv[x]) + 1);
			sprintf(dest, "%s/%s", base, argv[x]);
			if (rename(argv[x], dest) < 0) {
				perror(dest);
				errs += 1;
			}
		}
	} else {
		/*
		 * Existing dest--overwrite if a -> b, otherwise error
		 */
		if (argc != 3) {
			fprintf(stderr, "Last argument should be dir: %s\n",
				argv[argc-1]);
			exit(1);
		}
		unlink(argv[2]);
		if (rename(argv[1], argv[2]) < 0) {
			perror(argv[2]);
			errs = 1;
		}
	}
	return(errs ? 1 : 0);
}
