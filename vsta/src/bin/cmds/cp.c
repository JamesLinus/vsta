/*
 * cp.c
 *	Copy files
 *
 * Does not do -r, because -r hoses links and symlinks.  Use tar
 * or cpio.
 */
#include <fcntl.h>
#include <stdio.h>
#include <std.h>
#include <stat.h>

static char *buf;		/* I/O buffer--malloc()'ed */
#define BUFSIZE (16*1024)	/* I/O buffer size */

static int errs = 0;		/* Count of errors */

/*
 * cp_file()
 *	Copy one file to another
 */
static void
cp_file(char *src, char *dest)
{
	int in, out, x;

	if ((in = open(src, O_READ)) < 0) {
		perror(src);
		errs += 1;
		return;
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

	dest = malloc(strlen(src) + strlen(destdir) + 2);
	if (dest == 0) {
		perror(destdir);
		errs += 1;
		return;
	}
	sprintf(dest, "%s/%s", destdir, src);
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

/*
 * isdir()
 *	Tell if named file is a directory
 */
static int
isdir(char *n)
{
	struct stat sb;

	if (stat(n, &sb) < 0) {
		return(0);
	}
	return((sb.st_mode & S_IFMT) == S_IFDIR);
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
	if (argc < 3) {
		usage();
	}

	/*
	 * Parse args.  Support both:
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
			cp_dir(argv[x], dest);
		}
		return;
	}
	cp_file(argv[1], argv[2]);
	return(errs);
}
