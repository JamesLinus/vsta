#ifndef _PTE_H
#define _PTE_H
/*
 * pte.h
 *	Level 1 and 2 format--they're the same on i386
 */
typedef unsigned long pte_t;

#define PT_V	0x001		/* present */
#define PT_W	0x002		/* writable */
#define	PT_U	0x004		/* user accessible */
#define PT_R	0x020		/* accessed */
#define PT_M	0x040		/* dirty */
#define PT_PFN	0xFFFFF000	/* PFN goes here */
#define PT_PFNSHIFT 12		/* # bits over to plug in PFN */

#define NPTPG (NBPG/sizeof(pte_t))

/*
 * For picking apart the bits which index each level
 */
#define L1IDX(vaddr) (((ulong)vaddr) >> 22)
#define L2IDX(vaddr) ((((ulong)vaddr) >> 12) & 0x3FF)

#endif /* _PTE_H */
