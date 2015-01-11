/*
 * lock.s
 *	Spinlocks for user level mutexes
 */

	.globl	_p_lock,_yield
_p_lock:
	movl	4(%esp),%eax
	bts	$0,(%eax)
	jc	1f
	ret
1:	call	_yield
	jmp	_p_lock
