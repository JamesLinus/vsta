/*
 * gdt.c
 *	Stuff for setting up a boot GDT
 */
#include <memory.h>
#include "boot.h"
#include "gdt.h"
#include "../mach/tss.h"

extern ulong cr3, topbase, pbase, basemem, bootbase;
extern struct aouthdr hdr;

struct tss *tss16,	/* Scratch task for boot 16-bit mode */
	*tss32;		/* 32-bit task to switch to 32-bit world */
ulong *args32;		/* Array of argument words on 32-bit stack */
ulong gdt;		/* Linear address of GDT */
struct gdtdesc {
	ushort g_len;
	ulong g_off;
} gdtdesc;

/*
 * setup_gdt()
 *	Set up GDT, and associated data structures
 */
void
setup_gdt(void)
{
	struct tss *t;
	struct seg *s;
	ulong l;
	struct seg *g;

	/*
	 * Carve data structures out of high memory
	 */
	tss32 = lptr(topbase -= sizeof(struct tss));
	memset(tss32, '\0', sizeof(struct tss));
	tss16 = lptr(topbase -= sizeof(struct tss));
	memset(tss16, '\0', sizeof(struct tss));
	gdt = (topbase -= (sizeof(struct seg) * NGDT));
	g = lptr(gdt);
	memset(g, '\0', sizeof(struct seg) * NGDT);
	gdtdesc.g_len = sizeof(struct seg) * NGDT;
	gdtdesc.g_off = gdt;
	topbase &= ~(NBPG-1);	/* Leave page aligned */

	/*
	 * Create 32-bit TSS
	 */
	t = tss32;
	t->cr3 = cr3;
	t->eip = hdr.a_entry;
	t->cs = GDT_KTEXT;
	t->ss0 = t->ds = t->es = t->ss = GDT_KDATA;

	/*
	 * Leave arguments on stack of 32-bit task
	 */
	l = topbase;
	topbase -= NBPG;
	l -= (4 * sizeof(ulong));
	t->esp0 = t->esp = l;
	args32 = lptr(l);

	/*
	 * Null entry--actually, zero whole thing to be safe
	 */
	memset(g, '\0', NGDT * sizeof(struct seg));

	/*
	 * Kernel data--all 32 bits allowed, read-write
	 */
	s = &g[GDTIDX(GDT_KDATA)];
	s->seg_limit0 = 0xFFFF;
	s->seg_base0 = 0;
	s->seg_base1 = 0;
	s->seg_type = T_MEMRW;
	s->seg_dpl = 0;
	s->seg_p = 1;
	s->seg_limit1 = 0xF;
	s->seg_32 = 1;
	s->seg_gran = 1;
	s->seg_base2 = 0;

	/*
	 * Kernel text--low 2 gig, execute and read
	 */
	s = &g[GDTIDX(GDT_KTEXT)];
	*s = g[GDTIDX(GDT_KDATA)];
	s->seg_type = T_MEMXR;
	s->seg_limit1 = 0x7;

	/*
	 * 32-bit boot TSS descriptor
	 */
	l = linptr(tss32);
	s = &g[GDTIDX(GDT_BOOT32)];
	s->seg_limit0 = sizeof(struct tss)-1;
	s->seg_base0 = l & 0xFFFF;
	s->seg_base1 = (l >> 16) & 0xFF;
	s->seg_type = T_TSS;
	s->seg_dpl = 0;
	s->seg_p = 1;
	s->seg_limit1 = 0;
	s->seg_32 = 0;
	s->seg_gran = 0;
	s->seg_base2 = (l >> 24) & 0xFF;

	/*
	 * 16-bit scratch TSS
	 */
	l = linptr(tss16);
	s = &g[GDTIDX(GDT_TMPTSS)];
	*s = g[GDTIDX(GDT_BOOT32)];
	s->seg_base0 = l & 0xFFFF;
	s->seg_base1 = (l >> 16) & 0xFF;
	s->seg_base2 = (l >> 24) & 0xFF;
}

/*
 * set_args32()
 *	Set args to boot task now that all memory use is known
 */
void
set_args32(void)
{
	args32[0] = (pbase - basemem) / NBPG;
	args32[1] = 3L*M;
	args32[2] = 640L*K;
	args32[3] = (bootbase - basemem) / NBPG;
}
