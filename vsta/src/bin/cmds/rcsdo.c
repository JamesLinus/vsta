/*
 * rcsdo.c
 *	Apply a command to each RCS file in a tree
 */
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static char cmd[128], buf[128];
static char curdir[256];
static int verbose = 0;

static void
do_dir(DIR *d, int in_rcs)
{
	long pos;
	int olen;
	struct dirent *de;
	struct stat sb;

	if (verbose) {
		printf("Enter %sdir %s\n",
			in_rcs ? "RCS " : "", curdir);
	}
	olen = strlen(curdir);
	while (de = readdir(d)) {
		/*
		 * Skip ".", ".."
		 */
		if (de->d_name[0] == '.') {
			if (!strcmp(de->d_name, ".") ||
					!strcmp(de->d_name, "..")) {
				continue;
			}
		}

		/*
		 * Get dope on entry
		 */
		sprintf(buf, "%s/%s", curdir, de->d_name);
		if (stat(buf, &sb) < 0) {
			continue;
		}

		/*
		 * Recurse for directories.  Because of limitations
		 * on number of open files, we save the position and
		 * close the current dir during the recursion.
		 */
		if ((sb.st_mode & S_IFMT) == S_IFDIR) {
			/*
			 * Sanity
			 */
			if (in_rcs) {
				printf("Error: dir in RCS/: %s\n",
					de->d_name);
				continue;
			}

			pos = telldir(d);
			closedir(d);
			sprintf(curdir+olen, "/%s", de->d_name);
			if (d = opendir(curdir)) {
				do_dir(d, !strcmp(de->d_name, "rcs"));
			} else {
				perror(de->d_name);
			}
			closedir(d);
			curdir[olen] = '\0';
			d = opendir(curdir);
			seekdir(d, pos);
			continue;
		}

		/*
		 * If not an RCS directory, ignore
		 */
		if (!in_rcs) {
			continue;
		}

		/*
		 * Otherwise apply the command to this file
		 */
		sprintf(buf, "%s%s", cmd, de->d_name);
		if (verbose) {
			printf(" - %s\n", buf);
		}
		system(buf);
	}
}

main(int argc, char **argv)
{
	DIR *d;
	int x;

	if ((argc > 1) && !strcmp(argv[1], "-v")) {
		argc -= 1;
		argv += 1;
		verbose = 1;
	}
	if (argc < 2) {
		printf("Usage is: %s [-v] <RCS commands...>\n", argv[0]);
		exit(1);
	}
	for (x = 1; x < argc; ++x) {
		strcat(cmd, argv[x]);
		strcat(cmd, " ");
	}
	d = opendir(".");
	strcpy(curdir, ".");
	if (!d) {
		perror(".");
		exit(1);
	}
	do_dir(d, 0);
	closedir(d);
	return(0);
}
