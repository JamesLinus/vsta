/*
 * genassym.c
 *	Convert C identifiers into assembler declarations
 */
#include <mach/vm.h>
#include <mach/machreg.h>
#include <sys/thread.h>
#include <sys/percpu.h>
#include <mach/trap.h>

#define CONST(n) printf("#define %s 0x%x\n", #n, n);
#define OFF(base, field, name) \
	printf("#define %s 0x%x\n", #name, (char *)&(field) - (char *)&base);
main()
{
	jmp_buf r;
	struct thread t;
	struct percpu pc;

	/*
	 * Simple constants
	 */
	CONST(GDT_KDATA);
	CONST(GDT_KTEXT);
	CONST(GDT_UDATA);
	CONST(GDT_UTEXT);
	CONST(NBPG);
	CONST(PGSHIFT);

	/*
	 * Fields in a jmp_buf
	 */
	OFF(r, r[0].eip, R_EIP);
	OFF(r, r[0].edi, R_EDI);
	OFF(r, r[0].esi, R_ESI);
	OFF(r, r[0].ebp, R_EBP);
	OFF(r, r[0].esp, R_ESP);
	OFF(r, r[0].ebx, R_EBX);
	OFF(r, r[0].edx, R_EDX);
	OFF(r, r[0].ecx, R_ECX);
	OFF(r, r[0].eax, R_EAX);

	/*
	 * Thread fields
	 */
	OFF(t, t.t_uregs, T_UREGS);
	OFF(t, t.t_kregs, T_KREGS);
	OFF(t, t.t_kstack, T_KSTACK);
	OFF(t, t.t_probe, T_PROBE);

	/*
	 * Per-CPU fields
	 */
	OFF(pc, pc.pc_thread, PC_THREAD);

	/*
	 * Trap types
	 */
	CONST(T_DIV); CONST(T_DEBUG); CONST(T_NMI); CONST(T_BPT);
	CONST(T_OVFL); CONST(T_BOUND); CONST(T_INSTR); CONST(T_387);
	CONST(T_DFAULT); CONST(T_INVTSS); CONST(T_SEG); CONST(T_STACK);
	CONST(T_GENPRO); CONST(T_PGFLT); CONST(T_NPX); CONST(T_SYSCALL);

	return(0);
}
