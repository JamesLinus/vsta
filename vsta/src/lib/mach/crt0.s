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
	.globl	___iob,___ctab
___iob:	.long	0	/* Data "shared" with libc users */
___ctab: .long	0
	.text

	.globl	start,_main,_exit
start:

/*
 * Special code for boot servers.  This is pretty ugly, but the benefits
 * are large.  We put a static data area at the front of boot server
 * a.out's, so that the boot loader program boot.exe can fill this
 * area with an initial argument string, argc, and argv.
 */
#ifdef SRV

/* These are made available by putting them into an initialized area */
#define MAXNARG 8	/* Max # arguments */
#define MAXARG 64	/* Max # bytes of argument string */

	.globl	___bootargc,___bootargv,___bootarg
	jmp	2f		/* Skip this data area */
	nop ; nop		/* For future expansion */
	nop ; nop ; nop ; nop
___bootargc:
	.long	0		/* argc */
___bootargv:
	.long	0xDEADBEEF
	.word	MAXNARG,MAXARG
	.space	((MAXNARG-2)*4)	/* argv */
___bootarg:
	.space	MAXARG		/*  ...rest of arg space */
2:
#endif /* SRV */

	pushl	$argv
	pushl	$argc
#ifdef SRV
	.globl	___start2
	call	___start2	/* Need a special version to understand */
#else				/*  the boot string rea */
	.globl	___start
	call	___start
#endif
	addl	$8,%esp
	call	___get_iob	/* Get iob and ctab array values */
	movl	%eax,___iob
	call	___get_ctab
	movl	%eax,___ctab
	pushl	argv		/* Call main(argc, argv) */
	pushl	argc
	call	_main
	addl	$8,%esp
	pushl	%eax
1:	call	_exit		/* exit() with value returned from main() */
	movl	$0,(%esp)
	jmp	1b

/* Dummy for GCC 2.X hooks into C++ */
	.globl	___main
___main:
	ret
