#ifndef _STRING_H
#define _STRING_H
/*
 * string.h
 *	Definition for string handling functions
 */
#include <sys/types.h>

/*
 * Prototypes
 */
extern char *strcpy(char *, const char *), *strncpy(char *, const char *, int);
extern char *strcat(char *, const char *), *strncat(char *, const char *, int);
extern int strlen(const char *), strcmp(const char *, const char *),
	strncmp(const char *, const char *, int);
extern void *memcpy(void *, const void *, size_t);
extern char *strchr(const char *, int), *strrchr(const char *, int);
extern char *index(const char *, int), *rindex(const char *, int);
extern char *strdup(const char *);
extern int bcmp(const void *, const void *, unsigned int),
	memcmp(const void *, const void *, size_t);
extern void bcopy(const void *, void *, unsigned int);
#endif /* _STRING_H */
