#ifndef _STD_H
#define _STD_H
/*
 * std.h
 *	Another standards-driven file, I think
 *
 * See comments for unistd.h
 */

typedef void (*__voidfun)();

/*
 * Routine templates
 */
extern void *malloc(unsigned int), *realloc(void *, unsigned int);
extern void free(void *);
extern char *strdup(char *), *strchr(char *, char), *strrchr(char *, char),
	*index(char *, char), *rindex(char *, char);
extern int fork(void), tfork(__voidfun);

#endif /* _STD_H */
