/*
 * proc.c
 *	Handling of a running process
 */
#include <sys/types.h>
#include <sys/msg.h>
#define PROC_DEBUG
#include <sys/proc.h>
#include <mach/machreg.h>
#include "map.h"

#define MAX_BPOINT (4)		/* Max # breakpoints */

extern port_t dbg_port;
extern struct map coremap;

static void
	*bpoints[MAX_BPOINT];	/* Breakpoints set */
static uint nbpoint = 0;	/*  ...how many in bpoints[] */
static int ever_ran = 0;	/* Tell if first run */

/*
 * sendhim()
 *	Handling for all the tedium of talking to our slave
 */
static int
sendhim(int op, ulong *args)
{
	static struct msg m;
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
	uchar c;
	ushort w;
	ulong l;

	args[0] = addr;
	args[1] = 0;
	if ((sendhim(PD_RDMEM, args) < 0) || args[1]) {
		printf("Read error at addr 0x%x\n", addr);
		return(0);
	}
	l = args[0];
	switch (size) {
	case 1: c = *(char *)&l; return(c);
	case 2: w = *(ushort *)&l; return(w);
	case 4: return(l);
	}
	return(0);
}

/*
 * run()
 *	Tell slave to run
 */
void
run(void)
{
	ulong args[2];
	extern void show_here(void);

	/*
	 * When we first attach to a process, his mask is set
	 * to trap him out ASAP.  Now drop the mask back to
	 * what we need.
	 */
	if (!ever_ran) {
		args[0] = PD_EVENT|PD_EXIT|PD_BPOINT;
		args[1] = 0;
		(void)sendhim(PD_MASK, args);
		ever_ran = 1;
	}

	/*
	 * Away he goes.  We don't return until he breaks.
	 */
	args[0] = args[1] = 0;
	if (sendhim(PD_RUN, args) < 0) {
		printf("Error telling child to run\n");
	}

	/*
	 * Show where he popped up
	 */
	show_here();
}

/*
 * step()
 *	Set single-step, and run one step
 */
void
step(void)
{
	ulong args[2];

	/*
	 * Ask him to step, use run(), and then clear the step stuff
	 * so we can continue properly.
	 */
	args[0] = 1;
	args[1] = 0;
	(void)sendhim(PD_STEP, args);
	run();
	args[0] = 0;
	args[1] = 0;
	(void)sendhim(PD_STEP, args);
}

/*
 * getregs()
 *	Pull copy of register set
 */
void
getregs(struct trapframe *tf)
{
	uint x;
	ulong *t;

	t = (ulong *)tf;
	for (x = 0; x < sizeof(struct trapframe); x += sizeof(ulong)) {
		ulong args[2];

		args[0] = x/sizeof(ulong);
		args[1] = 0;
		if (sendhim(PD_RDREG, args) < 0) {
			*t++ = 0;
		} else {
			*t++ = args[0];
		}
	}
}

/*
 * breakpoint()
 *	Set and clear breakpoints
 */
void
breakpoint(void *addr, int set)
{
	uint x;
	ulong args[2];

	/*
	 * Check count and addr
	 */
	if (set && (nbpoint >= MAX_BPOINT)) {
		printf("No more breakpoints possible\n");
		return;
	}
	if (!set && !nbpoint) {
		printf("No breakpoints set\n");
		return;
	}
	if (set && !addr) {
		printf("Invalid breakpoint address\n");
		return;
	}

	/*
	 * Find slot for operation
	 */
	for (x = 0; x < MAX_BPOINT; ++x) {
		if (set && !bpoints[x])
			break;
		if (!set && (bpoints[x] == addr)) {
			/*
			 * May as well clear it now
			 */
			bpoints[x] = 0;
			nbpoint -= 1;
			break;
		}
	}

	/*
	 * Complain on bogosity
	 */
	if (!set && (x >= MAX_BPOINT)) {
		printf("No breakpoint at 0x%x\n", (ulong)addr);
		return;
	}
	if (set) {
		if (x >= MAX_BPOINT) {
			printf("Oops, breakpoint table out of synch\n");
			return;
		}
		nbpoint += 1;
		bpoints[x] = addr;
	}

	/*
	 * Tell our humble slave
	 */
	args[1] = (ulong)addr;
	args[0] = set;
	if (sendhim(PD_BREAK, args) < 0) {
		if (set) {
			bpoints[x] = 0;
			nbpoint -= 1;
		}
		printf("Breakpoint operation failed\n");
	}
}

/*
 * dump_breakpoints()
 *	List out current breakpoints
 */
void
dump_breakpoints(void)
{
	uint x;

	printf("Current breakpoints:\n");
	for (x = 0; x < MAX_BPOINT; ++x) {
		if (bpoints[x]) {
			printf(" 0x%x", bpoints[x]);
		}
	}
	if (nbpoint) {
		printf("\n");
	}
}

/*
 * wait_exec()
 *	Fiddle flags and wait for child process to finish exec()'iing
 */
wait_exec(void)
{
	ulong args[2];

	/*
	 * Tell him to break on exec completion
	 */
	args[0] = PD_EXEC;
	args[1] = 0;
	if (sendhim(PD_MASK, args) < 0) {
		printf("Failed to catch child exec\n");
		exit(1);
	}

	/*
	 * Run to that point.  We're in control, so short-circuit
	 * run()'s desire to choose a working mask for now.  Set it
	 * back so they can be set when we run him the first time
	 * "for real".
	 */
	ever_ran = 1;
	run();
	ever_ran = 0;
}
