/*
 * changed.c
 *	Tell what source files have changed
 */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static char buf[128];
static char curdir[256];
static int rflag;

static void
do_dir(DIR *d)
{
	long pos;
	int olen;
	struct dirent *de;
	struct stat sb;
	char *rcsdir;

	/*
	 * Figure out name of RCS storage subdir, if any
	 */
	rcsdir = "RCS";
	sprintf(buf, "%s/%s", curdir, rcsdir);
	if (access(buf, 0) < 0) {
		rcsdir = "rcs";
		sprintf(buf, "%s/%s", curdir, rcsdir);
		if (access(buf, 0) < 0) {
			rcsdir = NULL;
		}
	}


	olen = strlen(curdir);
	while (de = readdir(d)) {
		/*
		 * Skip ".", "..", and "rcs"
		 */
		if (de->d_name[0] == '.') {
			if (!strcmp(de->d_name, ".") ||
					!strcmp(de->d_name, "..")) {
				continue;
			}
		}
		if (!stricmp(de->d_name, "rcs")) {
			continue;
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
			pos = telldir(d);
			closedir(d);
			sprintf(curdir+olen, "/%s", de->d_name);
			if (d = opendir(curdir)) {
				do_dir(d);
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
		 * If this level isn't RCS storage, don't bother
		 * further checking.
		 */
		if (!rcsdir) {
			continue;
		}

		/*
		 * If file is not both here and in RCS form, ignore
		 */
		sprintf(buf, "%s/%s/%s", curdir, rcsdir, de->d_name);
		if (access(buf, 0) < 0) {
			sprintf(buf, "%s/%s/%s,v",
				curdir, rcsdir, de->d_name);
			if (access(buf, 0) < 0) {
				if (rflag) {
					printf("%s/%s\n",
						curdir, de->d_name);
				}
				continue;
			}
		}
		if (rflag) {
			continue;
		}

		/*
		 * Flag as changed if writable.  Skip the leading
		 * "./" from the curdir string.
		 */
		if (sb.st_mode & 0222) {
			printf("%s/%s\n", curdir, de->d_name);
		}
	}
}

main(int argc, char **argv)
{
	DIR *d;
	int x;

	for (x = 1; x < argc; ++x) {
		if (strncmp(argv[x], "-r", 2)) {
			fprintf(stderr, "Usage is: %s [-r]\n", argv[0]);
			exit(1);
		}
		rflag = 1;
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
