/*
 * rcsdo.c
 *	Apply a command to each RCS file in a tree
 */
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static char cmd[256], buf[256];
static char curdir[512];

static void
do_dir(DIR *d)
{
	long pos;
	int olen;
	struct dirent *de;
	struct stat sb;
	char *nm;

	olen = strlen(curdir);
	while (de = readdir(d)) {

		/*
		 * Skip names starting in ".", and "rcs" and "RCS"
		 */
		nm = de->d_name;
		if (!strcmp(nm, "rcs") || !strcmp(nm, "RCS") ||
				(de->d_name[0] == '.')) {
			continue;
		}

		/*
		 * Get dope on entry
		 */
		sprintf(buf, "%s/%s", curdir, nm);
		if (stat(buf, &sb) < 0) {
			continue;
		}

		/*
		 * Recurse for directories.  Because of limitations
		 * on number of open files, we save the position and
		 * close the current dir during the recursion.
		 */
		if ((sb.st_mode & S_IFMT) == S_IFDIR) {
			pos = telldir(d);
			closedir(d);
			sprintf(curdir+olen, "/%s", nm);
			if (d = opendir(curdir)) {
				do_dir(d);
			} else {
				perror(nm);
			}
			closedir(d);
			curdir[olen] = '\0';
			d = opendir(curdir);
			seekdir(d, pos);
			continue;
		}

		/*
		 * Make sure it's an RCS file.  Allow with or without
		 * the ",v" suffix.
		 */
		sprintf(buf, "%s/RCS/%s,v", curdir, nm);
		if (access(buf, 0) < 0) {
			sprintf(buf, "%s/RCS/%s", curdir, nm);
			if (access(buf, 0) < 0) {
				continue;
			}
		}

		/*
		 * Apply the command to this file
		 */
		sprintf(buf, "%s %s/%s", cmd, curdir, nm);
		system(buf);
	}
}

main(int argc, char **argv)
{
	DIR *d;
	int x;

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
	do_dir(d);
	closedir(d);
	return(0);
}
