/*
 * test.c
 *	Verify data integrity, especially at potential boundaries
 */
#include <fcntl.h>
#include <std.h>

#define SECSZ (512)

static void
mkf(char *file, int blksz)
{
	int x, fd, pos = 0, pos2 = 0;
	char *p, *p2;

	/*
	 * Open destination file
	 */
	if ((fd = open(file, O_WRITE|O_CREAT, 0600)) < 0) {
		perror(file);
		exit(1);
	}

	/*
	 * Create pattern buffers
	 */
	p = malloc(blksz);
	p2 = malloc(blksz);
	if (!p || !p2) {
		perror("malloc");
		exit(1);
	}
	for (x = 0; x < blksz; ++x)
		p[x] = x & 0xFF;

	/*
	 * Fill file with pattern
	 */
	while (pos < 3*SECSZ) {
		write(fd, p, blksz);
		pos += blksz;
	}

	/*
	 * Close file, reopen to read
	 */
	close(fd);
	if ((fd = open(file, O_READ)) < 0) {
		perror(file);
		exit(1);
	}

	/*
	 * Read contents and verify
	 */
	while ((x = read(fd, p2, blksz)) > 0) {
		if (x != blksz) {
			printf("Bad read at %d, got %d\n", pos2, x);
			exit(1);
		}
		if (bcmp(p, p2, blksz)) {
			printf("Data mismatch at %d\n", pos2);
			exit(1);
		}
		pos2 += blksz;
	}

	/*
	 * Verify got all data
	 */
	if (pos != pos2) {
		printf("Short read of file, end pos is %d should be %d\n",
			pos2, pos);
		exit(1);
	}
	close(fd);
}

main(int argc, char **argv)
{
	char buf[80];
	int growth, count = 0;

	if (argc != 2) {
		printf("Usage is: %s <path>\n", argv[0]);
		exit(1);
	}
	for (growth = 10; growth < 2*SECSZ; growth += 10) {
		sprintf(buf, "%s/%d", argv[1], count++);
		printf("File %s, block size %d\n", buf, growth);
		mkf(buf, growth);
	}
	return(0);
}
