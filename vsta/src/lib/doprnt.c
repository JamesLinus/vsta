/*
 * doprnt.c
 *	text buffer formatting code
 *
 * This code has been modified for use with VSTa, and now formats a string
 * buffer instead of an output file.
 */

/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <mach/ieeefp.h>

#define	MAXEXP DBL_MAX_10_EXP
#define	MAXFRACT DBL_DIG
#define MAXEXPDIGITS 8		/* Allow for 5 digits of exponent */
#define	DEFPREC	(FLT_DIG + 1)
#define	DEFLPREC (DBL_DIG + 1)
#define	BUF (MAXEXP + MAXFRACT + 1)

#define ARG(basetype) \
	_ulong = flags & LONGINT ? va_arg(argp, long basetype) : \
	    flags & SHORTINT ? (short basetype)va_arg(argp, int) : \
	    va_arg(argp, int)

#define	todigit(c) ((c) - '0')
#define	tochar(n) ((n) + '0')

#define	LONGINT		0x01	/* long integer */
#define	LONGDBL		0x02	/* long double; unimplemented */
#define	SHORTINT	0x04	/* short integer */
#define	ALT		0x08	/* alternate form */
#define	LADJUST		0x10	/* left adjustment */
#define	ZEROPAD		0x20	/* zero (as opposed to blank) pad */
#define	HEXPREFIX	0x40	/* add 0x or 0X prefix */

static char *round(double, int *, char *, char *, char, char *);
static char *exponent(char *, int, uchar);
static int isspecial(double, char *, char *);
extern double modf(double, double *);
static int cvt(double, int, int, char *, uchar, char *, char *);

/*
 * doprnt()
 *	Printf "printf()" like into a buffer
 */
