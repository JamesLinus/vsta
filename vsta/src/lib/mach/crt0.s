/*
 * crt0.s
 *	C Run-Time code at 0
 *
 * Not really at 0 any more, but the name sticks.  Interestingly, the
 * orginal magic numbers (407, 411, 413, etc.--octal, you understand)
 * were PDP-11 jump instructions.  Their presence at location 0 not
 * only allowed the exec() system call to determine the type of the
 * executable; the a.out was always run at 0 and the jump would then
 * skip over the rest of the header in memory and start at the right
 * place.  Since 413 was a demand-paged format, and the PDP-11 never
 * got pages, this likely doesn't hold true for this value.
 *
 * Anyway.  For us, we need to point to the well-known location for
 * the argv array, and pull its length onto the stack as well.  Then
 * we simply fire up the user's code.
 */
	.data
argc:	.long	0	/* Private variables for __start() to fill in */
argv:	.long	0
	.text

	.globl	start,___start,_main,_exit
start:
	pushl	$argv
	pushl	$argc
	call	___start
	addl	$8,%esp
	pushl	argv
	pushl	argc
	call	_main
	addl	$8,%esp
	pushl	%eax
1:	call	_exit
	movl	$0,(%esp)
	jmp	1b
