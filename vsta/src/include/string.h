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
extern void bcopy(const void *src, void *dest, unsigned int n);
extern void bzero(void *s, size_t n);
extern int bcmp(const void *s1, const void *s2, unsigned int n);

extern void *memcpy(void *dest, const void *src, size_t cnt);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern void *memmove(void *dest, const void *src, size_t length);
extern void *memchr(const void *s, unsigned char c, size_t n);
extern void *memset(void *dest, int c, size_t n);

extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, int len);
extern char *strcat(char *dest, const char *src);
extern char *strncat(char *dest, const char *src, int len);
extern size_t strlen(const char *p);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, int nbyte);
extern int strcoll(const char *a, const char *b);
extern char *strchr(const char *p, int c);
extern char *strrchr(const char *p, int c);
extern char *index(const char *p, int c);
extern char *rindex(const char *p, int c);
extern char *strdup(const char *s);
extern size_t strspn(const char *s1, const char *s2);
extern size_t strcspn(const char *s1, const char *s2);
extern char *strpbrk(const char *s1, const char *s2);
extern char *strstr(const char *s, const char *find);
extern char *strtok(char *s, const char *delim);
extern char *strsep(char **stringp, const char *delim);
extern int strcasecmp(const char *s1, const char *s2);
extern int strncasecmp(const char *s1, const char *s2, size_t n);
extern size_t strxfrm(char *s1, const char *s2, size_t n);
extern void swab(const char *src, char *dest, size_t len);

#define stricmp(s1, s2) strcasecmp(s1, s2)
#define strnicmp(s1, s2, n) strncasecmp(s1, s2, n)

extern char *strlwr(char *s);
extern char *strupr(char *s);
extern char *strrev(char *s);
extern char *strset(char *s, int ch);
extern char *strnset(char *s, int ch, size_t n);

/*
 * NULL defined here for the sake of completeness - I think this is needed
 * for ANSI C compliance
 */
#ifndef NULL
#define NULL (0)
#endif

#endif /* _STRING_H */
