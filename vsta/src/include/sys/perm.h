/*
 * perm.h
 *	Definitions for permission/protection structures
 */
#ifndef _PERM_H
#define _PERM_H
#include <sys/types.h>
#include <sys/param.h>

struct perm {
	uchar perm_len;		/* # slots valid */
	uchar perm_id[PERMLEN];	/* Permission values */
	ulong perm_uid;		/* UID for this ability */
};

struct prot {
	uchar prot_len;		/* # slots valid */
	uchar prot_default;	/* Capabilities available to all */
	uchar prot_id[PERMLEN];	/* Permission values */
	uchar			/* Capabilities granted */
		prot_bits[PERMLEN];
};

/*
 * Macros for fiddling perms
 */
#define PERM_ACTIVE(p) ((p)->perm_len < PERMLEN)
#define PERM_DISABLE(p) ((p)->perm_len |= 0x80)
#define PERM_DISABLED(p) ((p)->perm_len & 0x80)
#define PERM_ENABLE(p) ((p)->perm_len &= ~0x80)
#define PERM_NULL(p) ((p)->perm_len = PERMLEN)
#define PERM_LEN(p) (((p)->perm_len & ~0x80) % PERMLEN)

/*
 * Prototypes
 */
extern int perm_calc(struct perm *, int, struct prot *);
extern void zero_ids(struct perm *, int);
extern int perm_dominates(struct perm *, struct perm *);
extern int perm_ctl(int, struct perm *, struct perm *);

#endif /* _PERM_H */
