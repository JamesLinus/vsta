/*
 * locore.s
 *	Assembly support for VSTa
 *
 * We handle the really fast functions as inline assembly rather than
 * here, but some things such as interrupt vectors are better off being
 * done here.  Some of the code alignments (done with .fill operands
 * say) have been determined with arcane methods (:-)) to ensure the
 * best cache performance on a 486.  These should also help 386's, and
 * Pentium class processors won't notice so that's alright too!
 */
#include "assym.h"

/* Current thread executing */
	.globl	_cpu
#define CURTHREAD (_cpu+PC_THREAD)

	.data
	.globl	_free_pfn,_size_ext,_size_base,_boot_pfn

_free_pfn: .space	4	/* PFN of first free page beyond data */
_size_base: .space	4	/* # pages of base (< 640K) memory */
_size_ext:  .space	4	/*  ... extended (> 1M) memory */
_boot_pfn:  .space	4	/* PFN of first boot task */
mainretmsg:
	.asciz	"main returned"

	.align	4
	.space	0x1000

_id_stack:
	.space	0x40		/* Slop */
_id_top:
	.globl	_id_stack,_id_top

/*
 * Entered through 32-bit task gate constructed in 16-bit mode
 *
 * Our parameters are passed on the stack, which is located
 * in high memory.  Before any significant memory use occurs,
 * we must switch it down to a proper stack.
 */
	.text
	.globl	_start,_main,_panic
	.align	4

#define GETP(var) popl %eax ; movl %eax,_##var

_start:
	GETP(free_pfn)
	GETP(size_ext)
	GETP(size_base)
	GETP(boot_pfn);
#undef GETP

	/*
	 * Call our first C code on the idle stack
	 */
	movl	$_id_stack,%esp
	movl	$_id_stack,%ebp
	call	_main
1:	pushl	$mainretmsg
	call	_panic
	jmp	1b

/*
 * fpu_disable()
 *	Disable FPU access, save current state if have pointer
 */
	.globl	_fpu_disable
_fpu_disable:
	movl	4(%esp),%eax
	orl	%eax,%eax
	jz	1f
	fnsave	(%eax)
1:	movl	%cr0,%eax
	orl	$(CR0_EM),%eax
	movl	%eax,%cr0
	ret

/*
 * fpu_enable()
 *	Enable FPU access, load current state if have pointer
 */
	.globl	_fpu_enable
	.align	4

_fpu_enable:
	movl	%cr0,%eax	/* Turn on access */
	andl	$~(CR0_EM|CR0_TS),%eax
	movl	%eax,%cr0
	movl	4(%esp),%eax
	orl	%eax,%eax
	jz	_fpu_init	/* No FPU state--init FPU instead */
	frstor	(%eax)		/* Load old FPU state */
	ret

/*
 * fpu_init()
 *	Clear FPU state to its basic form
 */
	.globl	_fpu_init
	.align	4

_fpu_init:
	fnclex
	fninit
	ret

/*
 * fpu_detected()
 *	Tell if an FPU is present on this CPU
 *
 * Note, if you have an i287 on your system, you deserve everything
 * you're about to get.
 */
	.globl	_fpu_detected
	.align	4

_fpu_detected:
	fninit
	fstsw	%ax		/* See if an FP operation happens */
	orb	%al,%al
	je	1f
	xorl	%eax,%eax	/* Nope */
	ret

1:	xorl	%eax,%eax	/* Yup */
	incl	%eax
	ret

/*
 * fpu_maskexcep()
 *	Mask out pending exceptions
 */
	.globl	_fpu_maskexcep
	.align	4

_fpu_maskexcep:
	leal	-4(%esp),%esp		/* Put ctl word on stack */
	fnstcw	(%esp)
	fnstsw	%ax			/* Status word -> AX */
	andw	$0x3f,%ax		/* Clear pending exceps */
	orw	%ax,(%esp)
	fnclex
	fldcw	(%esp)			/* Load new ctl word */
	leal	4(%esp),%esp		/* Free temp storage */
	ret

/*
 * reload_dr()
 *	Load db0..3 and db7 from the "struct dbg_regs" in machreg.h
 */
	.globl	_reload_dr
	.align	4

_reload_dr:
	pushl	%ebx
	movl	8(%esp),%ebx
	movl	0(%ebx),%eax
	movl	%eax,%db0
	movl	4(%ebx),%eax
	movl	%eax,%db1
	movl	8(%ebx),%eax
	movl	%eax,%db2
	movl	0xC(%ebx),%eax
	movl	%eax,%db3
	movl	0x10(%ebx),%eax
	movl	%eax,%db7
	popl	%ebx
	ret

/*
 * _cpfail()
 *	A copy operation involving user-space has failed
 */
	.globl	_cpfail
	.align	4

_cpfail:
	movl	$-1,%eax
	ret

/*
 * __cpalign
 *	Handle the last parts of the unaligned data in copy ops
 */
	.align	4

___cpalign:
	testl	$2,%eax
	jz	1f
	movsw
1:	testl	$1,%eax
	jz	2f
	movb	(%esi),%al
	movb	%al,(%edi)
2:	xorl	%eax,%eax
	ret

/*
 * __copyin()
 *	Copy data from user to kernel space
 */
	.globl	___copyin
	.align	4

___copyin:
	addl	$0x80000000,%esi
	jc	_cpfail
	movl	%ecx,%eax
	cmpl	$0x40000000,%eax
	jae	_cpfail
	addl	%esi,%eax
	jc	_cpfail
	shrl	$2,%ecx
	rep
	movsl
	subl	%esi,%eax
	jnz	___cpalign
	ret

/*
 * copyout()
 *	Copy data from kernel to user space
 */
	.globl	___copyout
	.align	4

