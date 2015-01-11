/*
 * ctype.c
 *	Master table as well as function versions of macros
 */
#define _CT_NOMACS
#include <ctype.h>

/*
 * Hand-generated table of bits from ctype.h.  I would have used
 * symbolics but then the table is a LOT harder to get into this
 * neat columnar format.
 */
static const unsigned char __ctab[] = {
32,	32,	32,	32,	32,	32,	32,	32,	/* 000 */
32,	48,	48,	48,	48,	48,	32,	32,	/* 008 */
32,	32,	32,	32,	32,	32,	32,	32,	/* 016 */
32,	32,	32,	32,	32,	32,	32,	32,	/* 024 */
16,	0,	0,	0,	0,	0,	0,	0,	/* 032 */
0,	0,	0,	0,	0,	0,	0,	0,	/* 040 */
3,	3,	3,	3,	3,	3,	3,	3,	/* 048 */
3,	3,	0,	0,	0,	0,	0,	0,	/* 056 */
0,	10,	10,	10,	10,	10,	10,	8,	/* 064 */
8,	8,	8,	8,	8,	8,	8,	8,	/* 072 */
8,	8,	8,	8,	8,	8,	8,	8,	/* 080 */
8,	8,	8,	0,	0,	0,	0,	0,	/* 088 */
0,	6,	6,	6,	6,	6,	6,	4,	/* 096 */
4,	4,	4,	4,	4,	4,	4,	4,	/* 104 */
4,	4,	4,	4,	4,	4,	4,	4,	/* 112 */
4,	4,	4,	0,	0,	0,	0,	0	/* 120 */
};

const unsigned char *
__get_ctab(void)
{
	return(__ctab);
}

#define __bits(c, b) (__ctab[(c) & 0x7F] & (b))

isupper(int c) {
	return __bits(c, _CT_UPPER);
}
islower(int c) {
	return __bits(c, _CT_LOWER);
}
isalpha(int c) {
	return __bits(c, _CT_UPPER|_CT_LOWER);
}
isalnum(int c) {
	return __bits(c, _CT_UPPER|_CT_LOWER|_CT_DIG);
}
isdigit(int c) {
	return __bits(c, _CT_DIG);
}
isxdigit(int c) {
	return __bits(c, _CT_HEXDIG);
}
isspace(int c) {
	return __bits(c, _CT_WHITE);
}
iscntrl(int c) {
	return __bits(c, _CT_CTRL);
}
ispunct(int c) {
	return (!iscntrl(c) && !isalnum(c));
}
isprint(int c) {
	return (!iscntrl(c));
}
isascii(int c) {
	return (((int)(c) >= 0) && ((int)(c) <= 0x7F));
}
tolower(int c) {
	if (isupper(c)) {
		return(c - 'A' + 'a');
	}
	return(c);
}
toupper(int c) {
	if (islower(c)) {
		return(c - 'a' + 'A');
	}
	return(c);
}
toascii(int c) {
	return(c & 0x7F);
}
