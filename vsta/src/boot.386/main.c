/*
 * main.c
 *	Main routine to pull everything together
 */
#include <stdlib.h>
#include <alloc.h>
#include <string.h>
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

main(int argc, char **argv)
{
	char *bootname;
	char buf[64];

	if (argc < 2) {
		printf("Usage is: boot <image> [ <boot-list-file> ]\n");
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
		buf[strlen(buf)-1] = '\0';
		if (freopen(buf, "rb", fp) == NULL) {
			perror(buf);
			exit(1);
		}
		printf(" %s", basename(buf)); fflush(stdout);
		round_pbase();
		load_image(fp);
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
