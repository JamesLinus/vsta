/*
 * mem.s
 *	Memory operations worth coding in assembler for speed
 */

/*
 * bzero()
 *	Zero some memory
 */
	.globl	_bzero
_bzero:	pushl	%edi
	pushl	%ecx
	movl	0xC(%esp),%edi
	movl	0x10(%esp),%ecx
	xorl	%eax,%eax
	rep
	stosb
	popl	%ecx
	popl	%edi
	ret

/*
 * bcopy()
 *	Copy some memory
 */
	.globl	_bcopy
_bcopy:	pushl	%esi
	pushl	%edi
	pushl	%ecx
	movl	0x10(%esp),%esi
	movl	0x14(%esp),%edi
	movl	0x18(%esp),%ecx
	cmpl	%edi,%esi
	jb	2f
	rep		/* No ripple in forward copy */
	movsb

3:	popl	%ecx	/* Restore registers and return */
	popl	%edi
	popl	%esi
	ret

2:			/* Potential ripple in forward; copy backwards */
	std
	addl	%ecx,%esi
	decl	%esi
	addl	%ecx,%edi
	decl	%edi
	rep
	movsb
	cld
	jmp	3b

/*
 * getpagesize()
 *	Easy to answer here, eh?
 */
	.text
	.globl	_getpagesize
_getpagesize:
	movl	$0x1000,%eax
	ret
