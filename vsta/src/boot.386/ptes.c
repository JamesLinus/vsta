/*
 * ptes.c
 *	Code for setting up PTE structures
 */
#include <memory.h>
#include "boot.h"
#include "../mach/pte.h"

extern ulong topbase;
ulong cr3;
ulong *l1, *txt, *dat;

/*
 * setup_ptes()
 *	Set up a boot mapping of PTEs
 *
 * Build the structures in topmem coming downwards.  This keeps them
 * intact during the mass relocation of the kernel image down to 0.
 */
void
setup_ptes(ulong text_size)
{
	ulong x, datpage;

	/*
	 * Get root page table.  Set all slots to 0 (invalid)
	 */
	cr3 = (topbase -= NBPG);
	l1 = lptr(cr3);
	memset(l1, '\0', NBPG);

	/*
	 * First slot is for text--0..4MB.  Map it P==V.
	 */
	l1[0] = (topbase -= NBPG) | PT_V|PT_W;
	txt = lptr(topbase);
	for (x = 0L; x < NPTPG; x += 1L) {
		txt[x] = (x << PT_PFNSHIFT) | PT_V|PT_W;
	}

	/*
	 * Second slot is data.  It starts at next free page above
	 * text and goes up for full page's worth.  Text size doesn't
	 * include size of a.out header, though it's there in memory.
	 * It also doesn't include the NULL page at vaddr 0.
	 */
	l1[1] = (topbase -= NBPG) | PT_V|PT_W;
	dat = lptr(topbase);
	text_size += sizeof(struct aouthdr);
	datpage = (text_size + (NBPG-1)) & ~(NBPG-1);
	datpage /= NBPG;
	datpage += 1;
	for (x = 0L; x < NPTPG; x += 1L) {
		dat[x] = ((x+datpage) << PT_PFNSHIFT) | PT_V|PT_W;
	}
}
