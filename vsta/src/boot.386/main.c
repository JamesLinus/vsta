/*
 * main.c
 *	Main routine to pull everything together
 */
#include <stdlib.h>
#include <alloc.h>
#include <string.h>
#include <memory.h>
#include "boot.h"

extern ushort move_jump_len;	/* Assembly support */
extern void move_jump(void), run_move_jump(void);

typedef void (*voidfun)();
voidfun move_jump_hi;		/* Copy of move_jump in high mem */

FILE *aout, *bootf, *fp;	/* Files we load via */
ulong basemem;			/* Start of kernel image */
ulong pbase;			/* Actual physical address (linear, 32-bit) */
ulong topbase;			/* Address of top (640K downward) memory */
struct aouthdr hdr;		/* Header of kernel image */
ulong stackmem;			/* A temp stack for purposes of booting */
ushort stackseg;		/* And a segment pointer for it */
ulong bootbase;			/* Where boot tasks loaded */

/*
 * basename()
 *	Return pointer to just base part of path
 */
static char *
basename(char *path)
{
	char *p;

	p = strrchr(path, '/');
	if (p) {
		return(p+1);
	}
	p = strrchr(path, '\\');
	if (p) {
		return(p+1);
	}
	return(path);
}

/*
 * round_pbase()
 *	Round up base to next page boundary
 */
static void
round_pbase(void)
{
	/*
	 * Point to next chunk of memory, rounded up to a page
	 */
	pbase = (pbase + (NBPG-1)) & ~(NBPG-1);
}

/*
 * try_setarg()
 *	Try to insert the given argument into the area reserved for it
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
try_setarg(ulong base, char *p)
{
	char *buf = p;
	int maxnarg, maxarg, argoff, len = strlen(p)+1;
	ulong *lp;
	const ulong headsz = 32L+8L;

	/*
	 * Skip a.out header and initial jmp instruction.  Keep a pointer
	 * to the memory.
	 */
	base = base + headsz;
	lp = lptr(base);

	/*
	 * Verify that the dummy area exists; otherwise we're trying
	 * to pass boot arguments to a process not linked for it.
	 */
	if (lp[1] != 0xDEADBEEFL) {
		printf("\nError: not linked for boot arguments.\n");
		exit(1);
	}

	/*
	 * Extract maxnarg and maxarg.  Calculate offset to base
	 * of string area.
	 */
	maxnarg = lp[2] & 0xFFFF;
	maxarg = (lp[2] >> 16) & 0xFFFF;
	argoff = sizeof(long) + maxnarg*sizeof(long);

	/*
	 * Make sure it'll fit
	 */
	if (len > maxarg) {
		printf("\nError: arguments too long.\n");
		exit(1);
	}

	/*
	 * Fill in argv while advancing argc.  In the process,
	 * convert our argument strings to null-termination.
	 */
	while (p) {
		if (lp[0] >= maxnarg) {
			printf("\nError: too many arguments.\n");
			exit(1);
		}
		lp[lp[0]+1] =		/* argv */
			(p-buf)+argoff+NBPG+headsz;
		p = strchr(p, ' ');
		if (p) {
			*p++ = '\0';
		}
		lp[0] += 1;		/* argc */
	}

	/*
	 * Blast the buffer down into place, just beyond argc+argv
	 */
	memcpy(&lp[maxnarg+1], buf, len);
}

main(int argc, char **argv)
{
	char *bootname;
	char buf[64];
	extern int cputype(void);

	if (argc < 2) {
		printf("Usage is: boot <image> [ <boot-list-file> ]\n");
		exit(1);
	}
	if (cputype()) {
		printf("Must be in REAL mode (not V86) to boot VSTa\n");
		exit(1);
	}

	/*
	 * Get actual executable file
	 */
	if ((aout = fopen(argv[1], "rb")) == NULL) {
		perror(argv[1]);
		exit(1);
	}
	if (fread(&hdr, sizeof(hdr), 1, aout) != 1) {
		printf("Read of a.out header in %s failed.\n", argv[1]);
		exit(1);
	}

	/*
	 * Get list of process images we will append
	 */
	if (argc > 2) {
		bootname = argv[1];
	} else {
		bootname = "boot.lst";
	}
	if ((bootf = fopen(bootname, "r")) == NULL) {
		perror(bootname);
		exit(1);
	}
	(void)getc(bootf); rewind(bootf);

	/*
	 * For now, open to any old file; this is just to get its buffering
	 * into memory.  We start carving out chunks of memory beyond
	 * sbrk(0) pretty soon, and it would be inconvenient for another
	 * malloc() to conflict with this.
	 */
	fp = fopen(bootname, "rb");
	(void)getc(fp);

	/*
	 * Start at next page up
	 */
	pbase = linptr(sbrk(0));
	round_pbase();
	basemem = pbase;

	/*
	 * Start top pointer at top of memory
	 */
	topbase = 640L * K;

	/*
	 * Carve out a stack
	 */
	stackmem = (topbase -= NBPG);
	stackseg = stackmem >> 4;

	/*
	 * Set up page tables and GDT
	 */
	setup_ptes(hdr.a_text);
	setup_gdt();

	/*
	 * Get kernel image
	 */
	printf("Boot %s:", basename(argv[1])); fflush(stdout);
	round_pbase();
	load_kernel(&hdr, aout);
	fclose(aout);

	/*
	 * Add on each boot task image
	 */
	printf("Tasks:");
	round_pbase();
	bootbase = pbase;
	while (fgets(buf, sizeof(buf)-1, bootf)) {
		char *p, *q;
		ulong opbase;

		/*
		 * Convert to null-termination
		 */
		buf[strlen(buf)-1] = '\0';

		/*
		 * Skip blank lines and comments
		 */
		if (!buf[0] || (buf[0] == '#')) {
			continue;
		}

		/*
		 * Record arg string, if any
		 */
		p = strchr(buf, ' ');
		if (p) {
			*p = '\0';
		}

		/*
		 * Load file
		 */
		if (freopen(buf, "rb", fp) == NULL) {
			perror(buf);
			exit(1);
		}
		printf(" %s", basename(buf)); fflush(stdout);

		/*
		 * Round to next page, load file
		 */
		round_pbase();
		opbase = pbase;
		load_image(fp);

		/*
		 * Skip any leading path on the command name.
		 * Saves precious bytes in the a.out argument
		 * area.
		 */
		q = strrchr(buf, '/');
		if (q) {
			++q;
		} else {
			q = buf;
		}

		/*
		 * If there are args, restore space
		 */
		if (p) {
			*p = ' ';
		}

		/*
		 * Insert command name and args into boot file image
		 */
		try_setarg(opbase, q);
	}
	fclose(fp);
	fclose(bootf);

	/*
	 * Copy the boot code up to high memory
	 */
	topbase -= move_jump_len;
	lbcopy(linptr(move_jump), topbase, (ulong)move_jump_len);
	move_jump_hi = lptr(topbase);

	/*
	 * Fill in arguments to 32-bit task.  We couldn't do this until
	 * the last bit of memory use was known.
	 */
	set_args32();

	/*
	 * Finally, atomically (so far as *we* can tell) move the
	 * image down to 0 and jump to its entry point.
	 */
	printf("\nLaunch at 0x%lx\n", hdr.a_entry);
	run_move_jump();
	return(0);
}
