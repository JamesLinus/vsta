/*
 * server.h
 *	Global definitions useful for VSTa servers
 */
#ifndef _SERVER_H
#define _SERVER_H
#include <sys/perm.h>

extern int valid_fname(char *, int);
extern char *perm_print(struct prot *);

#endif /* _SERVER_H */
