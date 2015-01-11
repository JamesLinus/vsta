#ifndef _MACHSETJMP_H
#define _MACHSETJMP_H
/*
 * setjmp.h
 *	The context we keep to allow longjmp()'ing
 */
#include <sys/types.h>

/*
 * While not apparent, the layout matches a pushal instruction,
 * with the addition of EIP at the bottom of the "stack".
 */
typedef struct {
	ulong eip, edi, esi, ebp, esp, ebx, edx, ecx, eax;
} jmp_buf[1];

#ifndef KERNEL

/*
 * Routines for using this context
 */
extern int setjmp(jmp_buf);
extern void longjmp(jmp_buf, int);

#endif

#endif /* _MACHSETJMP_H */
