/*
 * setjmp.s
 *	Routines for saving and restoring a context
 */

/*
 * setjmp()
 *	Save context, return 0
 */
	.globl	_setjmp
_setjmp:
	pushl	%edi
	movl	8(%esp),%edi		/* jmp_buf pointer */
	movl	4(%esp),%eax		/* saved eip */
	movl	%eax,R_EIP(%edi)
	popl	%eax			/* edi's original value */
	movl	%eax,R_EDI(%edi)
	movl	%esi,R_ESI(%edi)	/* esi,ebp,esp,ebx,edx,ecx,eax */
	movl	%ebp,R_EBP(%edi)
	movl	%esp,R_ESP(%edi)
	movl	%ebx,R_EBX(%edi)
	movl	%edx,R_EDX(%edi)
	movl	%ecx,R_ECX(%edi)
/*	movl	%eax,R_EAX(%edi) Why bother? */
	xorl	%eax,%eax
	ret

/*
 * longjmp()
 *	Restore context, return second argument as value
 */
	.globl	_longjmp
_longjmp:
	movl	4(%esp),%edi		/* jmp_buf */
	movl	8(%esp),%eax		/* return value (eax) */
	movl	%eax,R_EAX(%edi)
	movl	R_ESP(%edi),%esp	/* switch to new stack position */
	movl	R_EIP(%edi),%eax	/* get new ip value */
	movl	%eax,(%esp)
	movl	R_ESI(%edi),%esi	/* esi,ebp,ebx,edc,ecx,eax */
	movl	R_EBP(%edi),%ebp
	movl	R_EBX(%edi),%ebx
	movl	R_EDX(%edi),%edx
	movl	R_ECX(%edi),%ecx
	movl	R_EAX(%edi),%eax
	movl	R_EDI(%edi),%edi	/* get edi last */
	ret
