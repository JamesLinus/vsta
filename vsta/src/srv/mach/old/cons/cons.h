#ifndef _CONS_H
#define _CONS_H
/*
 * cons.h
 *	Wrapper to #include the right "real" file
 */
#ifdef IBM_CONSOLE
#include <mach/con_ibm.h>
#endif

#ifdef NEC_CONSOLE
#include <mach/con_nec.h>
#endif

#endif /* _CONS_H */
