/*
 * touch.c
 *	Create (or update modification time) of files
 */
#include <fcntl.h>
#include <unistd.h>

/*
 * touch()
 *	Do actual touch'ing of a file
 *
 * Returns 0 on success, 1 on error
 */
static int
touch(char *p)
{
	int fd;

	/*
	 * If it exists, we *must* open for update and complain
	 * if we lack permission.
	 */
	fd = open(p, O_READ);
	if (fd > 0) {
		int len;
		char c;

		close(fd);
		fd = open(p, O_READ | O_WRITE);
		if (fd < 0) {
			perror(p);
			return(1);
		}

		/*
		 * Questionable.  Read and re-write first byte in
		 * file to make sure filesystem views file as modified.
		 * Arguably, just opening for writing should suffice.
		 */
		len = read(fd, &c, sizeof(c));
		if (len > 0) {
			if (lseek(fd, 0L, SEEK_SET) == 0L) {
				write(fd, &c, sizeof(c));
			}
		}
		close(fd);
		return(0);
	}

	/*
	 * Create file
	 */
	fd = creat(p, 0600);
	if (fd > 0) {
		close(fd);
		return(0);
	}
	perror(p);
	return(1);
}

int
main(int argc, char **argv)
{
	int x, errs = 0;

	for (x = 1; x < argc; ++x) {
		errs |= touch(argv[x]);
	}
	return(errs);
}
