#ifndef _STRING_H
#define _STRING_H
/*
 * string.h
 *	Definition for string handling functions
 */

/*
 * Prototypes
 */
extern char *strcpy(char *, char *), *strncpy(char *, char *, int);
extern char *strcat(char *, char *), *strncat(char *, char *, int);
extern int strlen(char *), strcmp(char *, char *),
	strncmp(char *, char *, int);
extern void *memcpy(void *, void *, unsigned int);
extern char *strchr(char *, char), *strrchr(char *, char);
extern char *index(char *, char), *rindex(char *, char);
extern char *strdup(char *);
extern int bcmp(void *, void *, unsigned int),
	memcmp(void *, void *, unsigned int);

#endif /* _STRING_H */
