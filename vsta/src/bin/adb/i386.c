/*
 * i386.c
 *	Routines which know all the gore of the i386
 */
#include <stdio.h>
#include "adb.h"
#include <sys/param.h>
#include <std.h>

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

	getregs(&t);
	printf("%s:\t", nameval(t.eip));
	(void)db_disasm(t.eip, 0);
}

/*
 * trace()
 *	Show backtrace of stack calls
 */
void
backtrace(void)
{
	ulong eip, ebp, oebp;
	struct trapframe t;
	struct stkframe {
		uint s_ebp;
		uint s_eip;
	} s;

	/*
	 * Get initial registers
	 */
	getregs(&t);
	eip = t.eip;
	ebp = t.ebp;

 	/*
 	 * Loop, reading pairs of frame pointers and return addresses
 	 */
#define INSTACK(v) (v <= 0x7FFFFFF8)
	while (INSTACK(ebp)) {
		uint narg, x;
		char *p, *loc;

 		/*
 		 * Read next stack frame, output called procedure name
 		 */
		s.s_ebp = readloc(ebp, sizeof(ulong));
		if (s.s_ebp == 0)
			break;
		s.s_eip = readloc(ebp+4, sizeof(ulong));
		if (s.s_eip == 0)
			break;
 		loc = nameval(eip);
 		if (p = strchr(loc, '+')) {
 			*p = '\0';
		}
 		printf("%s(", loc);

 		/*
 		 * Calculate number of arguments, default to 4.  We
 		 * figure it out by looking at the stack cleanup at
 		 * the return address.  If GNU C has optimized this
 		 * out (for instance, bunched several cleanups together),
 		 * we're out of luck.
 		 */
		if ((s.s_eip < NBPG) || (s.s_eip > 0x7FFFFFFF)) {
			x = 0;
		} else {
			x = readloc(s.s_eip, sizeof(ulong));
		}
		if (x == 0)
			break;
 		if ((x & 0xFF) == 0x59) {
 			narg = 1;
		} else if ((x & 0xFFFF) == 0xC483) {
			narg = ((x >> 18) & 0xF);
		} else {
			narg = 4;
		}

 		/*
 		 * Print arguments
 		 */
 		for (x = 0; x < narg; ++x) {
			uint idx;
			ulong val;

			idx = (x+2)*sizeof(ulong);
			if (INSTACK(ebp+idx)) {
				val = readloc(ebp + idx, sizeof(ulong));
			} else {
				val = 0;
			}
 			printf("%s0x%x", x ? ", " : "", val);
		}

		/*
		 * Print where called from.  We just assume that there's
		 * a 5-byte long call.  Wrong for function pointers.
		 */
		printf(") called from %s\n", nameval(s.s_eip-5));
		oebp = ebp;
 		ebp = s.s_ebp;
 		eip = s.s_eip;

		/*
		 * Make sure stack frames go in the right direction,
		 * and isn't too big a jump.
		 */
		if ((ebp <= oebp) || (ebp > (oebp + 32768))) {
			break;
		}
 	}
}

/*
 * regs_pc()
 *	Return "program counter" value as extracted from machine state
 */
ulong
regs_pc(struct trapframe *tf)
{
	return(tf->eip);
}
