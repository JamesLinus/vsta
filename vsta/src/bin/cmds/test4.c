#include <sys/ports.h>
#include <sys/fs.h>
#include <stdio.h>
#include <fcntl.h>

main()
{
	int fd, x, kbd, scrn;
	port_t port;
	char buf[64];

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	(void)__fd_alloc(scrn);
	printf("Connect to NAMER...\n");
	port = msg_connect(PORT_NAMER, ACC_READ);
	printf(" ...returns %d\n", port);
	printf("Mount %d on /mnt...\n", port);
	x = mountport("/mnt", port);
	if (x < 0) {
		perror("mount");
	}
	printf(" ...mount returns %d\n", x);
	printf("Chdir down to /mnt...\n");
	x = chdir("/mnt");
	printf(" ...returns %d\n", x);
	printf("Open /mnt...\n");
	fd = open("/mnt", O_READ);
	printf("Read contents:\n");
	while ((x = read(fd, buf, sizeof(buf))) > 0) {
		write(1, buf, x);
	}
	printf("Close\n");
	close(fd);
	exit(0);
}
