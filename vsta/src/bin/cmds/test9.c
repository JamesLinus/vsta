#include <sys/ports.h>
#include <sys/fs.h>

static void
tick()
{
	static char msg[] = " tick\n";

	for (;;) {
		sleep(1);
		write(1, msg, sizeof(msg)-1);
	}
}

main()
{
	port_t kbd, scrn;
	static char msg[] = "Tock\n";

	kbd = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(kbd);
	scrn = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(scrn);

	tfork(tick);
	for (;;) {
		sleep(2);
		write(1, msg, sizeof(msg)-1);
	}
}
