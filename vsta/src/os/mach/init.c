/*
 * init.c
 *	Set up initial environment for VSTa on i386
 */
#include <mach/pte.h>
#include <sys/percpu.h>
#include <mach/aout.h>
#include <sys/assert.h>
#include <sys/param.h>
#include <mach/vm.h>
#include <sys/boot.h>
#include <sys/mutex.h>
#include <rmap.h>
#include <mach/pte.h>
#include <sys/vm.h>
#include <mach/kbd.h>
#include <mach/machreg.h>
#include <std.h>
#include <sys/pstat.h>
#include <sys/multiboot.h>
#include "../mach/locore.h"

#define K (1024)

extern void init_trap();

char *mem_map_base;	/* Base of P->V mapping area */
char *heap;		/* Physical heap used during bootup */

struct percpu cpu;	/* Only one for i386 uP */
uint ncpu = 1;
struct percpu *nextcpu = &cpu;

struct boot_task *boot_tasks;
uint nboot_task;

struct rmap *vmap;	/* Map for virtual memory */

uint freel1pt;		/* First free slot after bootup */

/*
 * Values extracted from Multiboot header(s)
 */
uint size_base, size_ext;
static struct multiboot_module *mod_ptr;

/*
 * The two memory ranges supported under i386/ISA.  The size of
 * extended memory is handed to us from the boot loader--who
 * gets it from the NVRAM config data.
 */
struct memseg memsegs[2];
int nmemsegs = 2;
pte_t *cr3;
int bootpgs;		/* Pages of memory available at boot */

/*
 * interp_multiboot()
 *	Take multiboot mem config, extract what we need
 */
static void
interp_multiboot(struct multiboot_info *mi)
{
	ASSERT(mi->flags & MULTIBOOT_MEMORY, "interp_multiboot: no mem");
	size_base = mi->mem_lower * K;
	size_ext = mi->mem_upper * K;
	mod_ptr = (void *)mi->mods_addr;
	nboot_task = mi->mods_count;
}

/*
 * patch_args()
 *	Take the argument string, and patch it into a VSTa boot server
 *
 * This is a very basic argument line being passed; we don't honor
 * quotes or any such nonsense.
 *
 * The layout of the memory area is:
 *	32 bytes a.out header
 *	8 bytes of a jump around the argument patch area to L1
 *	argc
 *	0xDEADBEEF (argv[0])
 *	MAXARG << 16 | ARGSIZE (argv[1])
 *	argv[2]
 *	...
 *	arg string area (ARGSIZE bytes)
 * L1:	<rest of text segment>
 */
static void
patch_args(struct aout *a, char *args)
{
	char *p;
	int maxnarg, maxarg, argoff, len;
	ulong *lp;
	uint headsz = sizeof(struct aout) + 2*sizeof(ulong);

	/*
	 * Trim absolute path off first arg
	 */
	if (*args == '/') {
		while (*args && (*args != ' ')) {
			++args;
		}
		while (*--args != '/')
			;
		++args;
	}
	p = args;
	len = strlen(p)+1;

	/*
	 * Skip a.out header and initial jmp instruction.  Keep a pointer
	 * to the memory.
	 */
	lp = (ulong *)((char *)a + headsz);

	/*
	 * Verify that the dummy area exists; otherwise we're trying
	 * to pass boot arguments to a process not linked for it.
	 */
	if (lp[1] != 0xDEADBEEFL) {
		printf("Error: %s\n", args);
		ASSERT(0, "Not linked for boot arguments");
	}

	/*
	 * Extract maxnarg and maxarg.  Calculate offset to base
	 * of string area.
	 */
	maxnarg = lp[2] & 0xFFFF;
	maxarg = (lp[2] >> 16) & 0xFFFF;
	argoff = sizeof(ulong) + maxnarg*sizeof(ulong);

	/*
	 * Make sure it'll fit
	 */
	if (len > maxarg) {
		printf("Error: %s\n", args);
		ASSERT(0, "Arguments too long");
	}

	/*
	 * Fill in argv while advancing argc.  In the process,
	 * convert our argument strings to null-termination.
	 */
	while (p) {
		if (lp[0] >= maxnarg) {
			printf("Error: %s\n", args);
			ASSERT(0, "Too many arguments");
		}
		lp[lp[0]+1] =		/* argv */
			(p-args)+argoff+NBPG+headsz;
		while (*p && (*p != ' ')) {
			++p;
		}
		if (!*p) {
			p = 0;
		} else {
			*p++ = '\0';
		}
		lp[0] += 1;		/* argc */
	}

	/*
	 * Blast the buffer down into place, just beyond argc+argv
	 */
	bcopy(args, &lp[maxnarg+1], len);
}

