#ifndef _STD_H
#define _STD_H
/*
 * std.h
 *	Another standards-driven file, I think
 *
 * See comments for unistd.h
 */

/*
 * Routine templates
 */
extern void *malloc(unsigned int), *realloc(void *, unsigned int);
extern void free(void *);
extern char *strdup(char *), *strchr(char *, char), *strrchr(char *, char),
	*index(char *, char), *rindex(char *, char);

#endif /* _STD_H */
