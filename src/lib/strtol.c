/*
 * strtol.c
 *	Convert strings to longs and ulongs
 *
 * This code is extracted from the Cygnus newlib C library.  The
 * reentrancy handling has been removed as we don't do things quite the
 * same way and don't want specific reentrant versions of the functions.
 * The description for the strtol() function is included below - strtoul()
 * behaves almost identically.  The code has been reformatted into VSTa
 * style.
 *
 * The function strtol() converts the string *s to a long.  First, it
 * breaks down the string into three parts: leading whitespace, which
 * is ignored; a subject string consisting of characters resembling an
 * integer in the radix specified by base; and a trailing portion
 * consisting of zero or more unparseable characters, and always
 * including the terminating null character.  Then, it attempts to
 * convert the subject string into a long and returns the result.
 *
 * If the value of [base] is 0, the subject string is expected to look
 * like a normal C integer constant: an optional sign, a possible `0x'
 * indicating a hexadecimal base, and a number.  If base is between
 * 2 and 36, the expected form of the subject is a sequence of letters
 * and digits representing an integer in the radix specified by base,
 * with an optional plus or minus sign.  The letters a-z (or,
 * equivalently, A-Z are used to signify values from 10 to 35; only
 * letters whose ascribed values are less than base are permitted.  If
 * base is 16, a leading 0x is permitted.
 *
 * The subject sequence is the longest initial sequence of the input
 * string that has the expected form, starting with the first
 * non-whitespace character.  If the string is empty or consists entirely
 * of whitespace, or if the first non-whitespace character is not a
 * permissible letter or digit, the subject string is empty.
 *
 * If the subject string is acceptable, and the value of base is zero,
 * strtol() attempts to determine the radix from the input string.  A
 * string with a leading 0x is treated as a hexadecimal value; a string
 * with a leading 0 and no x is treated as octal; all other strings are
 * treated as decimal.  If base is between 2 and 36, it is used as the
 * conversion radix, as described above.  If the subject string begins
 * with a minus sign, the value is negated.  Finally, a pointer to the
 * first character past the converted subject string is stored in ptr,
 * if ptr is not NULL.
 *
 * If the subject string is empty (or not in acceptable form), no
 * conversion is performed and the value of s is stored in ptr (if ptr
 * is not NULL).
 *
 * Function author: Andy Wilson, 2-Oct-89.
 */
#include <sys/syscall.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

#ifdef Isspace
#undef Isspace
#endif /* Isspace */
#define Isspace(c) ((c == ' ') || (c == '\t') || (c=='\n') || (c=='\v') || \
			(c == '\r') || (c == '\f'))

/*
 * strtoul()
 *	Convert string to unsigned long
 */
ulong
strtoul(const char *s, char **ptr, int base)
{
	ulong total = 0;
	uint digit;
	int radix;
	const char *start = s;
	int did_conversion = 0;
	int overflow = 0;
	int minus = 0;
	ulong maxdiv, maxrem;

	if (s == NULL) {
		__seterr(__map_errno(ERANGE));
		if (!ptr) {
			*ptr = (char *) start;
		}
		return(0L);
	}

	while (Isspace (*s)) {
		s++;
	}

	if (*s == '-') {
		s++;
		minus = 1;
	} else if (*s == '+') {
		s++;
	}

	radix = base;
	if (base == 0 || base == 16) {
		/*
		 * try to infer radix from the string (assume decimal).
		 * accept leading 0x or 0X for base 16.
		 */
		if (*s == '0') {
			did_conversion = 1;
			if (base == 0) {
				radix = 8;	/* guess it's octal */
			}
			s++;			/* (but check for hex) */
			if (*s == 'X' || *s == 'x') {
				did_conversion = 0;
				s++;
				radix = 16;
			}
		}
	}
	if (radix == 0) {
		radix = 10;
	}
	
	maxdiv = ULONG_MAX / radix;
	maxrem = ULONG_MAX % radix;

	while ((digit = *s) != 0) {
		if (digit >= '0' && digit <= '9' && digit < ('0' + radix)) {
			digit -= '0';
		} else if (radix > 10) {
			if (digit >= 'a' && digit < ('a' + radix - 10)) {
				digit = digit - 'a' + 10;
			} else if (digit >= 'A' &&
					digit < ('A' + radix - 10)) {
				digit = digit - 'A' + 10;
			} else {
				break;
			}
		} else {
			break;
		}
		did_conversion = 1;
		if (total > maxdiv || (total == maxdiv && digit > maxrem)) {
			overflow = 1;
		}
		total = (total * radix) + digit;
		s++;
	}
	if (overflow) {
		__seterr(__map_errno(ERANGE));
		if (ptr != NULL) {
			*ptr = (char *) s;
		}
		return(ULONG_MAX);
	}
	if (ptr != NULL) {
		*ptr = (char *) ((did_conversion) ? (char *) s : start);
	}
	return(minus ? - total : total);
}

/*
 * strtol()
 *	Convert string to long
 */
long
strtol(const char *s, char **ptr, int base)
{
	int minus = 0;
	ulong tmp;
	const char *start = s;
	char *eptr;

	if (s == NULL) {
		__seterr(__map_errno(ERANGE));
		if (!ptr) {
			*ptr = (char *) start;
		}
		return(0L);
	}

	while (Isspace (*s)) {
		s++;
	}
	if (*s == '-') {
		s++;
		minus = 1;
	} else if (*s == '+') {
		s++;
	}

	/*
	 * Let strtoul do the hard work.
	 */
	tmp = strtoul(s, &eptr, base);
	if (ptr != NULL) {
		*ptr = (char *)((eptr == s) ? start : eptr);
	}
	if (tmp > (minus ? - (ulong) LONG_MIN : (ulong) LONG_MAX)) {
		__seterr(__map_errno(ERANGE));
		return(minus ? LONG_MIN : LONG_MAX);
	}
	return(minus ? (long) -tmp : (long) tmp);
}
