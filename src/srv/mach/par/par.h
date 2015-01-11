#ifndef __PAR_H
#define __PAR_H
/*
 * par.h
 *	Defines for high level code of parallel port
 *
 */
#include <sys/types.h>

/*
 * Structure for per-connection operations
 */
struct file {
	int f_sender;	/* Sender of current operation */
	uint f_gen;	/* Generation of access */
	uint f_flags;	/* User access bits */
};

void par_write(struct msg *m, struct file *fl);

void par_stat(struct msg *m, struct file *f);
void par_wstat(struct msg *m, struct file *f);

#endif /* _PAR_H */
