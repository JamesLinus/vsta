#ifndef _MACH_LOCORE_H
#define _MACH_LOCORE_H
/*
 * locore.h
 *	Inlined i386 processor specific functions
 *
 * Note that the "\n" and "\t" terminators at the end of the assembly
 * opcodes are simply there to prettify the assembly output from the
 * compiler - if you compile using gcc and use the "-S" switch you'll
 * see what I mean :-)
 */
#include <mach/setjmp.h>
#include <mach/machreg.h>
#include <mach/vm.h>
#include <sys/percpu.h>
#include <sys/thread.h>

extern int cpfail(void);

/*
 * set_cr0()
 *	Set the value of the processor config register cr0
 */
inline static void
set_cr0(ulong addr)
{
	__asm__ __volatile__(
		"movl %0, %%cr0\n\t"
		: /* No output */
		: "r" (addr));
}

/*
 * set_cr3()
 *	Set the value of the processor config register cr3 - the L1 page
 *	table pointer
 */
inline static void
set_cr3(ulong addr)
{
	__asm__ __volatile__(
		"movl %0, %%cr3\n\t"
		: /* No output */
		: "r" (addr)); 
}

/*
 * get_cr3()
 *	Return the value of the processor config register cr0
 */
inline static ulong
get_cr0(void)
{
	register ulong res;
	
	__asm__ __volatile__(
		"movl %%cr0, %0\n\t"
		: "=r" (res)
		: /* No input */); 
	return(res);
}

/*
 * get_cr3()
 *	Get the value of the processor config register cr2 - the fault
 *	address register
 */
inline static ulong
get_cr2(void)
{
	register ulong res;
	
	__asm__ __volatile__(
		"movl %%cr2, %0\n\t"
		: "=r" (res)
		: /* No input */); 
	return(res);
}

/*
 * get_cr3()
 *	Get the value of the processor config register cr3 - the L1 page
 *	table pointer
 */
inline static ulong
get_cr3(void)
{
	register ulong res;
	
	__asm__ __volatile__(
		"movl %%cr3, %0\n\t"
		: "=r" (res)
		: /* No input */); 
	return(res);
}

/*
 * flush_tlb()
 *	Flush the processor page table "translation lookaside buffer"
 */
inline static void
flush_tlb(void)
{
	__asm__ __volatile__ (
		"movl %%cr3, %%eax\n\t"
		"movl %%eax, %%cr3\n\t"
		: /* No output */
		: /* No input */
		: "ax");
}

/*
 * lgdt()
 *	Load the global descriptor table register
 */
inline static void
lgdt(void *gdt_base)
{
	__asm__ __volatile__(
		"lgdt (%%eax)\n\t"
		"jmp 1f\n"
		"1:\n\t"
		: /* No output */
		: "a" (gdt_base));
}

/*
 * lidt()
 *	Load the interrupt descriptor table register
 */
inline static void
lidt(void *idt_base)
{
	__asm__ __volatile__(
		"lidt (%%eax)\n\t"
		"jmp 1f\n"
		"1:\n\t"
		: /* No output */
		: "a" (idt_base));
}

/*
 * ltr()
 *	Load the task register
 */
inline static void
ltr(uint tr_base)
{
	__asm__ __volatile__(
		"ltr %%eax\n\t"
		"jmp 1f\n"
		"1:\n\t"
		: /* No output */
		: "a" (tr_base));
}

/*
 * cli()
 *	Disable the maskable processor interrupts.
 */
inline static void
cli(void)
{
	__asm__ __volatile__(
		"cli\n\t"
		: /* No output */
		: /* No input */);
}

/*
 * sti()
 *	Enable maskable interrupts
 */
inline static void
sti(void)
{
	__asm__ __volatile__(
		"sti\n\t"
		: /* No output */
		: /* No input */);
}

/*
 * geti()
 *	Get the mask setting for the maskable interrupts
 *
 * We return the result directly in the form required for a spinlock op
 */
inline static uint
geti(void)
{
	register uint res;

	__asm__ __volatile__(
		"pushfl\n\t"
		"popl %0\n\t"
		"andl $0x200,%0\n\t"
		"shrl $2,%0\n\t"
		: "=r" (res)
		: /* No input */);
}

/*
 * inportb()
 *	Get a byte from an I/O port
 */
inline static uchar
inportb(int port)
{
	register uchar res;
	
	__asm__ __volatile__(
		"inb %%dx,%%al\n\t"
		: "=a" (res)
		: "d" (port));
	return(res);
}

/*
 * outportb()
 *	Write a byte to an I/O port
 */
