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
extern size_t strlen(const char *);
extern int strcmp(const char *, const char *),
	strncmp(const char *, const char *, int);
extern void *memcpy(void *, const void *, size_t);
extern char *strchr(const char *, int), *strrchr(const char *, int);
extern char *index(const char *, int), *rindex(const char *, int);
extern char *strdup(const char *);
extern int bcmp(const void *, const void *, unsigned int),
	memcmp(const void *, const void *, size_t);
extern void bcopy(const void *, void *, unsigned int);
extern void bzero(void *, size_t);
extern char *strtok(char *, const char *),
	*strpbrk(const char *, const char *),
	*strstr(const char *, const char *);
extern size_t strspn(const char *, const char *);
extern void *memmove(void *, const void *, size_t),
	*memchr(const void *, unsigned char, size_t),
	*memset(void *, int, size_t);

#endif /* _STRING_H */
