/*
 * perm.h
 *	Definitions for permission/protection structures
 */
#ifndef _PERM_H
#define _PERM_H
#include <sys/param.h>

struct perm {
	unsigned char perm_len;		/* # slots valid */
	unsigned char perm_id[PERMLEN];	/* Permission values */
};

struct prot {
	unsigned char prot_len;		/* # slots valid */
	unsigned char prot_default;	/* Capabilities available to all */
	unsigned char prot_id[PERMLEN];	/* Permission values */
	unsigned char			/* Capabilities granted */
		prot_bits[PERMLEN];
};

#ifdef KERNEL
extern int perm_calc(struct perm *, int, struct prot *);
extern void zero_ids(struct perm *, int);
#endif

#endif /* _PERM_H */
