/*
 * scanf.c
 *	Routines for converting input into data
 *
 * Some source based on code from UC Berkeley; this source file
 * didn't have their banner, but I'll be happy to add it if
 * somebody minds.
 */
#include <stdio.h>
#include <ctype.h>

extern double atof();

/*
 * atoi()
 *	Given numeric string, return int value
 */
int
atoi(const char *p)
{
	return((int)strtol(p, (char **)NULL, 10));
}

/*
 * atol()
 *	Given numeric string, return long value
 */
long
atol(const char *p)
{
	return(strtol(p, (char **)NULL, 10));
}

#define	SPC	01
#define	STP	02

#define	SHORT	0
#define	REGULAR	1
#define	LONG	2
#define	INT	0
#define	FLOAT	1

static char _sctab[256] = {
	0,0,0,0,0,0,0,0,
	0,SPC,SPC,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	SPC,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
};

static int
instr(char *ptr, int type, int len, FILE *fp, int *eofptr,
	int *nreadp)
{
	int ch;
	char *optr;
	int ignstp;

	*eofptr = 0;
	optr = ptr;
	if (type=='c' && len==30000) {
		len = 1;
	}
	ignstp = 0;
	if (type == 's') {
		ignstp = SPC;
	}
	while ((ch = getc(fp)) != EOF) {
		*nreadp += 1;
		if (!(_sctab[ch] & ignstp)) {
			break;
		}
	}
	ignstp = SPC;
	if (type=='c')
		ignstp = 0;
	else if (type=='[')
		ignstp = STP;
	while (ch != EOF && (_sctab[ch]&ignstp)==0) {
		if (ptr) {
			*ptr++ = ch;
		}
		if (--len <= 0) {
			break;
		}
		if ((ch = getc(fp)) != EOF) {
			*nreadp += 1;
		}
	}
	if (ch != EOF) {
		if (len > 0) {
			ungetc(ch, fp);
			*nreadp -= 1;
		}
		*eofptr = 0;
	} else {
		*eofptr = 1;
	}
	if (ptr && ptr!=optr) {
		if (type != 'c') {
			*ptr++ = '\0';
		}
		return(1);
	}
	return(0);
}

static int
innum(int **ptr, int type, int len, int size, FILE *fp,
	int *eofptr, int *nreadp)
{
	char *np;
	char numbuf[64];
	int c, base;
	int expseen, scale, negflg, c1, ndigit;
	long lcval;

	if (type=='c' || type=='s' || type=='[') {
		return(instr(ptr ? *(char **)ptr :
			(char *)NULL, type, len, fp, eofptr, nreadp));
	}
	lcval = 0;
	ndigit = 0;
	scale = INT;
	if (type=='e'||type=='f') {
		scale = FLOAT;
	}
	base = 10;
	if (type=='o') {
		base = 8;
	} else if (type=='x') {
		base = 16;
	}
	np = numbuf;
	expseen = 0;
	negflg = 0;
	while ((c = getc(fp)) != EOF) {
		*nreadp += 1;
		if ((c != ' ') && (c != '\t') && (c != '\n')) {
			break;
		}
	}
	if (c=='-') {
		negflg++;
		*np++ = c;
		if ((c = getc(fp)) != EOF) {
			*nreadp += 1;
		}
		len--;
	} else if (c=='+') {
		len--;
		if ((c = getc(fp)) != EOF) {
			*nreadp += 1;
		}
	}
	for ( ; --len>=0; *np++ = c, c = getc(fp), ++*nreadp) {
		if (isdigit(c)
		 || base==16 && ('a'<=c && c<='f' || 'A'<=c && c<='F')) {
			ndigit++;
			if (base == 8) {
				lcval <<=3;
			} else if (base==10) {
				lcval = ((lcval<<2) + lcval)<<1;
			} else {
				lcval <<= 4;
			}
			c1 = c;
			if (isdigit(c)) {
				c -= '0';
			} else if ('a'<=c && c<='f') {
				c -= 'a'-10;
			} else {
				c -= 'A'-10;
			}
			lcval += c;
			c = c1;
			continue;
		} else if (c=='.') {
			if (base != 10 || scale == INT) {
				break;
			}
			ndigit++;
			continue;
		} else if ((c=='e'||c=='E') && expseen==0) {
			if (base != 10 || scale == INT || ndigit == 0) {
				break;
			}
			expseen++;
			*np++ = c;
			if ((c = getc(fp)) != EOF) {
				*nreadp += 1;
			}
			if (c!='+'&&c!='-'&&('0'>c||c>'9')) {
				break;
			}
		} else {
			break;
		}
	}
	if (negflg) {
		lcval = -lcval;
	}
	if (c != EOF) {
		ungetc(c, fp);
		*nreadp -= 1;
		*eofptr = 0;
	} else {
		*eofptr = 1;
	}
 	if (ptr == NULL || np == numbuf || (negflg && np == numbuf+1) ) {
		return(0);
	}
	*np++ = 0;
	switch((scale<<4) | size) {

	case (FLOAT<<4) | SHORT:
	case (FLOAT<<4) | REGULAR:
		**(float **)ptr = atof(numbuf);
		break;

	case (FLOAT<<4) | LONG:
		**(double **)ptr = atof(numbuf);
		break;

	case (INT<<4) | SHORT:
		**(short **)ptr = lcval;
		break;

	case (INT<<4) | REGULAR:
		**(int **)ptr = lcval;
		break;

	case (INT<<4) | LONG:
		**(long **)ptr = lcval;
		break;
	}
	return(1);
}

