/*
 * load.c
 *	Routines for loading memory from files
 */
#include <stdlib.h>
#include "boot.h"

extern ulong pbase;

/*
 * load_kernel()
 *	Load kernel from its a.out image
 */
void
load_kernel(struct aouthdr *h, FILE *fp)
{
	/*
	 * The first page is invalid to catch null pointers
	 */
	pbase += NBPG;

	/*
	 * Read in the a.out header and text section
	 */
	(void) rewind(fp);
	lread(fp, pbase, sizeof(struct aouthdr));
	pbase += sizeof(struct aouthdr);
	lread(fp, pbase, h->a_text);
	pbase += h->a_text;
	printf(" %ld", h->a_text); fflush(stdout);

	/*
	 * Read in data
	 */
	lread(fp, pbase, h->a_data);
	pbase += h->a_data;
	printf("+%ld", h->a_data); fflush(stdout);

	/*
	 * Zero out BSS
	 */
	lbzero(pbase, h->a_bss);
	pbase += h->a_bss;
	printf("+%ld\n", h->a_bss);
}

/*
 * load_image()
 *	Load a boot task image into the data area
 *
 * This is different than the kernel task load; these images are
 * not in runnable format.  They're simply copied end-to-end after
 * the _end location of the kernel task.
 */
void
load_image(FILE *fp)
{
	struct aouthdr *h;

	/*
	 * Get header
	 */
	h = lptr(pbase);
	lread(fp, pbase, sizeof(struct aouthdr));
	pbase += sizeof(struct aouthdr);
	lread(fp, pbase, h->a_text);
	pbase += h->a_text;
	lread(fp, pbase, h->a_data);
	pbase += h->a_data;
	lbzero(pbase, h->a_bss);
	pbase += h->a_bss;
}
