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
strcpy(char *dest, char *src)
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
strncpy(char *dest, char *src, int len)
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
strlen(char *p)
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
memcpy(void *dest, void *src, unsigned int cnt)
{
	bcopy(src, dest, cnt);
	return(dest);
}

/*
 * strcmp()
 *	Compare two strings, return their relationship
 */
strcmp(char *s1, char *s2)
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
strcat(char *dest, char *src)
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
char *strncat(char *dest, char *src, int len)
{
	char *p, *lim;

	lim = dest+len;
	for (p = dest; *p; ++p)
		;
	while (p < lim) {
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
strchr(char *p, char c)
{
	char c2;

	while (c2 = *p++) {
		if (c2 == c) {
			return(p-1);
		}
	}
	return(0);
}

/*
 * strrchr()
 *	Like strchr(), but search backwards
 */
char *
strrchr(char *p, char c)
{
	char *q = 0, c2;

	while (c2 = *p++) {
		if (c == c2) {
			q = p;
		}
	}
	return q ? (q-1) : 0;
}

/*
 * index/rindex()
 *	Alias for strchr/strrchr()
 */
char *
index(char *p, char c)
{
	return(strchr(p, c));
}
char *
rindex(char *p, char c)
{
	return(strrchr(p, c));
}

/*
 * strdup()
 *	Return malloc()'ed copy of string
 */
char *
strdup(char *s)
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
strncmp(char *s1, char *s2, int nbyte)
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
bcmp(void *s1, void *s2, unsigned int n)
{
	char *p = s1, *q = s2;

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
memcmp(void *s1, void *s2, unsigned int n)
{
	return(bcmp(s1, s2, n));
}