int
__doprnt(char *obuf, const char *fmt0, va_list argp)
{
	uchar *fmt;		/* format string */
	int ch;			/* character from fmt */
	int cnt;		/* return value accumulator */
	int n;			/* random handy integer */
	char *t;		/* buffer pointer */
	double _double;		/* double precision arguments %[eEfgG] */
	ulong _ulong;		/* integer arguments %[diouxX] */
	int base;		/* base for [diouxX] conversion */
	int dprec;		/* decimal precision in [diouxX] */
	int fieldsz;		/* field size expanded by sign, etc */
	int flags;		/* flags as above */
	int fpprec;		/* `extra' floating precision in [eEfgG] */
	int prec;		/* precision from format (%.3d), or -1 */
	int realsz;		/* field size expanded by decimal precision */
	int size;		/* size of converted field or string */
	int width;		/* width from format (%8d), or 0 */
	char sign;		/* sign prefix (' ', '+', '-', or \0) */
	char *digs;		/* digits for [diouxX] conversion */
	char buf[BUF];		/* space for %c, %[diouxX], %[eEfgG] */

	fmt = (uchar *)fmt0;
	digs = "0123456789abcdef";
	for (cnt = 0;; ++fmt) {
		n = 0;
		for (; (ch = *fmt) && ch != '%'; ++cnt, ++fmt) {
			*obuf++ = ch;
		}
		if (!ch) {
			*obuf++ = '\0';
			return (cnt);
		}

		flags = 0;
		dprec = 0;
		fpprec = 0;
		width = 0;
		prec = -1;
		sign = '\0';

rflag:		switch (*++fmt) {
		case ' ':
			/*
			 * ``If the space and + flags both appear, the space
			 * flag will be ignored.''
			 *	-- ANSI X3J11
			 */
			if (!sign) {
				sign = ' ';
			}
			goto rflag;
		case '#':
			flags |= ALT;
			goto rflag;
		case '*':
			/*
			 * ``A negative field width argument is taken as a
			 * - flag followed by a  positive field width.''
			 *	-- ANSI X3J11
			 * They don't exclude field widths read from args.
			 */
			if ((width = va_arg(argp, int)) >= 0) {
				goto rflag;
			}
			width = -width;
			/*
			 * FALLTHROUGH
			 */
		case '-':
			flags |= LADJUST;
			goto rflag;
		case '+':
			sign = '+';
			goto rflag;
		case '.':
			if (*++fmt == '*') {
				n = va_arg(argp, int);
			} else {
				n = 0;
				while (isascii(*fmt) && isdigit(*fmt)) {
					n = 10 * n + todigit(*fmt++);
				}
				--fmt;
			}
			prec = n < 0 ? -1 : n;
			goto rflag;
		case '0':
			/*
			 * ``Note that 0 is taken as a flag, not as the
			 * beginning of a field width.''
			 *	-- ANSI X3J11
			 */
			flags |= ZEROPAD;
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			while (isascii(*fmt) && isdigit(*fmt)) {
				n = 10 * n + todigit(*fmt++);
			}
			--fmt;
			width = n;
			goto rflag;
		case 'L':
			flags |= LONGDBL;
			goto rflag;
		case 'h':
			flags |= SHORTINT;
			goto rflag;
		case 'l':
			flags |= LONGINT;
			goto rflag;
		case 'c':
			*(t = buf) = va_arg(argp, int);
			size = 1;
			sign = '\0';
			goto pforw;
		case 'D':
			flags |= LONGINT;
			/*
			 * FALLTHROUGH
			 */
		case 'd':
		case 'i':
			ARG(int);
			if ((long)_ulong < 0) {
				_ulong = -_ulong;
				sign = '-';
			}
			base = 10;
			goto number;
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
			_double = va_arg(argp, double);
			/*
			 * don't do unrealistic precision; just pad it with
			 * zeroes later, so buffer size stays rational.
			 */
			if (prec > MAXFRACT) {
				if (*fmt != 'g' && *fmt != 'G' || (flags & ALT)) {
					fpprec = prec - MAXFRACT;
				}
				prec = MAXFRACT;
			} else if (prec == -1) {
				if (flags & LONGINT) {
					prec = DEFLPREC;
				} else {
					prec = DEFPREC;
				}
			}
			/*
			 * softsign avoids negative 0 if _double is < 0 and
			 * no significant digits will be shown
			 */
			if (_double < 0) {
				sign = '-';
				_double = -_double;
			} else {
				sign = 0;
			}
			/*
			 * cvt may have to round up past the "start" of the
			 * buffer, i.e. ``intf("%.2f", (double)9.999);'';
			 * if the first char isn't NULL, it did.
			 */
			*buf = NULL;
			size = cvt(_double, prec, flags, &sign, *fmt, buf,
				   buf + sizeof(buf));
			if (!finite(_double)) {
				fpprec = 0;
			}
			t = *buf ? buf : buf + 1;
			goto pforw;
		case 'n':
			if (flags & LONGINT) {
				*va_arg(argp, long *) = cnt;
			} else if (flags & SHORTINT) {
				*va_arg(argp, short *) = cnt;
			} else {
				*va_arg(argp, int *) = cnt;
			}
			break;
		case 'O':
			flags |= LONGINT;
			/*
			 * FALLTHROUGH
			 */
		case 'o':
			ARG(unsigned);
			base = 8;
			goto nosign;
		case 'p':
			/*
			 * ``The argument shall be a pointer to void.  The
			 * value of the pointer is converted to a sequence
			 * of printable characters, in an implementation-
			 * defined manner.''
			 *	-- ANSI X3J11
			 */
			/* NOSTRICT */
			_ulong = (ulong)va_arg(argp, void *);
			base = 16;
			goto nosign;
		case 's':
			if (!(t = va_arg(argp, char *))) {
				t = "(null)";
			}
			if (prec >= 0) {
				/*
				 * can't use strlen; can only look for the
				 * NUL in the first `prec' characters, and
				 * strlen() will go further.
				 */
				char *p, *memchr();

				if (p = memchr(t, 0, prec)) {
					size = p - t;
					if (size > prec) {
						size = prec;
					}
				} else {
					size = prec;
				}
			} else {
				size = strlen(t);
			}
			sign = '\0';
			goto pforw;
		case 'U':
			flags |= LONGINT;
			/*
			 * FALLTHROUGH
			 */
		case 'u':
			ARG(unsigned);
			base = 10;
			goto nosign;
		case 'X':
			digs = "0123456789ABCDEF";
			/*
			 * FALLTHROUGH
			 */
		case 'x':
			ARG(unsigned);
			base = 16;
			/*
			 * leading 0x/X only if non-zero
			 */
			if (flags & ALT && _ulong != 0) {
				flags |= HEXPREFIX;
			}
			
			/*
			 * unsigned conversions
			 */
nosign:			sign = '\0';
			/*
			 * ``... diouXx conversions ... if a precision is
			 * specified, the 0 flag will be ignored.''
			 *	-- ANSI X3J11
			 */
number:			if ((dprec = prec) >= 0) {
				flags &= ~ZEROPAD;
			}

			/*
			 * ``The result of converting a zero value with an
			 * explicit precision of zero is no characters.''
			 *	-- ANSI X3J11
			 */
			t = buf + BUF;
			if (_ulong != 0 || prec != 0) {
				do {
					*--t = digs[_ulong % base];
					_ulong /= base;
				} while (_ulong);
				digs = "0123456789abcdef";
				if (flags & ALT && base == 8 && *t != '0') {
					*--t = '0'; /* octal leading 0 */
				}
			}
			size = buf + BUF - t;

pforw:
			/*
			 * All reasonable formats wind up here.  At this point,
			 * `t' points to a string which (if not flags&LADJUST)
			 * should be padded out to `width' places.  If
			 * flags&ZEROPAD, it should first be prefixed by any
			 * sign or other prefix; otherwise, it should be blank
			 * padded before the prefix is emitted.  After any
			 * left-hand padding and prefixing, emit zeroes
			 * required by a decimal [diouxX] precision, then print
			 * the string proper, then emit zeroes required by any
			 * leftover floating precision; finally, if LADJUST,
			 * pad with blanks.
			 */

			/*
			 * compute actual size, so we know how much to pad
			 * fieldsz excludes decimal prec; realsz includes it
			 */
			fieldsz = size + fpprec;
			realsz = dprec > fieldsz ? dprec : fieldsz;
			if (sign) {
				realsz++;
			}
			if (flags & HEXPREFIX) {
				realsz += 2;
			}

			/*
			 * right-adjusting blank padding
			 */
			if ((flags & (LADJUST|ZEROPAD)) == 0 && width) {
				for (n = realsz; n < width; n++) {
					*obuf++ = ' ';
				}
			}
			/*
			 * prefix
			 */
			if (sign) {
				*obuf++ = sign;
			}
			if (flags & HEXPREFIX) {
				*obuf++ = '0';
				*obuf++ = (char)*fmt;
			}
			/*
			 * right-adjusting zero padding
			 */
			if ((flags & (LADJUST|ZEROPAD)) == ZEROPAD) {
				for (n = realsz; n < width; n++) {
					*obuf++ = '0';
				}
			}
			
			/*
			 * leading zeroes from decimal precision
			 */
			for (n = fieldsz; n < dprec; n++) {
				*obuf++ = '0';
			}
			
			/*
			 * Check if we're doing fp - we may need to do some
			 * awkward padding for e/E and some g/G formats
			 */
			if (fpprec) {
				/*
				 * Do this the hard way :-(
				 */
				for (n = 0; n < size; n++) {
					/*
					 * Handle trailing zeros
					 */
					if ((t[n] == 'e')
					    || (t[n] == 'E')) {
						while (--fpprec >= 0) {
							*obuf++ = '0';
						}
					}
					*obuf++	= t[n];
				}
				/*
				 * trailing fp zeroes if not already
				 * handled for e/E case
				 */
				while (--fpprec >= 0) {
					*obuf++ = '0';
				}
			} else {
				/*
				 * copy the string or number proper
				 */
				bcopy(t, obuf, size);
				obuf += size;
			}
			
			/*
			 * left-adjusting padding (always blank)
			 */
			if (flags & LADJUST) {
				for (n = realsz; n < width; n++) {
					*obuf++ = ' ';
				}
			}
			
			/*
			 * finally, adjust cnt
			 */
			cnt += width > realsz ? width : realsz;
			break;
		case '\0':	/* "%?" prints ?, unless ? is NULL */
			*obuf++ = '\0';
			return (cnt);
		default:
			*obuf++ = (char)*fmt;
			cnt++;
		}
	}
	/*
	 * NOTREACHED
	 */
}

