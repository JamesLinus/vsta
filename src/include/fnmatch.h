#ifndef _FNMATCH_H
#define _FNMATCH_H

/*
 * fnmatch.h
 *	Filename matching facility
 */

#define	FNM_NOMATCH 1		/* Match failed. */

#define	FNM_NOESCAPE 0x01	/* Disable backslash escape character */
#define	FNM_PATHNAME 0x02	/* Slash must be matched by a slash */
#define	FNM_PERIOD 0x04		/* Dot must be matched by a dot */

extern int fnmatch(const char *, const char *, int);

#endif /* _FNMATCH_H */
