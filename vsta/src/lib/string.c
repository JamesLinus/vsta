/*
 * string.c
 *	Miscellaneous string utilities
 */
#include <string.h>
#include <std.h>

/*
 * strcpy()
 *	Copy a string
 */
char *
strcpy(char *dest, const char *src)
{
	char *p = dest;

	while (*p++ = *src++)
		;
	return(dest);
}

/*
 * strncpy()
 *	Copy up to a limited length
 */
char *
strncpy(char *dest, const char *src, int len)
{
	char *p = dest, *lim;

	lim = p+len;
	while (p < lim) {
		if ((*p++ = *src++) == '\0') {
			break;
		}
	}
	return(dest);
}

/*
 * strlen()
 *	Length of string
 */
strlen(const char *p)
{
	int x = 0;

	while (*p++)
		++x;
	return(x);
}

/*
 * memcpy()
 *	The compiler can generate these; we use bcopy()
 */
void *
memcpy(void *dest, const void *src, size_t cnt)
{
	bcopy(src, dest, cnt);
	return(dest);
}

/*
 * strcmp()
 *	Compare two strings, return their relationship
 */
strcmp(const char *s1, const char *s2)
{
	while (*s1++ == *s2) {
		if (*s2++ == '\0') {
			return(0);
		}
	}
	return((int)s1[-1] - (int)s2[0]);
	return(1);
}

/*
 * strcat()
 *	Add one string to another
 */
char *
strcat(char *dest, const char *src)
{
	char *p;

	for (p = dest; *p; ++p)
		;
	while (*p++ = *src++)
		;
	return(dest);
}

/*
 * strncat()
 *	Concatenate, with limit
 */
char *
strncat(char *dest, const char *src, int len)
{
	char *p;
	const char *lim;

	lim = src+len;
	for (p = dest; *p; ++p)
		;
	while (src < lim) {
		if ((*p++ = *src++) == '\0') {
			break;
		}
	}
	return(dest);
}

/*
 * strchr()
 *	Return pointer to first occurence, or 0
 */
char *
strchr(const char *p, int c)
{
	int c2;

	while (c2 = *p++) {
		if (c2 == c) {
			return((char *)(p-1));
		}
	}
	return(0);
}

/*
 * strrchr()
 *	Like strchr(), but search backwards
 */
char *
strrchr(const char *p, int c)
{
	char *q = 0, c2;

	while (c2 = *p++) {
		if (c == c2) {
			q = (char *)p;
		}
	}
	return q ? (q-1) : 0;
}

/*
 * index/rindex()
 *	Alias for strchr/strrchr()
 */
char *
index(const char *p, int c)
{
	return(strchr(p, c));
}
char *
rindex(const char *p, int c)
{
	return(strrchr(p, c));
}

/*
 * strdup()
 *	Return malloc()'ed copy of string
 */
char *
strdup(const char *s)
{
	char *p;

	if (!s) {
		return(0);
	}
	if ((p = malloc(strlen(s)+1)) == 0) {
		return(0);
	}
	strcpy(p, s);
	return(p);
}

/*
 * strncmp()
 *	Compare up to a limited number of characters
 */
strncmp(const char *s1, const char *s2, int nbyte)
{
	while (nbyte && (*s1++ == *s2)) {
		if (*s2++ == '\0') {
			return(0);
		}
		nbyte -= 1;
	}
	if (nbyte == 0) {
		return(0);
	}
	return((int)s1[-1] - (int)s2[0]);
	return(1);
}

/*
 * bcmp()
 *	Compare, binary style
 */
bcmp(const void *s1, const void *s2, unsigned int n)
{
	const char *p = s1, *q = s2;

	while (n-- > 0) {
		if (*p++ != *q++) {
			return(1);
		}
	}
	return(0);
}

/*
 * memcmp()
 *	Yet Another Name, courtesy AT&T
 */
memcmp(const void *s1, const void *s2, size_t n)
{
	return(bcmp(s1, s2, n));
}
