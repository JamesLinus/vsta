/*
 * i386.c
 *	Routines which know all the gore of the i386
 */
#include <sys/types.h>
#include <mach/machreg.h>

extern char *nameval(ulong);
extern void getregs(struct trapframe *);

/*
 * flagnames()
 *	Return string describing flags
 */
static char *
flagnames(ulong eflags)
{
	static char buf[60];

	buf[0] = '\0';
	if (eflags & F_CF) strcat(buf, " carry");
	if (eflags & F_PF) strcat(buf, " parity");
	if (eflags & F_AF) strcat(buf, " bcd");
	if (eflags & F_ZF) strcat(buf, " zero");
	if (eflags & F_SF) strcat(buf, " sign");
	if (eflags & F_DF) strcat(buf, " dir");
	if (eflags & F_OF) strcat(buf, " overflow");
	return(buf);
}

/*
 * dump_regs()
 *	Print out registers
 */
void
dump_regs(void)
{
	struct trapframe t;

	getregs(&t);
	printf("eip 0x%x (%s) eflags %s\n",
		t.eip, nameval(t.eip), flagnames(t.eflags));
	printf(" eax 0x%x ebx 0x%x ecx 0x%x edx 0x%x esi 0x%x edi 0x%x\n",
		t.eax, t.ebx, t.ecx, t.edx, t.esi, t.edi);
	printf(" esp 0x%x ebp 0x%x\n",
		t.esp, t.ebp);
}

/*
 * show_here()
 *	Pick up current EIP, and show location
 */
void
show_here(void)
{
	struct trapframe t;
	extern ulong db_disasm(ulong, int);

	getregs(&t);
	printf("%s:\t", nameval(t.eip));
	(void)db_disasm(t.eip, 0);
}
