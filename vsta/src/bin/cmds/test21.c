/*
 * test21.c
 *	mmap() a file and see how it looks
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stat.h>

main(int argc, char **argv)
{
	int x, fd;
	char *map;
	struct stat sbuf;

	for (x = 1; x < argc; ++x) {
		fd = open(argv[x], O_READ);
		if (fd < 0) {
			perror(argv[x]);
			continue;
		}
		fstat(fd, &sbuf);
		map = mmap((void *)0, sbuf.st_size, PROT_READ,
			MAP_FILE | MAP_PRIVATE,
			__fd_port(fd), 0L);
		printf("Memory at 0x%x\n", map);
		printf("First char is: '%c'\n", *(char *)map);
		dbg_enter();
		if (map == 0) {
			perror(argv[x]);
		} else {
			write(1, map, sbuf.st_size);
		}
		munmap(map, sbuf.st_size);
		close(fd);
	}
}
