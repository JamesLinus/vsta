/*
 * rm.c
 *	A simple file/dir removal program
 */
#include <stdio.h>
#include <stat.h>
#include <dirent.h>

/* Option flags */
static int rflag, fflag;

/* Forward dec'ls */
static int cleardir(void);
static void do_remove(char *);

/*
 * cleardir()
 *	Remove all entries in current directory
 *
 * Returns 1 on failure of dir itself, 0 otherwise.
 */
static int
cleardir(void)
{
	DIR *dir;
	struct dirent *de;

	dir = opendir(".");
	if (dir == 0) {
		return(1);
	}
	for (de = readdir(dir); de; de = readdir(dir)) {
		do_remove(de->d_name);
	}
	closedir(dir);
	return(0);
}

/*
 * do_remove()
 *	Do actual removal for a named entry
 */
static void
do_remove(char *n)
{
	struct stat sb;

	/*
	 * Common/simple case
	 */
	if (unlink(n) >= 0) {
		return;
	}

	/*
	 * Figure out what it is
	 */
	if (stat(n, &sb) < 0) {
		if (!fflag)
			perror(n);
		return;
	}

	/*
	 * Things other than directories, give up now.
	 */
	if ((sb.st_mode & S_IFMT) != S_IFDIR) {
		if (!fflag)
			perror(n);
		return;
	}

	/*
	 * If not -r, complain & bail
	 */
	if (!rflag) {
		if (!fflag)
			fprintf(stderr, "%s: is a directory\n", n);
		return;
	}

	/*
	 * Descend, remove
	 */
	if (chdir(n) < 0) {
		if (!fflag)
			perror(n);
		return;
	}
	if (cleardir()) {
		if (!fflag) {
			perror(n);
		}
	}
	if (chdir("..") < 0) {
		perror("Return from subdir");
		abort();
	}

	/*
	 * Now try to clear the dir one more time
	 */
	if (unlink(n) < 0) {
		if (!fflag)
			perror(n);
		return;
	}
}

main(int argc, char **argv)
{
	int x, forced = 0;

	/*
	 * Parse leading options
	 */
	for (x = 1; (x < argc) && !forced; ++x) {
		char *p;

		if (argv[x][0] != '-') {
			break;
		}
		for (p = argv[x]+1; *p; ++p) {
			switch (*p) {
			case 'r': rflag = 1; break;
			case 'f': fflag = 1; break;
			case '-': forced = 1; break;
			}
		}
	}

	/*
	 * Process remaining arguments on command line
	 */
	for ( ; x < argc; ++x) {
		do_remove(argv[x]);
	}
}
