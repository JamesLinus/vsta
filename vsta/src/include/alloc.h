#ifndef _ALLOC_H
#define _ALLOC_H
/*
 * alloc.h
 *	Some common defs for allocation interfaces
 */
#define alloca(s) __builtin_alloca(s)

extern void *malloc(unsigned int), *realloc(void *, unsigned int);
extern void free(void *);

#endif /* _ALLOC_H */
