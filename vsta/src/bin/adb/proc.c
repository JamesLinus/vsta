/*
 * proc.c
 *	Handling of a running process
 */
#include <sys/types.h>
#include <sys/msg.h>
#define PROC_DEBUG
#include <sys/proc.h>
#include "map.h"

extern port_t dbg_port;

/*
 * sendhim()
 *	Handling for all the tedium of talking to our slave
 */
static int
sendhim(int op, ulong *args)
{
	struct msg m;
	int x;
	static int connected = 0,	/* Have seen MSG_CONNECT */
		listening = 0;		/* Is currently listening to us */

	/*
	 * If we have never heard from him, wait for the connection
	 */
	if (!connected) {
		x = msg_receive(dbg_port, &m);
		if (x < 0) {
			perror("Listen for slave");
			exit(1);
		}
		if (m.m_op != M_CONNECT) {
			printf("Unexpected message type %d\n", m.m_op);
			exit(1);
		}
		if (msg_accept(m.m_sender) < 0) {
			perror("Accept of slave");
			exit(1);
		}
		connected = 1;
	}

	/*
	 * Now wait for him to send us a request.  Our relationship with
	 * him is strange; we are the server, so we have to wait for him
	 * to make a request.
	 */
	while (!listening) {
		x = msg_receive(dbg_port, &m);
		if (x < 0) {
			perror("Listen for slave request");
			exit(1);
		}

		switch (m.m_op) {
		case M_DISCONNECT:
			/*
			 * He exited, or something like that
			 */
		 	printf("Lost connect to slave\n");
			exit(1);

		case M_DUP:
			/*
			 * He fork()'ed a child; ignore the child
			 */
			break;

		default:
			listening = 1;
			break;
		}
	}

	/*
	 * Formulate our request, and send it to him.  That is,
	 * reply to his "request" with our command.  His next
	 * "request" will have the answer.
	 */
	m.m_op = op;
	m.m_nseg = 0;
	m.m_arg = args[0];
	m.m_arg1 = args[1];
	if (msg_reply(m.m_sender, &m) < 0) {
		perror("Reply to slave");
		exit(1);
	}

	/*
	 * Now get his answer
	 */
	x = msg_receive(dbg_port, &m);
	if (x < 0) {
		perror("Response from slave");
		exit(1);
	}
	args[0] = m.m_arg;
	args[1] = m.m_arg1;
	return(0);
}

/*
 * read_procmem()
 *	Read some bytes from process memory
 */
ulong
read_procmem(ulong addr, int size)
{
	ulong args[2];

	args[0] = addr & ~(sizeof(long)-1);
	args[1] = 0;
	if ((sendhim(PD_RDMEM, args) < 0) || args[1]) {
		printf("Read error at addr 0x%x\n", addr);
		return(0);
	}
	return(args[0]);
}
