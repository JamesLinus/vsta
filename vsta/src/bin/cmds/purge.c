/*
 * purge.c
 *	Purge old revisions
 *
 * -r		Recurse down directories
 * -k <n>	Keep <n> newest versions (default 2)
 * -n		List what would be done, don't do it
 */
#include <stdio.h>
#include <getopt.h>
#include <alloc.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <std.h>
#include <fdl.h>

extern char *__fieldval(char *, char *);

static int nkeep = 2;	/* # revisions to keep */
static int nflag,	/* Command switches */
	rflag,
	vflag;
static int errs;	/* Errors? */

/*
 * getstat()
 *	Get stat string from named file
 */
static char *
getstat(char *file, char *val)
{
	int fd;
	char *p;
	extern char *rstat(port_t, char *);

	fd = open(file, O_READ);
	if (fd < 0) {
		return(NULL);
	}
	p = rstat(__fd_port(fd), val);
	close(fd);
	return(p);
}

/*
 * purge_file()
 *	Delete file revisions, keeping "nkeep" most recent
 */
static void
purge_file(char *name, char *statstr)
{
	char *p, *tmpname;
	int x, revs;
	ulong cur;

	/*
	 * Initial file under consideration--the newest revision
	 */
	tmpname = alloca(strlen(name) + 16);
	sprintf(tmpname, "%s,,0", name);
	cur = 0;

	/*
	 * Walk back revisions
	 */
	for (revs = 1; statstr; ++revs) {
		/*
		 * Get the dope on the stat string before
		 * our unlink() stomps on the stat buffer.
		 */
		p = __fieldval(statstr, "prev");
		if (p) {
			x = sscanf(p, "%U", &cur);
		}

		/*
		 * Delete the file?
		 */
		if (revs > nkeep) {
			if (nflag || vflag) {
				printf("purge %s\n", tmpname);
			}
			if (!nflag) {
				unlink(tmpname);
			}
		}

		/*
		 * Done if no stat string, bad parse, or no
		 * more versions.
		 */
		if (!p || (x != 1) || !cur) {
			return;
		}

		sprintf(tmpname, "%s,,%U", name, cur);
		statstr = getstat(tmpname, NULL);
	}
}

/*
 * purge_dir()
 *	Walk down into a dir, purge its contents
 */
static void
purge_dir(char *name)
{
	DIR *d;
	long pos;
	int olen;
	struct dirent *de;
	char *curdir, *buf;
	char *statstr, *type;

	d = opendir(name);
	if (!d) {
		return;
	}
	olen = strlen(name);
	curdir = alloca(olen + 128);
	strcpy(curdir, name);
	buf = alloca(olen + 128);
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
		statstr = getstat(buf, NULL);
		if (!statstr) {
			continue;
		}
		type = __fieldval(statstr, "type");
		if (!type) {
			continue;
		}

		/*
		 * Recurse for directories.  Because of limitations
		 * on number of open files, we save the position and
		 * close the current dir during the recursion.
		 */
		if (*type == 'd') {
			pos = telldir(d);
			closedir(d);
			sprintf(curdir+olen, "/%s", de->d_name);
			purge_dir(curdir);
			curdir[olen] = '\0';
			d = opendir(curdir);
			seekdir(d, pos);
			continue;
		}

		/*
		 * Do the file thang for files
		 */
		if (*type == 'f') {
			purge_file(buf, statstr);
		}
	}
	closedir(d);
}

/*
 * do_purge()
 *	Purge an entry
 *
 * Recurse on directories if -r, otherwise no-op them
 */
static void
do_purge(char *name)
{
	char *p, *statstr;

	/*
	 * Get attributes of next file
	 */
	statstr = getstat(name, NULL);
	p = __fieldval(statstr, "type");

	/*
	 * If it's a file, purge it
	 */
	if (p && (*p == 'f')) {
		purge_file(name, statstr);
		return;
	}

	/*
	 * Directory?  Recurse if -r
	 */
	if (p && (*p == 'd') && rflag) {
		purge_dir(name);
	}

	/*
	 * No-op everything else
	 */
}

int
main(int argc, char **argv)
{
	int x;

	while ((x = getopt(argc, argv, "rnvk:")) > 0) {
		switch (x) {
		case 'n':
			nflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		case 'k':
			if (sscanf(optarg, "%d", &nkeep) != 1) {
				fprintf(stderr, "Bad num to -k: %s\n",
					optarg);
				exit(1);
			}
			break;
		default:
			fprintf(stderr,
			 "Usage is: %s [-rn] [-k <revs>] <file> [<file>...]");
			exit(1);
		}
	}

	/*
	 * Always keep one
	 */
	if (nkeep < 1) {
		nkeep = 1;
	}

	/*
	 * Process files and directories
	 */
	for (x = optind; x < argc; ++x) {
		do_purge(argv[x]);
	}

	return(errs);
}