/*
 * init_machdep()
 *	Initialize machine-dependent memory stuff
 */
void
init_machdep(void)
{
	pte_t *pt;
	struct aout *a;
	struct boot_task *b;
	int have_fpu, x, y, pgs;
	ulong cr0;
	extern struct multiboot_info *cfg_base;
	extern char _end[];

	/*
	 * Probe FPU
	 */
	fpu_enable(0);
	have_fpu = fpu_detected();
	fpu_disable(0);

	/*
	 * Initialize our single "per CPU" data structure
	 */
	bzero(&cpu, sizeof(cpu));
	cpu.pc_flags = CPU_UP|CPU_BOOT;
	if (have_fpu) {
		cpu.pc_flags |= CPU_FP;
	}
	cpu.pc_next = &cpu;

	/*
	 * Set up CR0.  Clear "task switched", set emulation.
	 */
	cr0 = get_cr0();
	cr0 &= ~(CR0_TS);
	cr0 |=  (CR0_MP | CR0_NE | CR0_EM);
	set_cr0(cr0);

	/*
	 * Get memory configuration from multiboot
	 */
	interp_multiboot(cfg_base);

	/*
	 * Apply sanity checks to values our boot loader provided.
	 */
	ASSERT(size_base > 630*K, "need 640K base mem");
	ASSERT(size_ext >= K*K, "need 1M extended mem");
	memsegs[0].m_base = 0;
	memsegs[0].m_len = size_base;
	memsegs[1].m_base = (void *)(K*K);
	memsegs[1].m_len = size_ext;

	/*
	 * Cap memory due to limits on virtual address space mapping
	 * of available RAM.
	 */
	if (size_ext > 768*K*K) {
		memsegs[1].m_len = 768*K*K;
	}

	/*
	 * Point heap at first byte beyond _end; it will almost
	 * certainly be advanced past boot tasks next, but this
	 * makes it possible to test the kernel by itself.
	 */
	if (nboot_task == 0) {
		heap = (void *)_end;
	}

	/*
	 * Multiboot will have deposited zero or more (likely more)
	 * modules.  We
	 * must manually construct processes for them so that they
	 * will be scheduled when we start running processes.  This
	 * technique is used to avoid having to embed boot drivers/
	 * filesystems/etc. into the microkernel.  We're not ready
	 * to do full task creation, but now is a good time to
	 * tabulate them.
	 *
	 * We do this in two passes; the first time, to find the first
	 * free page beyond the boot tasks (to start the heap), and then
	 * a second to tabulate the boot images using data structures
	 * carved from the heap.
	 */
	else for (x = 0; x < 2; ++x) {
		struct multiboot_module *m;

		/*
		 * When we have the heap, get our boot_tasks table
		 */
		if (x) {
			b = boot_tasks = (void *)heap;
			heap += sizeof(struct boot_task) * nboot_task;
		}

		/*
		 * Walk the multiboot modules
		 */
		for (y = 0, m = mod_ptr; y < nboot_task; ++y, ++m, ++b) {
			/*
			 * Convert from a.out-ese into a more generic
			 * representation.
			 */
			if (x) {
				a = (struct aout *)m->mod_start;
#ifdef DEBUG
				printf("%d: %x/%x/%x @ %x mod %x\n",
					y,
					a->a_text, a->a_data, a->a_bss,
					a, m);
#endif
				/*
				 * Record next entry
				 */
				b->b_pc = a->a_entry;
				b->b_textaddr = (char *)NBPG;
				b->b_text = btorp(a->a_text +
					sizeof(struct aout));
				b->b_dataaddr = (void *)(NBPG*K);
				b->b_data = btorp(a->a_data);
				b->b_bss = btorp(a->a_bss);
				b->b_pfn = btop(a);

				/*
				 * Patch in the arguments.  I claim
				 * that there should be a way to make
				 * the Multiboot loader do this, but
				 * for now there isn't, so here we go.
				 */
				patch_args(a, (char *)m->string);
			} else {
				/*
				 * First pass, just record first
				 * address beyond end of modules
				 * so we have heap start.
				 */
				if (m->mod_end > (ulong)heap) {
					heap = (void *)m->mod_end;
				}
			}
		}
	}

	/*
	 * Get a resource map for our utility virtual pool.  The PTEs
	 * for the virtual space will be set up shortly.
	 */
	vmap = (struct rmap *)heap;
	heap += sizeof(struct rmap)*VMAPSIZE;
	rmap_init(vmap, VMAPSIZE);
	rmap_free(vmap, btop(L1PT_UTIL*BYTES_L1PT), NPTPG);

	/*
	 * Get our "real" root page table.  We've been using the one
	 * from the boot loader in high memory, but it will be more
	 * convenient to use a one in boot memory, safely out of reach
	 * of the memory allocator.
	 *
	 * Note that the mappings for text and data, unlike the one
	 * provided by the boot environment, only map the actual
	 * contents of the text and data segments.
	 */
	heap = (char *)roundup(heap, NBPG);
	cr3 = (pte_t *)heap; heap += NBPG;
	bzero(cr3, NBPG);

	/*
	 * Build entry 0--1:1 map for text+data.  Note we start at index 1
	 * to leave an invalid page at vaddr 0--this catches null
	 * pointer accesses, usually.
	 */
	pt = (pte_t *)heap; heap += NBPG;
	cr3[L1PT_TEXT] = (ulong)pt | PT_V|PT_W;
	pt[0] = 0;
	for (x = 1; x < NPTPG; ++x) {
		pt[x] = (x << PT_PFNSHIFT) | PT_V|PT_W;
	}

	/*
	 * Entry 1--unused
	 */

	/*
	 * Entry 2--recursive map of page tables
	 */
	cr3[L1PT_CR3] = (ulong)cr3 | PT_V|PT_W;

	/*
	 * Entry 3--utility
	 */
	pt = (pte_t *)heap; heap += NBPG;
	cr3[L1PT_UTIL] = (ulong)pt | PT_V|PT_W;
	bzero(pt, NBPG);

	/*
	 * Set up a 1:1 mapping of memory now that we know
	 * its size.  We will use the next free L1PT entry,
	 * and use as many as it takes.  Note that the base
	 * 640K counts as 1 M for purposes of the global
	 * 1:1 map.
	 */
	pt = (pte_t *)heap;
	y = L1PT_FREE;
	bootpgs = pgs =
		btop(memsegs[1].m_base) + btop(memsegs[1].m_len);
	for (x = 0; x < pgs; ++x) {
		if ((x % NPTPG) == 0) {
			cr3[y++] = (((ulong)(&pt[x])) & PT_PFN) |
				PT_V|PT_W;
			heap += NBPG;
		}
		pt[x] = (x << PT_PFNSHIFT) | PT_V|PT_W;
	}
	mem_map_base = (char *)(L1PT_FREE * BYTES_L1PT);

	/*
	 * Switch to our own PTEs
	 */
	set_cr3((ulong)cr3);

	/*
	 * Turn on paging mode
	 */
	cr0 |=  CR0_PG;
	set_cr0(cr0);

	/*
	 * Leave index of next free slot in kernel part of L1PTEs.
	 */
	freel1pt = y;

	/*
	 * Set up interrupt system
	 */
	init_trap();
}
