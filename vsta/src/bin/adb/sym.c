/*
 * sym.c
 *	Handle symbol table read-in
 *
 * From code:
 * Written by Pace Willisson (pace@blitz.com)
 * and placed in the public domain.
 * Hacked for VSTa by Andy Valencia (vandys@cisco.com).  This file is
 * still in the public domain.
 */
#include <stdio.h>
#include <mach/aout.h>
#include <mach/nlist.h>
#include <std.h>
#include <sys/param.h>
#include "map.h"

static struct nlist *old_syms;		/* a.out information */
static int num_old_syms;
static char *old_strtab;
static int old_strtab_size;
static struct aout hdr;

static struct nlist **strtab;		/* After sifting out useless stuff */
static uint nsym = 0;			/*  ...# left */

/*
 * rdsym()
 *	Read symbols from a.out
 */
void
rdsym(char *name)
{
	FILE *f;
	uint i;
	struct nlist *sp;

	if ((f = fopen(name, "r")) == NULL) {
		fprintf(stderr, "can't open %s\n", name);
		exit(1);
	}

	if (fread((char *)&hdr, sizeof hdr, 1, f) != 1) {
		fprintf(stderr, "can't read header\n");
		exit(1);
	}

	if (N_BADMAG(hdr)) {
		fprintf(stderr, "bad magic number\n");
		exit(1);
	}

	if (hdr.a_syms == 0) {
		fprintf(stderr, "no symbols\n");
		fclose(f);
		return;
	}

	fseek (f, N_STROFF(hdr), 0);
	if (fread((char *)&old_strtab_size, sizeof(int), 1, f) != 1) {
		fprintf(stderr, "can't read old strtab size\n");
		exit(1);
	}

	if ((old_syms = (struct nlist *)malloc(hdr.a_syms)) == NULL
	    || ((old_strtab = malloc(old_strtab_size)) == NULL)) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	fseek(f, N_SYMOFF(hdr), 0);
	if (fread((char *)old_syms, hdr.a_syms, 1, f) != 1) {
		fprintf (stderr, "can't read symbols\n");
		exit (1);
	}

	fseek(f, N_STROFF(hdr), 0);
	if (fread((char *)old_strtab, old_strtab_size, 1, f) != 1) {
		fprintf (stderr, "can't read string table\n");
		exit (1);
	}

	num_old_syms = hdr.a_syms / sizeof (struct nlist);

	/*
	 * Now index just those symbols which are useful
	 */
	for (i = 0, sp = old_syms; sp && (i < num_old_syms); i++, sp++) {
		char *name;

		/*
		 * Skip useless entries
		 */
		if (sp->n_type & N_STAB)
			continue;
		if (sp->n_un.n_strx == 0)
			continue;
		if (sp->n_value >= 0x10000000)
			continue;
		name = old_strtab + sp->n_un.n_strx;
		if (strlen(name) == 0)
			continue;
		if (strchr(name, '.'))
			continue;
		if (!strncmp(name, "___gnu", 6))
			continue;

		/*
		 * Map everything to have one less leading '_'
		 */
		if (name[0] == '_') {
			sp->n_un.n_strx += 1;
		}

		/*
		 * Now add to our index
		 */
		strtab = realloc(strtab, ++nsym * sizeof(struct nlist *));
		if (strtab == 0) {
			perror("string table");
			exit(1);
		}
		strtab[nsym-1] = sp;
	}

	fclose(f);
}


/*
 * nameval()
 *	Map numeric offset to closest symbol plus offset
 */
char *
nameval(ulong val)
{
	uint i;
	struct nlist *sp;
	static char buf[128];
	ulong closest = 0;
	char *closename = 0;

	for (i = 0; i < nsym; ++i) {
		sp = strtab[i];

		/*
		 * Bound value
		 */
		if ((sp->n_value < 0x1000) || (sp->n_value > val))
			continue;

		/*
		 * If it's the closest fit, record it
		 */
		if (sp->n_value > closest) {
			closest = sp->n_value;
			closename = old_strtab + sp->n_un.n_strx;
		}
	}

	/*
	 * If did find anything, just return hex number
	 */
	if (!closename) {
		sprintf(buf, "0x%x", val);
	} else {
		/*
		 * Otherwise give them name + offset
		 */
		if (closest == val) {
			strcpy(buf, closename);
		} else {
			sprintf(buf, "%s+0x%x", closename,
				val - closest);
		}
	}
	return(buf);
}

/*
 * symval()
 *	Given symbol name, return its value
 */
ulong
symval(char *p)
{
	uint i;
	struct nlist *sp;
	char *name;

	for (i = 0; i < nsym; ++i) {
		sp = strtab[i];
		name = old_strtab + sp->n_un.n_strx;
		if (!strcmp(p, name)) {
			return(sp->n_value);
		}
	}
	return(0);
}

/*
 * map_aout()
 *	Fill in map for the a.out file we loaded
 */
map_aout(struct map *m)
{
	ulong end_text;

	add_map(m, (void *)0x1000, hdr.a_text, 0);
	end_text = 0x1000 + hdr.a_text;
	add_map(m, (void *)roundup(end_text, 4*1024*1024), hdr.a_data,
		hdr.a_text + sizeof(struct aout));
}
