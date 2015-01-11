/*
 * mstring.c
 *	"More" miscellaneous string utilities
 *
 * The functions covered here aren't normally found on Unix type systems,
 * but they are quite common in the dos world.
 */
#include <string.h>
#include <ctype.h>

/*
 * strlwr()
 *	Convert a string to lower case
 */
char *
strlwr(char *s)
{
	char *pstr = s;

	while(*pstr != '\0') {
		*pstr = tolower(*pstr);
		pstr++;
	}

	return(s);
}

/*
 * strupr()
 *	Convert a string to upper case
 */
char *
strupr(char *s)
{
	char *pstr = s;

	while(*pstr != '\0') {
		*pstr = toupper(*pstr);
		pstr++;
	}

	return(s);
}

/*
 * strrev()
 *	Reverse the order of characters in a string
 */
char *
strrev(char *s)
{
	char *ep, *sp, t;
	int i, len;

	len = strlen(s);
	if (len < 2) {
		return(s);
	}

	i = (len + 1) / 2;
	sp = s;
	ep = &s[len - 1];

	while(i > 0) {
		t = *ep;
		*ep-- = *sp;
		*sp++ = t;
		i--;
	}

	return(s);
}

/*
 * strset()
 *	Set all of the characters in a string to a given character
 */
char *
strset(char *s, int ch)
{
	char *pstr = s;

	while(*pstr != '\0') {
		*pstr++ = (char)ch;
	}

	return(s);
}

/*
 * strnset()
 *	Set all of the characters in a string to a given character up to a
 *	specified limit
 */
char *
strnset(char *s, int ch, size_t n)
{
	int i = 0;
	char *pstr = s;

	while((i < n) && (*pstr != '\0')) {
		*pstr++ = (char)ch;
		i++;
	}

	return(s);
}
