/*
 * genassym.c
 *	Convert C identifiers into assembler declarations
 */
#include <mach/vm.h>
#include <mach/machreg.h>
#include <sys/thread.h>
#include <sys/percpu.h>
#include <mach/trap.h>
#include <fcntl.h>

static char buf[256];
static int outfd;

#define CONST(n) sprintf(buf, "#define %s 0x%x\n", #n, n), out(buf)
#define OFF(base, field, name) sprintf(buf, "#define %s 0x%x\n", #name, \
		(char *)&(field) - (char *)&base), \
	out(buf)

/*
 * out()
 *	Quick little "write string" routine
 */
static void
out(char *p)
{
	(void)write(outfd, p, strlen(p));
}

main(int argc, char **argv)
{
	jmp_buf r;
	struct thread t;
	struct percpu pc;

	/*
	 * Single arg is output file
	 */
	if (argc != 2) {
		printf("Usage is: genassym <outfile>\n");
		exit(1);
	}
	if ((outfd = open(argv[1], O_WRITE|O_CREAT, 0600)) < 0) {
		perror(argv[1]);
		exit(1);
	}

	/*
	 * Simple constants
	 */
	CONST(GDT_KDATA);
	CONST(GDT_KTEXT);
	CONST(GDT_UDATA);
	CONST(GDT_UTEXT);
	CONST(NBPG);
	CONST(PGSHIFT);
	CONST(CR0_EM);
	CONST(CR0_TS);

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
	CONST(T_CPSOVER);

	close(outfd);
	return(0);
}
