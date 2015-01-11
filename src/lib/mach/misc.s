/*
 * getpagesize()
 *	Easy to answer here, eh?
 */
	.text
	.globl	_getpagesize
_getpagesize:
	movl	$0x1000,%eax
	ret

/*
 * cli
 *	Disable maskable processor interrupts
 */
	.globl	_cli
_cli:
	cli
	ret

/*
 * sti
 *	Enable maskable processor interrupts
 */
	.globl	_sti
_sti:
	sti
	ret
