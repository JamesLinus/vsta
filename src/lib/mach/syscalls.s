/*
 * syscalls.c
 *	Actual entry points into the VSTa operating system
 */
#include <sys/syscall.h>
#include <sys/param.h>

/*
 * syserr()
 *	A hidden wrapper for system error string handling
 *
 * The point of this routine is NOT to get the error string.  We simply
 * ensure that any pending __errno changes are invalidated by the new
 * error status.  We also make sure that a flag is set to tell the libc
 * code that it should pick its next error string up from the kernel
 */
	.data
	.globl	__errno
	.globl	__old_errno
	.globl	__err_sync

	.bss
__faultframe:
	.space	4

	.text
	.align	4

syserr:	movl	$1, __err_sync
	push	%eax
	movl	__errno, %eax
	movl	%eax, __old_errno
	pop	%eax
	ret

/*
 * ENTHEAD()
 *	Prolog for making a system call
 *
 * Define our global C entry point, move system call number into EAX
 * where the kernel looks for it.
 */
#define ENTHEAD(n, v) \
	.globl	_##n ;\
_##n##:	movl	$((v) + 0x80),%eax

/*
 * ENTCALL()
 *	Actual system call
 *
 * Enter kernel for system call service via vector 0xFF
 */
#define ENTCALL \
	int	$0xFF

/*
 * ENTTAIL()
 *	Clean up after system call
 *
 * On return, kernel has set carry if there was an error.  If there
 * was an error, call our code to flag the error and cause it to be
 * picked up.  Otherwise just return.
 */
#define ENTTAIL \
	jc	syserr ;\
	ret

/*
 * ENTRY()
 *	System call entry when # args > 3
 *
 * For large numbers of arguments, we just leave the arguments on the
 * calling stack and the kernel picks them up from there.
 */
#define ENTRY(n, v) \
	ENTHEAD(n, v) ;\
	ENTCALL ;\
	ENTTAIL

/*
 * ENTRY0()
 *	System call when there are no arguments
 *
 * This is conceptually different from ENTRY(), although it's the
 * same code.  We leave all 0 arguments on the stack "in place".
 */
#define ENTRY0(n, v) ENTRY(n, v)

/*
 * ENTRY[123]()
 *	System call for 1..3 arguments
 *
 * We move the arguments into EBX..EDX (as appropriate, for the # of
 * arguments) and call the kernel.  These registers are not free, so we
 * have to push their contents and restore on the way back.
 */
#define ENTRY1(n, v) \
	ENTHEAD(n, v) ;\
	pushl	%ebx ;\
	movl	8(%esp),%ebx ;\
	ENTCALL ;\
	popl	%ebx ;\
	ENTTAIL

#define ENTRY2(n, v) \
	ENTHEAD(n, v) ;\
	pushl	%ecx ;\
	pushl	%ebx ;\
	movl	0xC(%esp),%ebx ;\
	movl	0x10(%esp),%ecx ;\
	ENTCALL ;\
	popl	%ebx ;\
	popl	%ecx ;\
	ENTTAIL

#define ENTRY3(n, v) \
	ENTHEAD(n, v) ;\
	pushl	%edx ;\
	pushl	%ecx ;\
	pushl	%ebx ;\
	movl	0x10(%esp),%ebx ;\
	movl	0x14(%esp),%ecx ;\
	movl	0x18(%esp),%edx ;\
	ENTCALL ;\
	popl	%ebx ;\
	popl	%ecx ;\
	popl	%edx ;\
	ENTTAIL

ENTRY2(msg_port, S_MSG_PORT)
ENTRY2(msg_connect, S_MSG_CONNECT)
ENTRY1(msg_accept, S_MSG_ACCEPT)
ENTRY2(msg_send, S_MSG_SEND)
ENTRY2(msg_receive, S_MSG_RECEIVE)
ENTRY2(msg_reply, S_MSG_REPLY)
ENTRY1(msg_disconnect, S_MSG_DISCONNECT)
ENTRY3(_msg_err, S_MSG_ERR)
ENTRY1(_exit, S_EXIT)
ENTRY0(_fork, S_FORK)
ENTRY2(tfork, S_THREAD)
ENTRY2(enable_io, S_ENABIO)
ENTRY2(enable_isr, S_ISR)
ENTRY(_mmap, S_MMAP)
ENTRY2(munmap, S_MUNMAP)
ENTRY1(_strerror, S_STRERROR)
ENTRY(_notify, S_NOTIFY)
ENTRY1(clone, S_CLONE)
ENTRY3(page_wire, S_PAGE_WIRE)
ENTRY1(page_release, S_PAGE_RELEASE)
ENTRY0(enable_dma, S_ENABLE_DMA)
ENTRY1(time_get, S_TIME_GET)
ENTRY1(time_sleep, S_TIME_SLEEP)
ENTRY0(dbg_enter, S_DBG_ENTER)
ENTRY3(exec, S_EXEC)
ENTRY2(waits, S_WAITS)
ENTRY3(perm_ctl, S_PERM_CTL)
ENTRY1(set_swapdev, S_SET_SWAPDEV)
ENTRY0(run_qio, S_RUN_QIO)
ENTRY1(set_cmd, S_SET_CMD)
ENTRY0(pageout, S_PAGEOUT)
ENTRY1(_getid, S_GETID)
ENTRY2(unhash, S_UNHASH)
ENTRY1(time_set, S_TIME_SET)
ENTRY2(ptrace, S_PTRACE)
ENTRY1(msg_portname, S_MSG_PORTNAME)
ENTRY(pstat, S_PSTAT)
ENTRY2(sched_op, S_SCHED_OP)
ENTRY0(setsid, S_SETSID)
ENTRY1(mutex_thread, S_MUTEX_THREAD)

/*
 * notify_handler()
 *	Insert a little assembly in front of C event handling
 *
 * The kernel calls the named routine with no saved context.  Put
 * an assembly front-line handler around the C routine, which saves
 * and restore context.
 */
	.data
c_handler: .space	4
	.text
asm_handler:
	pusha				/* Save state */
	pushf
	movl	%esp,__faultframe	/* Mark frame for debugging ease */
	lea	0x24(%esp),%eax		/* Point to event string */
	movl	0xC(%esp),%ebx		/* Get old EBP value */
	movl	(EVLEN+0x24)(%esp),%ecx	/* Get old EIP value */
	pushl	%ecx			/* Create a call-like frame: EIP */
	pushl	%ebx			/*   ...EBP */
	movl	%esp,%ebp		/*   ...point EBP to new "frame" */
	push	%eax			/* Event string is arg to routine */
	movl	c_handler,%eax
	call	%eax
	lea	0xC(%esp),%esp		/* Drop args on stack */
	popf				/* Restore state */
	popa
	lea	EVLEN(%esp),%esp	/* Drop event string */
	ret				/* Resume at old IP */

	.globl	_notify_handler
_notify_handler:
	movl	4(%esp),%eax		/* Get func pointer */
	movl	%eax,c_handler		/* Save in private space */
	orl	%eax,%eax		/* Special handling for NULL */
	jnz	1f
	movl	%eax,%ebx		/* Deregister event handler */
	movl	$(S_NOTIFY_HANDLER + 0x80),%eax
	ENTCALL
	jmp	2f

1:	pushl	%ebx			/* Single arg via EBX register */
	movl	$asm_handler,%ebx	/* Vector to assembly handler */
	movl	$(S_NOTIFY_HANDLER + 0x80),%eax
	ENTCALL
	popl	%ebx

2:	ENTTAIL
