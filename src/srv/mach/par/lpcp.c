/*
 * lpcp.c
 *	Copy files to printer.
 */
#include <fcntl.h>
#include <stdio.h>
#include <std.h>
#include <unistd.h>
#include <stat.h>
#include <dirent.h>

static char *buf;		/* I/O buffer--malloc()'ed */
#define BUFSIZE (16*1024)	/* I/O buffer size */

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
 *	Copy one file to printer
 */
static void
cp_file(char *src, char *dest, off_t skip)
{
	int in, out, x, cnt, bytes_written = 0;

	/*
	 * Access source
	 */
	if ((in = open(src, O_READ)) < 0) {
		fprintf(stderr, "lpcp: can't open ");
		perror(src);
		return;
	}

	/*
	 * If source is a directory, bomb
	 */
	if (fisdir(in)) {
		close(in);
		fprintf(stderr, "lpcp: %s is a directory\n", src);
		return;
	}

	if ((out = open(dest, O_WRITE|O_CREAT, 0666)) < 0) {
		fprintf(stderr, "lpcp: ");
		perror(dest);
		close(in);
		return;
	}

	if (skip) {
		lseek(in, skip, SEEK_SET);
	}

	while (1) {
		x = read(in, buf, BUFSIZE);
		if (x < 0) {
			fprintf(stderr, "lpcp: error reading %s", src);
			perror("");
			exit(1);
		}
		if (x == 0) {
			break;
		}
		cnt = write(out, buf, x);
		if (cnt >= 0) {
			bytes_written += cnt;
		}
		if (cnt != x) {
			/*
			 * Tell user where to start on next attempt.
			 */
			fprintf(stderr, "lpcp: error writing, "
				"successfully wrote %d bytes of %s",
				bytes_written, src);
			if (skip)
				fprintf(stderr, " (total %d bytes)\n",
					skip + bytes_written);
			else
				fprintf(stderr, "\n");
			exit(1);
		}
	}
	close(out);
	close(in);
}

static volatile void
usage(void)
{
	fprintf(stderr, "usage: lpcp src1 src2 ... dest\n"
			"       lpcp +skip src dest\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	off_t skip = 0;
	int x;

	/*
	 * Get an I/O buffer
	 */
	buf = malloc(BUFSIZE);
	if (buf == 0) {
		perror("lpcp");
		exit(1);
	}

	if ((argc > 4) && (argv[1][0] == '+')) {
		/*
		 * skip only allowed with exactly one src file
		 */
		usage();
	}

	if ((argc == 4) && (argv[1][0] == '+')) {
		skip = atoi(argv[1] + 1);
		argc--;
		argv++;
	}

	/*
	 * Sanity check argument count
	 */
	if (argc < 3) {
		usage();
	}

	if (isdir(argv[argc-1])) {
		fprintf(stderr, "lpcp: %s is a directory\n", argv[argc-1]);
		exit(1);
	}

	for (x = 1; x < argc-1; ++x)  {
		cp_file(argv[x], argv[argc-1], skip);
	}
	return (0);
}
