/*
 * scanf.c
 *	Routines for converting input into data
 */
#include <ctype.h>

/*
 * atoi()
 *	Given numeric string, return value
 */
atoi(char *p)
{
	int val = 0, neg = 0;
	char c;

	/*
	 * If leading '-', flag negative
	 */
	c = *p++;
	if (c == '-') {
		neg = 1;
		c = *p++;
	}

	/*
	 * While is numeric, assemble value
	 */
	while (isdigit(c)) {
		val = val*10 + (c - '0');
		c = *p++;
	}

	return neg ? (0 - val) : val;
}
