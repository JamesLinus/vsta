#ifndef ADB_H
#define ADB_H
/*
 * adb.h
 *	Global data defs
 */
#include <sys/types.h>
#include <mach/machreg.h>
#include <setjmp.h>

/*
 * Constants
 */
#define MAXARGS (16)	/* Max # args to :r command */
#define MAX_BPOINT (4)	/*  ... # breakpoints */

/*
 * Routines
 */
extern char *getnum(char *, uint *);
extern void set_breakpoint(void *),
	clear_breakpoint(void *),
	clear_breakpoints(void),
	dump_breakpoints(void),
	dump_syms(void);
extern ulong readloc(ulong, int);
extern ulong db_disasm(ulong, int);
extern char *nameval(ulong);
extern void getregs(struct trapframe *);
extern ulong regs_pc(struct trapframe *);
extern void dump_regs(void);
extern void show_here(void);
extern void backtrace(void);
extern ulong read_procmem(ulong, int);
extern void run(void), step(int);
extern void wait_exec(void);
extern void rdsym(char *);
extern char *nameval(ulong);
extern ulong symval(char *);

/*
 * Variables
 */
extern pid_t corepid;		/* PID we debug, if any */
extern port_t dbg_port;		/*  ...port we talk to him via */
extern uint addr, count;	/* Addr/count for each command */
extern jmp_buf errjmp;		/* Recovery from parse errors */
extern struct map coremap;	/* Map of core for a.out */
extern ulong why_stop;		/* Why proc isn't running */

#endif /* ADB_H */

