#ifndef _ALLOC_H
#define _ALLOC_H
/*
 * alloc.h
 *	Some common defs for allocation interfaces
 */
#include <sys/types.h>

#define alloca(s) __builtin_alloca(s)

extern void *malloc(uint), *realloc(void *, uint);
extern void free(void *);

#endif /* _ALLOC_H */
