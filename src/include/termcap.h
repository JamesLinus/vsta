#ifndef _TERMCAP_H
#define _TERMCAP_H
/*
 * termcap.h
 *	Definitions for terminal capability database library
 */
#include <sys/types.h>

extern char *tgetstr(char *, char **),
	*tgoto(char *, int, int);
extern int tgetent(char *, char *),
	tfindent(char *, char *),
	tgetnum(char *), tgetflag(char *),
	tputs(char *, int, intfun);

#endif /* _TERMCAP_H */