static char *
round(double fract, int *exp, char *start, char *end, char ch, char *signp)
{
	double tmp;

	if (fract) {
		(void)modf(fract * 10, &tmp);
	} else {
		tmp = todigit(ch);
	}
	if (tmp > 4) {
		for (;; --end) {
			if (*end == '.') {
				--end;
			}
			if (++*end <= '9') {
				break;
			}
			*end = '0';
			if (end == start) {
				if (exp) {	/* e/E; increment exponent */
					*end = '1';
					++*exp;
				} else {	/* f; add extra digit */
					*--end = '1';
					--start;
				}
				break;
			}
		}
	}
	/*
	 * ``"%.3f", (double)-0.0004'' gives you a negative 0.
	 */
	else if (*signp == '-') {
		for (;; --end) {
			if (*end == '.') {
				--end;
			}
			if (*end != '0') {
				break;
			}
			if (end == start) {
				*signp = '\0';
			}
		}
	}
	return(start);
}

static char *
exponent(char *p, int exp, uchar fmtch)
{
	register char *t;
	char expbuf[MAXEXPDIGITS];

	*p++ = fmtch;
	if (exp < 0) {
		exp = -exp;
		*p++ = '-';
	} else {
		*p++ = '+';
	}
	t = expbuf + MAXEXPDIGITS;
	if (exp > 9) {
		do {
			*--t = tochar(exp % 10);
		} while ((exp /= 10) > 9);
		*--t = tochar(exp);
		for (; t < expbuf + MAXEXPDIGITS; *p++ = *t++);
	}
	else {
		*p++ = '0';
		*p++ = tochar(exp);
	}
	return(p);
}

