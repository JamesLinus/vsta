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
	*index(char *, char), *rindex(char *, char), *strerror(void);
extern int fork(void), tfork(__voidfun);
extern int bcopy(void *, void *, unsigned int),
	bcmp(void *, void *, unsigned int);
extern long __cwd_size(void);
extern void __cwd_save(char *);
extern char *__cwd_restore(char *);

#endif /* _STD_H */
