/*
 * getpagesize()
 *	Easy to answer here, eh?
 */
	.text
	.globl	_getpagesize
_getpagesize:
	movl	$0x1000,%eax
	ret
