#ifndef _GETOPT_H
#define _GETOPT_H
/*
 * getopt() package.  We use __getopt_ptr() to get a pointer to the
 * "global" data variables.
 */
extern void *__getopt_ptr(int);
#define optarg (*(char **)__getopt_ptr(3))
#define optind (*(int *)__getopt_ptr(1))
#define opterr (*(int *)__getopt_ptr(0))
extern int getopt(int, char **, char *);
#endif
