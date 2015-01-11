/*
 * proc.c
 *	Handling of a running process
 */
#include <sys/msg.h>
#define PROC_DEBUG
#include <sys/proc.h>
#include <fcntl.h>
#include <stdio.h>
#include <std.h>
#include "adb.h"
#include "map.h"

static void
	*bpoints[MAX_BPOINT];	/* Breakpoints set */
static uint nbpoint = 0;	/*  ...how many in bpoints[] */
static int ever_ran = 0;	/* Tell if first run */
static int stepping = 0;	/* Stepping currently? */
ulong why_stop;			/* Copy of flags for why we stopped */

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
		m.m_op &= MSG_MASK;
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

		case PD_SLAVE:
			why_stop = m.m_arg;
			/* VVV fall into VVV */
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
		printf("Read error at addr 0x%lx\n", addr);
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
 * eventstr()
 *	Return current event
 */
static char *
eventstr(void)
{
	static char evstr[ERRLEN+2];
	uint x;
	ulong args[2];

	for (x = 0; x < ERRLEN; ++x) {
		args[0] = x;
		args[1] = 0;
		if (sendhim(PD_MEVENT, args) < 0) {
			evstr[x] = '\0';
			break;
		}
		evstr[x] = args[1];
		if (evstr[x] == '\0') {
			break;
		}
	}
	if (evstr[0] == '\0') {
		return(0);
	}
	return(evstr);
}

/*
 * cur_event()
 *	Print out why we're stopped
 */
static void
cur_event(void)
{
	printf("pid %ld stopped", corepid);
	if (why_stop & PD_EVENT) {
		printf(" at event: %s", eventstr());
	} else if (why_stop & PD_BPOINT) {
		if (!stepping) {
			printf(" at breakpoint");
		}
	} else if (why_stop & PD_EXIT) {
		printf(" while exiting");
	}
	printf("\n");
}

/*
 * run()
 *	Tell slave to run
 */
void
run(void)
{
	ulong args[2];

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
	why_stop = args[0];

	/*
	 * Show where he popped up
	 */
	cur_event();
	show_here();
}

/*
 * step()
 *	Set single-step, and run one step
 *
 * If "over" is true, will "step" by figuring out address of
 * next instruction, and setting temporary breakpoint there.
 * This has the effect of stepping over function calls, if the
 * next instruction is a "call".
 */
void
step(int over)
{
	ulong args[2];
	int oldstdout, tempstdout;
	uint nextpc;
	struct trapframe cur_regs;

	/*
	 * Ask him to step, use run(), and then clear the step stuff
	 * so we can continue properly.
	 */
	if (!over) {
		args[0] = 1;
		args[1] = 0;
		(void)sendhim(PD_STEP, args);
		run();
		args[0] = 0;
		args[1] = 0;
		(void)sendhim(PD_STEP, args);
		return;
	}

	/*
	 * Arrange for disassembler to silently calculate
	 * next instruction address for us.  It does printf()'s,
	 * so we just channel stdout to nowhere for a second...
	 */
	getregs(&cur_regs);
	oldstdout = dup(1);
	close(1);
	tempstdout = open("/dev/null", O_WRITE);
	nextpc = db_disasm(regs_pc(&cur_regs), 0);
	close(tempstdout);
	dup2(oldstdout, 1);
	close(oldstdout);
	set_breakpoint((void *)nextpc);
	run();
	clear_breakpoint((void *)nextpc);
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
 * set_breakpoint()
 *	Set a new breakpoint
 */
void
set_breakpoint(void *addr)
{
	uint x;
	ulong args[2];

	/*
	 * Check count and addr
	 */
	if (nbpoint >= MAX_BPOINT) {
		printf("No more breakpoints possible\n");
		return;
	}
	if (!addr) {
		printf("Invalid breakpoint address\n");
		return;
	}

	/*
	 * Find slot for operation
	 */
	for (x = 0; x < MAX_BPOINT; ++x) {
		if (!bpoints[x])
			break;
	}

	/*
	 * Update breakpoint table
	 */
	if (x >= MAX_BPOINT) {
		printf("Oops, breakpoint table out of synch\n");
		return;
	}
	nbpoint += 1;
	bpoints[x] = addr;

	/*
	 * Tell our humble slave
	 */
	args[1] = (ulong)addr;
	args[0] = 1;
	if (sendhim(PD_BREAK, args) < 0) {
		bpoints[x] = 0;
		nbpoint -= 1;
		printf("Breakpoint operation failed\n");
	}
}

/*
 * clear_breakpoint()
 *	Clear a breakpoint
 */
void
clear_breakpoint(void *addr)
{
	uint x;
	ulong args[2];

	/*
	 * Check count and addr
	 */
	if (!nbpoint) {
		printf("No breakpoints set\n");
		return;
	}

	/*
	 * Find slot for operation
	 */
	for (x = 0; x < MAX_BPOINT; ++x) {
		if (bpoints[x] == addr) {
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
	if (x >= MAX_BPOINT) {
		printf("No breakpoint at 0x%lx\n", (ulong)addr);
		return;
	}

	/*
	 * Tell our humble slave
	 */
	args[1] = (ulong)addr;
	args[0] = 0;
	if (sendhim(PD_BREAK, args) < 0) {
		printf("Breakpoint operation failed\n");
	}
}

/*
 * clear_breakpoints()
 *	Clear all current breakpoints
 */
void
clear_breakpoints(void)
{
	uint x;

	for (x = 0; x < MAX_BPOINT; ++x) {
		if (!bpoints[x])
			continue;
		clear_breakpoint(bpoints[x]);
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
			printf(" %s (0x%lx)\n",
				nameval((ulong)bpoints[x]),
				(ulong)bpoints[x]);
		}
	}
}

/*
 * wait_exec()
 *	Fiddle flags and wait for child process to finish exec()'iing
 */
void
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
