/*
 * mcount.h
 *	API for a profiling system
 */
#ifndef _MCOUNT_H
#define _MCOUNT_H

#include <sys/types.h>

extern void dump_samples(void), take_samples(uint);

#endif /* _MCOUNT_H */