static char *
getccl(unsigned char *s)
{
	int c, t;

	t = 0;
	if (*s == '^') {
		t++;
		s++;
	}
	for (c = 0; c < (sizeof _sctab / sizeof _sctab[0]); c++)
		if (t)
			_sctab[c] &= ~STP;
		else
			_sctab[c] |= STP;
	if ((c = *s) == ']' || c == '-') {	/* first char is special */
		if (t)
			_sctab[c] |= STP;
		else
			_sctab[c] &= ~STP;
		s++;
	}
	while ((c = *s++) != ']') {
		if (c==0)
			return((char *)--s);
		else if (c == '-' && *s != ']' && s[-2] < *s) {
			for (c = s[-2] + 1; c < *s; c++)
				if (t)
					_sctab[c] |= STP;
				else
					_sctab[c] &= ~STP;
		} else if (t)
			_sctab[c] |= STP;
		else
			_sctab[c] &= ~STP;
	}
	return((char *)s);
}

int
_doscan(FILE *fp, const char *fmt, void *argp2)
{
	int ch, nmatch, len, ch1, **ptr, fileended, size, nread = 0;
	void **argp = argp2;

	nmatch = 0;
	fileended = 0;
	for (;;) switch (ch = *fmt++) {
	case '\0': 
		return (nmatch);
	case '%': 
		if ((ch = *fmt++) == '%')
			goto def;
		ptr = 0;
		if (ch != '*') {
			ptr = (int **)argp++;
		} else {
			ch = *fmt++;
		}
		len = 0;
		size = REGULAR;
		while (isdigit(ch)) {
			len = len*10 + ch - '0';
			ch = *fmt++;
		}
		if (len == 0)
			len = 30000;
		if (ch=='l') {
			size = LONG;
			ch = *fmt++;
		} else if (ch=='h') {
			size = SHORT;
			ch = *fmt++;
		} else if (ch=='[') {
			fmt = getccl((unsigned char *)fmt);
		}
		if (isupper(ch)) {
			ch = tolower(ch);
			size = LONG;
		}
		if (ch == '\0') {
			return(-1);
		}
		if (ch == 'n') {
			if (ptr) {
				**ptr = nread;
				nmatch += 1;
			}
			break;
		}
		if (innum(ptr, ch, len, size, fp, &fileended, &nread) &&
				ptr) {
			nmatch++;
		}
		if (fileended) {
			return(nmatch? nmatch: -1);
		}
		break;

	case ' ':
	case '\n':
	case '\t': 
		while ((ch1 = getc(fp)) != EOF) {
			nread += 1;
			if ((ch1 != ' ') && (ch1 != '\t') &&
					(ch1 != '\n')) {
				break;
			}
		}
		if (ch1 != EOF) {
			ungetc(ch1, fp);
			nread -= 1;
		}
		break;

	default: 
	def:
		ch1 = getc(fp);
		nread += 1;
		if (ch1 != ch) {
			if (ch1==EOF)
				return(-1);
			ungetc(ch1, fp);
			return(nmatch);
		}
	}
}

/*
 * scanf()
 *	stdin input scan conversion
 */
int
scanf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	return(_doscan(stdin, fmt, ap));
}

/*
 * fscanf()
 *	file input scan conversion
 */
int
fscanf(FILE *fp, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	return(_doscan(fp, fmt, ap));
}

/*
 * sscanf()
 *	string input scan conversion
 */
int
sscanf(const char *str, const char *fmt, ...)
{
	FILE *fp;
	int fd, x;
	va_list ap;

	fd = fdmem(str, strlen(str));
	if (fd < 0) {
		return(EOF);
	}
	fp = fdopen(fd, "r");
	if (fp == 0) {
		close(fd);
		return(EOF);
	}
	va_start(ap, fmt);
	x = _doscan(fp, fmt, ap);
	fclose(fp);
	return(x);
}

/*
 * vscanf()
 *	stdin input scan conversion
 */
int
vscanf(const char *fmt, va_list ap)
{
	return(_doscan(stdin, fmt, ap));
}

/*
 * vfscanf()
 *	file input scan conversion
 */
int
vfscanf(FILE *fp, const char *fmt, va_list ap)
{
	return(_doscan(fp, fmt, ap));
}

/*
 * vsscanf()
 *	string input scan conversion
 */
int
vsscanf(const char *str, const char *fmt, va_list ap)
{
	FILE *fp;
	int fd, x;

	fd = fdmem(str, strlen(str));
	if (fd < 0) {
		return(EOF);
	}
	fp = fdopen(fd, "r");
	if (fp == 0) {
		close(fd);
		return(EOF);
	}
	x = _doscan(fp, fmt, ap);
	fclose(fp);
	return(x);
}
