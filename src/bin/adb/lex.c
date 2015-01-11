/*
 * A standard lexical analyzer
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <std.h>
#include "adb.h"
#include "expr.h"

static char *expr_line, *expr_pos;

static char buf[80];
static int donum(int);

/*
 * Convert hex and decimal strings to integers
 */
static int
xtoi(char *p)
{
	unsigned int val = 0;
	char c;

	while ((c = *p++)) {
		if (isdigit(c)) {
			val = val*16 + (c - '0');
		} else if (isxdigit(c)) {
			if (c < 'a') {
				val = val*16 + (c - 'A') + 10;
			} else {
				val = val*16 + (c - 'a') + 10;
			}
		} else {
			break;
		}
	}
	return(val);
}

 /*
  * getchar() function for lexical analyzer.
  */
static int
nextc()
{
	register int c;

	/*
	 * Pop up a level of indirection on EOF
	 */
	c = *expr_pos;
	if (c == '\0') {
		return(-1);
	}
	expr_pos += 1;
	return (c);
}

/*
 * Push back a character
 */
static void inline
unget_c(int c)
{
	if ((expr_pos <= expr_line) || (c == -1) || !c) {
		return;
	}
	expr_pos -= 1;
	*expr_pos = c;
}

/*
 * Skip leading white space in current input stream
 */
static void
skipwhite(void)
{
	register c;

	/*
	 * Skip leading blank space
	 */
	while ((c = nextc()) != -1) {
		if (!isspace(c)) {
			break;
		}
	}
	unget_c(c);
}

/*
 * Lexical analyzer for YACC
 */
int
yylex(void)
{
	register char *p = buf;
	register c, c1;

	/*
	 * Skip over white space
	 */
	skipwhite();
	c = nextc();

	/*
	 * Return EOF
	 */
	if (c == -1) {
		return (c);
	}

	/*
	 * An "identifier"?
	 */
	if (isalpha(c) || (c == '_')) {
		/*
		 * Assemble a "word" out of the input stream, symbol table it
		 */
		*p++ = c;
		for (;;) {
			c = nextc();
			if (!isalnum(c) && (c != '_')) {
				break;
			}
			*p++ = c;
		}
		unget_c(c);
		*p = '\0';
		yylval = symval(buf);
		return (ID);
	}

	/*
	 * For numbers, call our number routine.
	 */
	if (isdigit(c)) {
		return (donum(c));
	}

	/*
	 * For certain C operators, need to look at following char to
	 *	assemble relationals.  Otherwise, just return the char.
	 */
	yylval = c;
	switch (c) {
	case '<':
		if ((c1 = nextc()) == '=') {
			return (LE);
		}
		unget_c(c1);
		return (c);
	case '>':
		if ((c1 = nextc()) == '=') {
			return (GE);
		}
		unget_c(c1);
		return (c);
	case '~':
		if ((c1 = nextc()) == '=') {
			return (NE);
		}
		unget_c(c1);
		return (c);
	default:
		return (c);
	}
}

/*
 * donum()
 *	Handle parsing of a number
 */
static int
donum(int startc)
{
	register char c, *p = buf;

	/*
	 * Hex numbers
	 */
	if (startc == '0') {
		c = nextc();
		if (c == 'x') {
			c = nextc();
			while (isxdigit(c)) {
				*p++ = c;
				c = nextc();
			}
			unget_c(c);
			*p = '\0';
			yylval = xtoi(buf);
			return (INT);
		}
		unget_c(c);
	}

	/*
	 * Otherwise assume decimal
	 */
	*p++ = startc;
	for (;;) {
		c = nextc();
		if (isdigit(c)) {
			*p++ = c;
			continue;
		}
		unget_c(c);
		break;
	}
	*p = '\0';
	yylval = atoi(buf);
	return (INT);
}

/*
 * getnum()
 *	Pull number from character stream
 *
 * We assume expression boundary is at first character which isn't
 * a legal digit, letter, or operator.
 */
char *
getnum(char *p, uint *valp)
{
	char *q, c, buf[128];
	extern uint yyval;

	expr_pos = expr_line = buf;
	buf[127] = '\0';
	strncpy(buf, p, 127);
	for (q = buf; (c = *q); ++q) {
		if (isalnum(c) || (c == '+') || (c == '-') ||
				(c == '_'))
			continue;
		break;
	}
	p += (q - buf);
	*q = '\0';
	(void)yyparse();
	*valp = yyval;
	return(p);
}

/*
 * yyerror()
 *	Report syntax error
 */
void
yyerror(char *msg)
{
	printf("Expression error: %s\n", msg);
	longjmp(errjmp, 1);
}
