/*
 * io.s
 *	Routines for accessing i386 I/O ports
 */

/*
 * inportb()
 *	Get a byte from an I/O port
 */
	.globl	_inportb
_inportb:
	movl	4(%esp),%edx
	xorl	%eax,%eax
	inb	%dx,%al
	ret

/*
 * outportb()
 *	Write a byte to an I/O port
 */
	.globl	_outportb
_outportb:
	movl	4(%esp),%edx
	movl	8(%esp),%eax
	outb	%al,%dx
	ret
/*
 * inportw()
 *	Get a word from an I/O port
 */
	.globl	_inportw
_inportw:
	movl	4(%esp),%edx
	xorl	%eax,%eax
	inw	%dx,%ax
	ret

/*
 * outportw()
 *	Write a word to an I/O port
 */
	.globl	_outportw
_outportw:
	movl	4(%esp),%edx
	movl	8(%esp),%eax
	outw	%ax,%dx
	ret

/*
 * repinsw(port, buffer, count)
 *	Read a bunch of words from an I/O port
 */
	.globl	_repinsw
_repinsw:
	pushl	%edi
	movl	0x8(%esp),%edx
	movl	0xC(%esp),%edi
	movl	0x10(%esp),%ecx
	rep
	insw
	popl	%edi
	ret

/*
 * repoutsw(port, buffer, count)
 *	Write a bunch of words to an I/O port
 */
	.globl	_repoutsw
_repoutsw:
	pushl	%esi
	movl	0x8(%esp),%edx
	movl	0xC(%esp),%esi
	movl	0x10(%esp),%ecx
	rep
	outsw
	popl	%esi
	ret
