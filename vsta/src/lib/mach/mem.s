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
	cmpl	$4,%ecx			/* Zero longwords */
	jb	1f
	shrl	$2,%ecx			/* Scale to longword count */
	cld
	rep
	stosl
	movl	0x10(%esp),%ecx		/* Calculate byte resid */
	andl	$3,%ecx
	jz	2f
1:	cld
	rep				/* Zero bytes */
	stosb
2:	popl	%ecx
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

	cmpl	%edi,%esi	/* Potential ripple in copy? */
	jb	2f

1:	cmpl	$4,%ecx		/* Move longwords */
	jb	5f
	shrl	$2,%ecx		/* Scale units */
	cld
	rep
	movsl
	movl	0x18(%esp),%ecx
	andl	$3,%ecx
	jz	3f

5:	cld
	rep			/* Resid copy of bytes */
	movsb

3:	popl	%ecx		/* Restore registers and return */
	popl	%edi
	popl	%esi
	ret

2:	addl	%ecx,%esi	/* If no overlap, still copy forward */
	cmpl	%edi,%esi
	jae	4f
	movl	0x10(%esp),%esi	/* Restore register */
	jmp	1b		/* Forward copy */

4:				/* Overlap; copy backwards */
	std
	/* addl	%ecx,%esi	Done in overlap check */
	decl	%esi
	addl	%ecx,%edi
	decl	%edi
	rep
	movsb
	cld
	jmp	3b
