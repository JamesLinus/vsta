#include <sys/ports.h>
#include <sys/fs.h>

main()
{
	int x;
	static char msg[] = "Hello, world.\n";

	x = msg_connect(PORT_CONS, ACC_WRITE);
	x = __fd_alloc(x);
	for (;;) {
		write(x, msg, sizeof(msg)-1);
	}
}