static int
isspecial(double d, char *bufp, char *signp)
{
	if (finite(d)) {
		return(0);
	} else if (isnan(d)) {
		(void)strcpy(bufp, "NaN");
	} else {
		(void)strcpy(bufp, "Inf");
	}
	*signp = (d < 0) ? '-' : '+';

	return(3);
}

static int
cvt(double number, int prec, int flags, char *signp, uchar fmtch,
		char *startp, char *endp)
{
	char *p, *t;
	double fract;
	int dotrim, expcnt, gformat;
	double integer, tmp;

	if (expcnt = isspecial(number, startp, signp)) {
		return(expcnt);
	}

	dotrim = expcnt = gformat = 0;
	fract = modf(number, &integer);

	/*
	 * get an extra slot for rounding
	 */
	t = ++startp;

	/*
	 * get integer portion of number; put into the end of the buffer; the
	 * .01 is added for modf(356.0 / 10, &integer) returning .59999999...
	 */
	for (p = endp - 1; integer; ++expcnt) {
		tmp = modf(integer / 10, &integer);
		*p-- = tochar((int)((tmp + .01) * 10));
	}
	switch(fmtch) {
	case 'f':
		/* reverse integer into beginning of buffer */
		if (expcnt) {
			for (; ++p < endp; *t++ = *p);
		} else {
			*t++ = '0';
		}

		/*
		 * if precision required or alternate flag set, add in a
		 * decimal point.
		 */
		if (prec || flags & ALT) {
			*t++ = '.';
		}
		
		/*
		 * if requires more precision and some fraction left
		 */
		if (fract) {
			if (prec) {
				do {
					fract = modf(fract * 10, &tmp);
					*t++ = tochar((int)tmp);
				} while (--prec && fract);
			}
			if (fract) {
				startp = round(fract, (int *)NULL, startp,
				    t - 1, (char)0, signp);
			}
		}
		for (; prec--; *t++ = '0');
		break;
	case 'e':
	case 'E':
eformat:	if (expcnt) {
			*t++ = *++p;
			if (prec || flags & ALT) {
				*t++ = '.';
			}
			
			/*
			 * if requires more precision and some integer left
			 */
			for (; prec && ++p < endp; --prec) {
				*t++ = *p;
			}
			
			/*
			 * if done precision and more of the integer component,
			 * round using it; adjust fract so we don't re-round
			 * later.
			 */
			if (!prec && ++p < endp) {
				fract = 0;
				startp = round((double)0, &expcnt, startp,
					       t - 1, *p, signp);
			}
			/*
			 * adjust expcnt for digit in front of decimal
			 */
			--expcnt;
		}
		/*
		 * until first fractional digit, decrement exponent
		 */
		else if (fract) {
			/*
			 * adjust expcnt for digit in front of decimal
			 */
			for (expcnt = -1;; --expcnt) {
				fract = modf(fract * 10, &tmp);
				if (tmp) {
					break;
				}
			}
			*t++ = tochar((int)tmp);
			if (prec || flags & ALT) {
				*t++ = '.';
			}
		} else {
			*t++ = '0';
			if (prec || flags & ALT) {
				*t++ = '.';
			}
		}
		/*
		 * if requires more precision and some fraction left
		 */
		if (fract) {
			if (prec) {
				do {
					fract = modf(fract * 10, &tmp);
					*t++ = tochar((int)tmp);
				} while (--prec && fract);
			}
			if (fract) {
				startp = round(fract, &expcnt, startp,
				    	       t - 1, (char)0, signp);
			}
		}
		/*
		 * if requires more precision
		 */
		for (; prec--; *t++ = '0');

		/*
		 * unless alternate flag, trim any g/G format trailing 0's
		 */
		if (gformat && !(flags & ALT)) {
			while (t > startp && *--t == '0');
			if (*t == '.') {
				--t;
			}
			++t;
		}
		t = exponent(t, expcnt, fmtch);
		break;
	case 'g':
	case 'G':
		/*
		 * a precision of 0 is treated as a precision of 1
		 */
		if (!prec) {
			++prec;
		}
		
		/*
		 * ``The style used depends on the value converted; style e
		 * will be used only if the exponent resulting from the
		 * conversion is less than -4 or greater than the precision.''
		 *	-- ANSI X3J11
		 */
		if (expcnt > prec || !expcnt && fract && fract < .0001) {
			/*
			 * g/G format counts "significant digits, not digits of
			 * precision; for the e/E format, this just causes an
			 * off-by-one problem, i.e. g/G considers the digit
			 * before the decimal point significant and e/E doesn't
			 * count it as precision.
			 */
			--prec;
			fmtch -= 2;		/* G->E, g->e */
			gformat = 1;
			goto eformat;
		}
		/*
		 * reverse integer into beginning of buffer,
		 * note, decrement precision
		 */
		if (expcnt) {
			for (; ++p < endp; *t++ = *p, --prec);
		} else {
			*t++ = '0';
		}
		/*
		 * if precision required or alternate flag set, add in a
		 * decimal point.  If no digits yet, add in leading 0.
		 */
		if (prec || flags&ALT) {
			dotrim = 1;
			*t++ = '.';
		} else {
			dotrim = 0;
		}
		/*
		 * if requires more precision and some fraction left
		 */
		while (prec && fract) {
			fract = modf(fract * 10, &tmp);
			*t++ = tochar((int)tmp);
			prec--;
		}
		if (fract) {
			startp = round(fract, (int *)NULL, startp, t - 1,
					(char)0, signp);
		}
		/*
		 * alternate format, adds 0's for precision, else trim 0's
		 */
		if (flags & ALT) {
			for (; prec--; *t++ = '0');
		} else if (dotrim) {
			while (t > startp && *--t == '0');
			if (*t != '.') {
				++t;
			}
		}
	}
	return(t - startp);
}
