/*
 * syscalls.c
 *	Actual entry points into the VSTa operating system
 *
 * We keep the return address in a register so that tfork() will
 * return correctly even though the child thread returns on a new
 * stack.
 */
#include <sys/syscall.h>

/*
 * syserr()
 *	A hidden wrapper for system error string handling
 *
 * The point of this routine is NOT to get the error string.  We simply
 * ensure that any pending __errno changes are invalidated by the new
 * error status.  We also make sure that a flag is set to tell the libc
 * code that it should pick it's next error string up from the kernel
 */
	.data
	.globl	__errno
	.globl	__old_errno
	.globl	__err_sync
	.text

syserr:	movl	$1, __err_sync
	push	%eax
	movl	__errno, %eax
	movl	%eax, __old_errno
	pop	%eax
	ret

#define ENTRY(n, v)	.globl	_##n ; \
	_##n##: movl $(v),%eax ; int $0xFF ; jc syserr; ret

ENTRY(msg_port, S_MSG_PORT)
ENTRY(msg_connect, S_MSG_CONNECT)
ENTRY(msg_accept, S_MSG_ACCEPT)
ENTRY(msg_send, S_MSG_SEND)
ENTRY(msg_receive, S_MSG_RECEIVE)
ENTRY(msg_reply, S_MSG_REPLY)
ENTRY(msg_disconnect, S_MSG_DISCONNECT)
ENTRY(_msg_err, S_MSG_ERR)
ENTRY(_exit, S_EXIT)
ENTRY(fork, S_FORK)
ENTRY(tfork, S_THREAD)
ENTRY(enable_io, S_ENABIO)
ENTRY(enable_isr, S_ISR)
ENTRY(_mmap, S_MMAP)
ENTRY(munmap, S_MUNMAP)
ENTRY(_strerror, S_STRERROR)
ENTRY(_notify, S_NOTIFY)
ENTRY(clone, S_CLONE)
ENTRY(page_wire, S_PAGE_WIRE)
ENTRY(page_release, S_PAGE_RELEASE)
ENTRY(enable_dma, S_ENABLE_DMA)
ENTRY(time_get, S_TIME_GET)
ENTRY(time_sleep, S_TIME_SLEEP)
ENTRY(dbg_enter, S_DBG_ENTER)
ENTRY(exec, S_EXEC)
ENTRY(waits, S_WAITS)
ENTRY(perm_ctl, S_PERM_CTL)
ENTRY(set_swapdev, S_SET_SWAPDEV)
ENTRY(run_qio, S_RUN_QIO)
ENTRY(set_cmd, S_SET_CMD)
ENTRY(pageout, S_PAGEOUT)
ENTRY(_getid, S_GETID)
ENTRY(unhash, S_UNHASH)
ENTRY(time_set, S_TIME_SET)
ENTRY(ptrace, S_PTRACE)
ENTRY(msg_portname, S_MSG_PORTNAME)
ENTRY(pstat, S_PSTAT)
ENTRY(sched_op, S_SCHED_OP)

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
	lea	0x28(%esp),%eax		/* Point to event string */
	push	%eax			/* Leave as arg to routine */
	movl	c_handler,%eax
	call	%eax
	lea	4(%esp),%esp		/* Drop arg */
	popf				/* Restore state */
	popa
	pop	%esp			/* Skip event string */
	ret				/* Resume at old IP */

	.globl	_notify_handler
_notify_handler:
	movl	4(%esp),%eax		/* Get func pointer */
	movl	%eax,c_handler		/* Save in private space */
	movl	$asm_handler,%eax	/* Vector to assembly handler */
	movl	%eax,4(%esp)
	movl	$(S_NOTIFY_HANDLER),%eax
	int	$0xFF
	jc	syserr
	ret
