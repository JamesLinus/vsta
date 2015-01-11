#ifndef _MISC_H
#define _MISC_H
/*
 * misc.h
 *	Miscellaneous kernel function declarations
 */
#ifdef KERNEL

#include <sys/types.h>

extern int get_ustr(char *, int, void *, int);
extern void puts(char *);
extern void sprintf(char *, const char *, ...);
extern void printf(const char *, ...);
extern void panic(const char *, ...);
extern int err(char *);
extern char *strcpy(char *, const char *);
extern int strlen(const char *);
extern int isroot(void);
extern int issys(void);
extern int strcmp(const char *, const char *);
extern int strerror(char *);
extern int perm_ctl(int, struct perm *, struct perm *);
extern int set_cmd(char *);
extern pid_t getid(int);
extern void assfail(const char *, const char *, int);
extern void interval_sleep(int);

#endif /* KERNEL */

#endif /* _MISC_H */
