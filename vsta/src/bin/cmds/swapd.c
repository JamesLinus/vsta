/*
 * swapd.c
 *	Launch all the stuff needed for the swapping system
 */
#include <sys/types.h>
#include <sys/ports.h>
#include <sys/fs.h>
#include <swap/swap.h>

#define NQIO (4)		/* # parallel qio's */

static void
runqio(void)
{
	run_qio();
	perror("swapd run_qio failed");
	exit(1);
}

main(argc, argv)
	int argc;
	char **argv;
{
	port_t p;
	int x;

	/*
	 * Connect to swap daemon
	 */
	p = msg_connect(PORT_SWAP, ACC_READ|ACC_WRITE);
	if (p < 0) {
		perror("swapd can't access daemon");
		exit(1);
	}

	/*
	 * If there's an initial swap device, hand it to the
	 * swapper now.
	 */
	if (argc > 1) {
		struct swapadd s;
		port_name n;
		struct msg m;

		n = namer_find(argv[1]);
		s.s_port = n;
		s.s_off = 0;
		s.s_len = 0;
		if (argc > 2) {
			strcpy(s.s_path, argv[2]);
		} else {
			s.s_path[0] = '\0';
		}
		m.m_op = SWAP_ADD;
		m.m_buf = &s;
		m.m_buflen = sizeof(s);
		m.m_nseg = 1;
		m.m_arg = m.m_arg1 = 0;
		if (msg_send(p, &m) < 0) {
			perror("swapd initial swap");
			exit(1);
		}
	}

	/*
	 * Hand off daemon to kernel
	 */
	if (set_swapdev(p) < 0) {
		perror("swapd can't configure swap port");
		exit(1);
	}
	for (x = 0; x < NQIO; ++x) {
		if (tfork(runqio) < 0) {
			perror("swapd qio fork");
		}
	}
	pageout();
	perror("swapd can't start pageout");
	exit(1);
}
