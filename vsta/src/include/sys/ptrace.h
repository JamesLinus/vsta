#ifndef _PTRACE_H
#define _PTRACE_H
/*
 * ptrace.h
 *	Definitions for ptrace() shared between kernel and debuggers
 *
 * The additional kernel stuff is in <sys/proc.h>
 */

/* Bits in pd_flags */
#define PD_ALWAYS 1		/* Stop at next chance */
#define PD_SYSCALL 2		/*  ...before & after syscalls */
#define PD_EVENT 4		/*  ...when receiving an event */
#define PD_EXIT 8		/*  ...when exiting */
#define PD_BPOINT 16		/*  ...breakpoint reached */
#define PD_EXEC 32		/*  ...exec done (new addr space) */
#define PD_CONNECTING 0x8000	/* Slave has connect in progress */

/*
 * Values for m_op of a debug message.  The sense of the operation
 * is further defined by m_arg and m_arg1.
 */
#define PD_SLAVE 300		/* Slave ready for commands */
#define PD_RUN 301		/* Run */
#define PD_STEP 302		/* Run one instruction, then break */
#define PD_BREAK 303		/* Set/clear breakpoint */
#define PD_RDREG 304		/* Read registers */
#define PD_WRREG 305		/*  ...write */
#define PD_MASK 306		/* Set mask */
#define PD_RDMEM 307		/* Read memory */
#define PD_WRMEM 308		/*  ...write */
#define PD_MEVENT 309		/* Read/write event */
#define PD_PID 310		/* Report PID/TID */

#endif /* _PTRACE_H */
