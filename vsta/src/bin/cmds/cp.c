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
static int rflag = 0;		/* -r flag specified */

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
		if (!rflag) {
			fprintf(stderr, "%s: is a directory\n", src);
			errs += 1;
			return;
		}
		cp_recurse(src, dest);
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

	/*
	 * Parse "-r"; allow recursive copy
	 */
	if ((argc > 1) && !strcmp(argv[1], "-r")) {
		rflag = 1;
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
			cp_dir(argv[x], dest);
		}
		return;
	}
	cp_file(argv[1], argv[2]);
	return(errs);
}