___copyout:
	addl	$0x80000000,%edi
	jc	_cpfail
	movl	%ecx,%eax
	cmpl	$0x40000000,%eax
	jae	_cpfail
	addl	%edi,%eax
	jc	_cpfail
	shrl	$2,%ecx
	rep
	movsl
	subl	%edi,%eax
	jnz	___cpalign
	ret

/*
 * Common macros to force segment registers to appropriate values
 */
#define SAVE_SEGS \
	pushw %ds ; \
	pushw %es

#define RESTORE_SEGS \
	popw %es ; \
	popw %ds

#define SET_KSEGS \
	movw $(GDT_KDATA),%ax ; \
	mov %ax,%ds ; \
	mov %ax,%es

/*
 * stray_intr()
 *	Handling of any vectors for which we have no handler
 *
 * For a more complete description of what this code is doing see the
 * trap_common code below
 */
	.globl	_stray_intr,_stray_interrupt
	.align	4
	.fill	9,1,0x90

_stray_intr:
	cld
	pushl	$0
	pushl	$0
	pushal
	SAVE_SEGS
	SET_KSEGS
	call	_stray_interrupt
	RESTORE_SEGS
	popal
	addl	$8,%esp
	iret

/*
 * trap_common()
 *	Common code for all traps
 *
 * At this point all the various traps and exceptions have been moulded
 * into a single stack format--a OS-type trap number, an error code (0
 * for those which don't have one), and then the saved registers followed
 * by a trap-type stack frame suitable for iret'ing.
 */
	.align	4
	.fill	5,1,0x90

trap_common:
	/*
	 * Save the user's registers, ensure that we have the direction
	 * flag set appropriately and establish the kernel selectors
	 */
	cld
	pushal
	SAVE_SEGS
	SET_KSEGS

	/*
	 * Call C-code for our common trap()
	 */
	call	_trap

	/*
	 * May as well share the code for this...
	 */
	.globl	_retuser
_retuser:

	/*
	 * Get registers back, drop the OS trap type and error number
	 */
	RESTORE_SEGS
	popal
	addl	$8,%esp

	/*
	 * Back to whence we came...
	 */
	iret

/*
 * Templates for entry handling.  IDTERR() is for entries which
 * already have an error code supplied by the i386.  IDT() is for
 * those which don't--we push a dummy 0.
 */
#define IDT(n, t) \
	.globl	_##n ; \
	.align	4 ; \
	_##n##: ; \
	pushl	$0 ; \
	pushl	$(t) ; \
	jmp trap_common

#define IDTERR(n, t) \
	.globl	_##n ; \
	.align	4 ; \
	_##n##: ; \
	pushl	$(t) ; \
	jmp trap_common

/*
 * The vectors we handle
 */
IDT(Tdiv, T_DIV)
IDT(Tdebug, T_DEBUG)
IDT(Tnmi, T_NMI)
IDT(Tbpt, T_BPT)
IDT(Tovfl, T_OVFL)
IDT(Tbound, T_BOUND)
IDT(Tinstr, T_INSTR)
IDT(T387, T_387)
IDTERR(Tdfault, T_DFAULT)
IDTERR(Tcpsover, T_CPSOVER)
IDTERR(Tinvtss, T_INVTSS)
IDTERR(Tseg, T_SEG)
IDTERR(Tstack, T_STACK)
IDTERR(Tgenpro, T_GENPRO)
IDT(Tnpx, T_NPX)

/*
 * Tpgflt()
 *	A fast handler for page faults
 */
	.globl	_Tpgflt,_page_fault
	.align	4
	.fill 11,1,0x90

_Tpgflt:
	cld
	pushl	$(T_PGFLT)
	pushal
	SAVE_SEGS
	SET_KSEGS
	call	_page_fault
	RESTORE_SEGS
	popal
	addl	$8,%esp
	iret

/*
 * Tsyscall()
 *	A fast handler for system calls
 */
	.globl	_Tsyscall,_syscall
	.align	4
	.fill	9,1,0x90

_Tsyscall:
	cld
	pushl	$0
	pushl	$(T_SYSCALL)
	pushal
	SAVE_SEGS
	SET_KSEGS
	call	_syscall
	RESTORE_SEGS
	popal
	addl	$8,%esp
	iret

/*
 * stray_ign()
 *	A fast path for ignoring known stray interrupts
 *
 * Currently used for IRQ7, even when lpt1 is masked on the PIC!
 */
	.globl	_stray_ign
	.align	4

_stray_ign:
	iret

/*
 * INTVEC()
 *	Macro to set up trap frame for a hardware interrupt
 *
 * We waste the extra pushl to make it look much like a trap frame.
 * This radically simplifies things and makes it easier on the kernel
 * debugger :-)  We push the IRQ number, not the interrupt vector
 * number as this saves some time in the interrupt handler
 */
#define INTVEC(n) \
	.globl	_xint##n ; \
	.align	4 ; \
	.fill	9,1,0x90 ; \
_xint##n##: ; \
	cld ; \
	pushl	$0 ; \
	pushb	$(n - T_EXTERN) ; \
	pushal ; \
	SAVE_SEGS ; \
	SET_KSEGS ; \
	call	_interrupt ; \
	RESTORE_SEGS ; \
	popal ; \
	addl	$8,%esp ; \
	iret

INTVEC(32); INTVEC(33); INTVEC(34); INTVEC(35)
INTVEC(36); INTVEC(37); INTVEC(38); INTVEC(39)
INTVEC(40); INTVEC(41); INTVEC(42); INTVEC(43)
INTVEC(44); INTVEC(45); INTVEC(46); INTVEC(47)

	.align 4