inline static void
outportb(int port, uchar data)
{
	__asm__ __volatile__(
		"outb %%al,%%dx\n\t"
		: /* No output */
		: "a" (data), "d" (port));
}

/*
 * idle_stack()
 *	Switch to using the idle stack
 */
inline static void
idle_stack(void)
{
	extern void *id_stack;

	__asm__ __volatile__ (
		"movl $_id_stack,%%esp\n\t"
		"movl %%esp,%%ebp\n\t"
		: /* No output */
		: /* No input */);
}

/*
 * idle()
 *	Run idle - do nothing except wait for something to happen :-)
 *
 * We watch for num_run to go non-zero; we use sti/halt to atomically
 * enable interrupts and halt the CPU--this saves a fair amount of power
 * and heat.
 */
inline static void
idle(void)
{
	__asm__ __volatile__ (
		"movl $_num_run,%%eax\n\t"
		"movl $0,%%edx\n"
		"1:\t"
		"cmpl %%edx,(%%eax)\n\t"
		"jne 2f\n\t"
		"hlt\n\t"
		"jmp 1b\n\t"
		".align 2,0x90\n"
		"2:\n\t"
		: /* No output */
		: /* No input */
		: "ax", "dx");
}

/*
 * setjmp()
 *	Save context, returning 0
 *
 * We don't bother to save registers whose value we know at the end of
 * a matching longjmp (eax and edi)
 */
inline static int
setjmp(jmp_buf regs)
{
	register int retcode;

	__asm__ __volatile__ (
		"movl $1f,(%%edi)\n\t"
		"movl %%esi,8(%%edi)\n\t"
		"movl %%esp,%%eax\n\t"
		"movl %%ebp,12(%%edi)\n\t"
		"subl $4,%%eax\n\t"
		"movl %%eax,16(%%edi)\n\t"
		"xorl %%eax,%%eax\n\t"
		"movl %%ebx,20(%%edi)\n\t"
		"movl %%edx,24(%%edi)\n\t"
		"movl %%ecx,28(%%edi)\n"
		"1:\n\t"
		: "=a" (retcode)
		: "D" (regs));
	return(retcode);
}

/*
 * longjmp()
 *	Restore context, returning a specified result
 */
inline static void
longjmp(jmp_buf regs, int retval)
{
	__asm__ __volatile__ (
		"movl 16(%%edi),%%esp\n\t"
		"movl 12(%%edi),%%ebp\n\t"
		"movl 8(%%edi),%%esi\n\t"
		"movl (%%edi),%%edx\n\t"
		"movl %%edx,(%%esp)\n\t"
		"movl 20(%%edi),%%ebx\n\t"
		"movl 24(%%edi),%%edx\n\t"
		"movl 28(%%edi),%%ecx\n\t"
		"ret\n\t"
		: /* No output */
		: "D" (regs), "a" (retval));
}

/*
 * copyin()
 *	Copy data from user to kernel space
 *
 * This is really just a frontend to the real copy function, but
 * using this we pass the parameters in registers so it's a little quicker
 */
inline static int
copyin(void *uaddr, void *sysaddr, uint nbyte)
{
	register int retval;
	struct thread *t = curthread;
	extern int __copyin();

	t->t_probe = cpfail;
	__asm__ __volatile__ (
		"call ___copyin\n\t"
		: "=a" (retval)
		: "S" (uaddr), "D" (sysaddr), "c" (nbyte)
		: "si", "di", "cx");
	t->t_probe = 0;
	return(retval);
}

/*
 * copyout()
 *	Copy data from kernel to user space
 *
 * This is really just a frontend to the real copy function, but
 * using this we pass the parameters in registers so it's a little quicker
 */
inline static int
copyout(void *uaddr, void *sysaddr, uint nbyte)
{
	register int retval;
	struct thread *t = curthread;
	extern int __copyout();

	t->t_probe = cpfail;
	__asm__ __volatile__ (
		"call ___copyout\n\t"
		: "=a" (retval)
		: "S" (sysaddr), "D" (uaddr), "c" (nbyte)
		: "si", "di", "cx");
	t->t_probe = 0;
	return(retval);
}

/*
 * uucopy()
 *	Copy bytes from one user space to another
 *
 * This is used to copy memory from a message reply after attaching
 * it to the user's address space.
 *
 * We just map the "from" address into user space, then let copyout()
 * do all the work.  This means we trust "from", but since it's a
 * system-generated address, this should be OK.
 */
inline static int
uucopy(void *uaddr, void *sysaddr, uint count)
{
	return(copyout(uaddr, (void *)((ulong)sysaddr | UOFF), count));
}

#endif /* _MACH_LOCORE_H */
