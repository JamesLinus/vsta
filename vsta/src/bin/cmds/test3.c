#include <sys/ports.h>
#include <sys/fs.h>
#include <stdio.h>

main()
{
	int scrn, kbd;
	int x;
	char buf[128];

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);
	printf("Hello, world.\n");
	for (;;) {
		if (gets(buf) == 0) {
			printf("Read failed\n");
			continue;
		}
		printf("Got '%s'\n", buf);
	}
}
