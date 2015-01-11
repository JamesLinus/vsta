#ifndef _TRAP_H
#define _TRAP_H
/*
 * trap.h
 *	Defines for the various traps on an i386
 */

#define T_DIV 0			/* Basic CPU traps */
#define T_DEBUG 1
#define T_NMI 2
#define T_BPT 3
#define T_OVFL 4
#define T_BOUND 5
#define T_INSTR 6
#define T_387 7
#define T_DFAULT 8
#define T_CPSOVER 9
#define T_INVTSS 10
#define T_SEG 11
#define T_STACK 12
#define T_GENPRO 13
#define T_PGFLT 14
#define T_RESVD1 15
#define T_NPX 16

#define T_RESVD2 17		/* Big block of reserved vectors */
#define T_RESVD3 18
#define T_RESVD4 19
#define T_RESVD5 20
#define T_RESVD6 21
#define T_RESVD7 22
#define T_RESVD8 23
#define T_RESVD9 24
#define T_RESVD10 25
#define T_RESVD11 26
#define T_RESVD12 27
#define T_RESVD13 29
#define T_RESVD14 29
#define T_RESVD15 30
#define T_RESVD16 31

#define T_EXTERN 32		/* Through 255--external interrupts */

#define T_SYSCALL 255		/* System calls--use "int $T_SYSCALL" */

#endif /* _TRAP_H */
