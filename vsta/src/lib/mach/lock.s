/*
 * lock.s
 *	Spinlocks for user level mutexes
 */

	.globl	_p_lock,___msleep
_p_lock:
	movl	4(%esp),%eax
	bts	$0,(%eax)
	jc	1f
	ret
1:	pushl	$10
	call	___msleep
	popl	%eax
	jmp	_p_lock
