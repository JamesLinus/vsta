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
size_t
strlen(const char *p)
{
	size_t x = 0;

	if (p == 0) {
		return(0);
	}

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
			return(dest);
		}
	}
	*p++ = '\0';
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

	do {
		c2 = *p++;
		if (c2 == c) {
			return((char *)(p-1));
		}
	} while (c2);
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

	do {
		c2 = *p++;
		if (c == c2) {
			q = (char *)p;
		}
	} while (c2);
	return(q ? (q-1) : 0);
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

/*
 * strspn()
 *	Span the string s2 (skip characters that are in s2).
 */
size_t
strspn(const char *s1, const char *s2)
{
	const char *p = s1, *spanp;
	char c, sc;

	/* Skip any characters in s2, excluding the terminating \0. */
cont:
	c = *p++;
	for (spanp = s2; (sc = *spanp++) != 0; ) {
		if (sc == c)
			goto cont;
	}
	return (p - 1 - s1);
}

/*
 * strpbrk()
 *	Find the first occurrence in s1 of a character in s2 (excluding NUL)
 */
char *
strpbrk(const char *s1, const char *s2)
{
	const char *scanp;
	int c, sc;

	while ((c = *s1++) != 0) {
		for (scanp = s2; (sc = *scanp++) != 0; ) {
			if (sc == c) {
				return ((char *) (s1 - 1));
			}
		}
	}
	return(0);
}

/*
 * strstr()
 *	Find the first occurrence of find in s
 */
char *
strstr(const char *s, const char *find)
{
	char c, sc;
	size_t len;

	if ((c = *find++) != 0) {
		len = strlen(find);
		do {
			do {
				if ((sc = *s++) == 0)
					return ((char *) 0);
			} while (sc != c);
		} while (strncmp(s, find, len) != 0);
		s--;
	}
	return ((char *) s);
}

/*
 * strtok()
 *	Tokenize string
 */
char *
strtok(char *s, const char *delim)
{
	char *spanp;
	int c, sc;
	char *tok;
	static char *last;

	if (s == (char *) 0 && (s = last) == (char *) 0) {
		return(0);
	}

	/* Skip (span) leading delimiters (s += strspn(s, delim), sort of). */
cont:
	c = *s++;
	for (spanp = (char *) delim; (sc = *spanp++) != 0;) {
		if (c == sc)
			goto cont;
	}

	if (c == 0) {		/* no non-delimiter characters */
		last = 0;
		return(0);
	}
	tok = s - 1;

	/*
	 * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
	 * Note that delim must have one NUL; we stop if we see that, too.
	 */
	for (;;) {
		c = *s++;
		spanp = (char *) delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = (char *) 0;
				else
					s[-1] = 0;
				last = s;
				return (tok);
			}
		} while (sc != 0);
	}
	/*NOTREACHED*/
}

/*
 * strsep()
 *	Extract "tokens" from a string
 */
char *
strsep(char **stringp, const char *delim)
{
	char *ret, *endd;
	
	if (!(ret = *stringp)) {
		return 0;
	}
	if (endd = strpbrk(ret, delim)) {
		/*
		 * Set *stringp to the next char after the delimeter, and
		 * zero terminate the delimeted token
		 */
		*stringp = endd + 1;
		*endd = '\0';
	} else {
		*stringp = 0;
	}

	return(ret);
}

/*
 * memmove()
 *	Apparently, a bcopy()
 */
void *
memmove(void *dest, const void *src, size_t length)
{
	bcopy(src, dest, length);
	return(dest);
}

/*
 * memchr()
 *	Like strchr(), but for any binary value
 */
void *
memchr(const void *s, unsigned char c, size_t n)
{
	if (n != 0) {
		const unsigned char *p = s;

		do {
			if (*p++ == c) {
				return ((void *) (p - 1));
			}
		} while (--n != 0);
	}
	return(0);
}

/*
 * memset()
 *	Set a binary range to a constant
 */
void *
memset(void *dst, int c, size_t n)
{
	if (n != 0) {
		char  *d = dst;

		do {
			*d++ = c;
		} while (--n != 0);
	}
	return (dst);
}

/*
 * strcspn()
 *	Find the length of initial part of s1 not including chars from s2
 */
size_t
strcspn(const char *s1, const char *s2)
{
	const char *s = s1;
	const char *c;

	while (*s1) {
		for (c = s2; *c; c++) {
			if (*s1 == *c) {
				break;
			}
		}
		if (*c) {
			break;
		}
		s1++;
	}

	return(s1 - s);
}

/*
 * strcoll()
 *	Locale specific string compare
 *
 * VSTa doesn't understand locales so it uses the POSIX definition which
 * equates strcoll() to strcmp()
 */
int
strcoll(const char *a, const char *b)
{
	return strcmp(a, b);
}

/*
 * strcasecmp()
 *	Case insensitive string compare
 */
int
strcasecmp(const char *s1, const char *s2)
{
	while (*s1 != '\0' && toupper(*s1) == toupper(*s2)) {
		s1++;
		s2++;
	}

	return(toupper(*(unsigned char *) s1)
	       - toupper(*(unsigned char *) s2));
}

/*
 * strncasecmp()
 *	Case insensitive string compare up to a maximum number of characters
 */
int
strncasecmp(const char *s1, const char *s2, size_t n)
{
	if (n == 0) {
		return 0;
	}

	while (n-- != 0 && toupper(*s1) == toupper(*s2)) {
		if (n == 0 || *s1 == '\0' || *s2 == '\0') {
			break;
		}
		s1++;
		s2++;
	}

	return(toupper(*(unsigned char *) s1)
	       - toupper(*(unsigned char *) s2));
}

/*
 * strxfrm()
 *	Transform string
 */
size_t
strxfrm(char *s1, const char *s2, size_t n)
{
	size_t res = 0;

	while (n-- > 0 && *s2) {
		*s1++ = *s2++;
		res++;
	}
	while (*s2) {
		s2++;
		res++;
	}

	return(res);
}

/*
 * swab()
 *	Swap pairs of bytes in a string
 */
void
swab(const char *src, char *dest, size_t len)
{
	for (; len > 1; len -= 2) {
		dest[1] = src[0];
		dest[0] = src[1];
		src += 2;
		dest += 2;
	}
}
