/*
 * cp.c
 *	Copy files
 */
#include <fcntl.h>
#include <stdio.h>
#include <std.h>
#include <stat.h>
#include <dirent.h>

static char *buf;		/* I/O buffer--malloc()'ed */
#define BUFSIZE (16*1024)	/* I/O buffer size */

/*
 * Part of source filename not copied.  When you copy, the destination
 * gets only the basename of the path (or basename on down, for -r).
 * So "cp /x/y /z" does not create file "/z/x/y", only "/z/y".
 */
static int path_off;

static int errs = 0;		/* Count of errors */
static int rflag = 0,		/* Recursive copy */
	vflag = 0;		/* Verbose */

static void cp_file(char *, char *);

/*
 * fisdir()
 *	Version of isdir() given file descriptor
 */
static int
fisdir(int fd)
{
	struct stat sb;

	if (fstat(fd, &sb) < 0) {
		return(0);
	}
	return((sb.st_mode & S_IFMT) == S_IFDIR);
}

/*
 * isdir()
 *	Tell if named file is a directory
 */
static int
isdir(char *n)
{
	int x, fd;

	fd = open(n, O_READ);
	if (fd < 0) {
		return(0);
	}
	x = fisdir(fd);
	close(fd);
	return(x);
}

/*
 * cp_recurse()
 *	Recursively copy contents of one dir into another
 */
static void
cp_recurse(char *src, char *dest)
{
	DIR *dir;
	struct dirent *de;
	char *srcname, *destname;

	if ((dir = opendir(src)) == 0) {
		perror(src);
		return;
	}
	(void)mkdir(dest);
	while (de = readdir(dir)) {
		srcname = malloc(strlen(src)+strlen(de->d_name)+2);
		if (srcname == 0) {
			perror(src);
			exit(1);
		}
		sprintf(srcname, "%s/%s", src, de->d_name);
		destname = malloc(strlen(dest)+strlen(de->d_name)+2);
		if (destname == 0) {
			perror(dest);
			exit(1);
		}
		sprintf(destname, "%s/%s", dest, de->d_name);
		cp_file(srcname, destname);
		free(destname); free(srcname);
	}
	closedir(dir);
}

/*
 * cp_file()
 *	Copy one file to another
 */
static void
cp_file(char *src, char *dest)
{
	int in, out, x;

	/*
	 * Access source
	 */
	if ((in = open(src, O_READ)) < 0) {
		perror(src);
		errs += 1;
		return;
	}

	/*
	 * If source is a directory, bomb or start recursive copy
	 */
	if (fisdir(in)) {
		close(in);
		if (!rflag) {
			fprintf(stderr, "%s: is a directory\n", src);
			errs += 1;
			return;
		}
		cp_recurse(src, dest);
		return;
	}

	if (vflag) {
		printf("%s -> %s\n", src, dest);
	}
	if ((out = open(dest, O_WRITE|O_CREAT, 0666)) < 0) {
		perror(dest);
		errs += 1;
		close(in);
		return;
	}
	while ((x = read(in, buf, BUFSIZE)) > 0) {
		write(out, buf, x);
	}
	close(out);
	close(in);
}

/*
 * cp_dir()
 *	Copy a file into a directory, using same name
 */
static void
cp_dir(char *src, char *destdir)
{
	char *dest;

	dest = malloc((strlen(src) - path_off) + strlen(destdir) + 2);
	if (dest == 0) {
		perror(destdir);
		errs += 1;
		return;
	}
	sprintf(dest, "%s/%s", destdir, src+path_off);
	cp_file(src, dest);
	free(dest);
}

/*
 * usage()
 *	Give usage message & exit
 */
static void
usage(void)
{
	fprintf(stderr,
"Usage is: cp <src> <dest>\nor\tcp <src1> [ <src2> ... ] <destdir>\n");
	exit(1);
}

main(int argc, char **argv)
{
	int lastdir;
	char *dest;

	/*
	 * Get an I/O buffer
	 */
	buf = malloc(BUFSIZE);
	if (buf == 0) {
		perror("cp");
		exit(1);
	}

	while ((argc > 1) && (argv[1][0] == '-')) {
		char *p;

		/*
		 * Process switches
		 */
		for (p = argv[1]+1; *p; ++p) {
			switch (*p) {
			case 'r':
				rflag = 1; break;
			case 'v':
				vflag = 1; break;
			default:
				fprintf(stderr, "Unknown flag: %c\n",
					*p);
				exit(1);
			}
		}

		/*
		 * Make the option(s) disappear
		 */
		argc -= 1;
		argv[1] = argv[0];
		argv += 1;
	}

	/*
	 * Sanity check argument count
	 */
	if (argc < 3) {
		usage();
	}

	/*
	 * Parse rest of args.  Support both:
	 *	cp <src> <dest>
	 * and	cp <src1> <src2> ... <dest-dir>
	 */
	dest = argv[argc-1];
	lastdir = isdir(dest);
	if ((argc > 3) || lastdir) {
		int x;

		if (!lastdir) {
			usage();
		}
		for (x = 1; x < argc-1; ++x)  {
			char *p;

			if (p = strrchr(argv[x], '/')) {
				path_off = (p - argv[x])+1;
			} else {
				path_off = 0;
			}
			cp_dir(argv[x], dest);
		}
		return(errs);
	}
	cp_file(argv[1], argv[2]);
	return(errs);
}
