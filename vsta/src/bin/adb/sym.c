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

/*
 * This is all that's really needed from the symbol table
 */
struct sym {
	char *s_name;
	ulong s_val;
};

static struct sym *symtab;	/* Name/val mappings */
static uint nsym = 0;		/*  ...size */
static struct aout main_hdr;	/* a.out header we loaded first */
static int hdr_loaded;		/*  ->1 when loaded */

/*
 * rdsym()
 *	Read symbols from a.out
 */
void
rdsym(char *name)
{
	FILE *f;
	uint i;
	int strtab_size, num_full_syms;
	struct nlist *sp;
	struct aout hdr;
	char *full_strtab;		/* All entries from all a.out's */
	struct nlist *full_syms;		/* All nlist entries */


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
	if (fread((char *)&strtab_size, sizeof(int), 1, f) != 1) {
		fprintf(stderr, "can't read old strtab size\n");
		exit(1);
	}

	full_strtab = malloc(strtab_size);
	full_syms = malloc(hdr.a_syms);
	if (!full_syms || !full_strtab) {
		perror(name);
		exit(1);
	}

	fseek(f, N_SYMOFF(hdr), 0);
	if (fread(full_syms, hdr.a_syms, 1, f) != 1) {
		fprintf (stderr, "can't read symbols\n");
		exit (1);
	}

	fseek(f, N_STROFF(hdr), 0);
	if (fread(full_strtab, strtab_size, 1, f) != 1) {
		fprintf (stderr, "can't read string table\n");
		exit (1);
	}

	/*
	 * All done with the file
	 */
	fclose(f);

	/*
	 * Index just those symbols which are useful
	 */
	sp = full_syms;
	num_full_syms = hdr.a_syms / sizeof (struct nlist);
	for (i = 0; sp && (i < num_full_syms); i++, sp++) {
		char *name;

		/*
		 * Skip useless entries
		 */
		if (sp->n_type & N_STAB)
			continue;
		if (sp->n_un.n_strx == 0)
			continue;
		if (sp->n_value >= 0x80000000)
			continue;
		name = full_strtab + sp->n_un.n_strx;
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
		symtab = realloc(symtab, ++nsym * sizeof(struct sym));
		if (symtab == 0) {
			perror("string table");
			exit(1);
		}
		name = strdup(name);
		if (name == NULL) {
			perror("string table");
		}
		symtab[nsym-1].s_name = name;
		symtab[nsym-1].s_val = sp->n_value;
	}

	/*
	 * Free temp storage
	 */
	free(full_strtab);
	free(full_syms);

	/*
	 * Snapshot header for first one loaded
	 */
	if (!hdr_loaded) {
		main_hdr = hdr;
		hdr_loaded = 1;
	}
}

/*
 * nameval()
 *	Map numeric offset to closest symbol plus offset
 */
char *
nameval(ulong val)
{
	uint i;
	struct sym *s;
	static char buf[128];
	ulong closest = 0;
	char *closename = 0;

	for (i = 0; i < nsym; ++i) {
		s = &symtab[i];

		/*
		 * Bound value
		 */
		if ((s->s_val < 0x1000) || (s->s_val > val))
			continue;

		/*
		 * If it's the closest fit, record it
		 */
		if (s->s_val > closest) {
			closest = s->s_val;
			closename = s->s_name;
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
	struct sym *s;

	for (i = 0; i < nsym; ++i) {
		s = &symtab[i];
		if (!strcmp(p, s->s_name)) {
			return(s->s_val);
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

	add_map(m, (void *)0x1000, main_hdr.a_text + sizeof(main_hdr), 0);
	end_text = sizeof(main_hdr) + 0x1000 + main_hdr.a_text;
	add_map(m, (void *)roundup(end_text, 4*1024*1024), main_hdr.a_data,
		main_hdr.a_text + sizeof(struct aout));
}

/*
 * dump_syms()
 *	Dump out symbol table
 */
void
dump_syms(void)
{
	uint x;
	struct sym *s;

	for (x = 0; x < nsym; ++x) {
		s = &symtab[x];
		printf("%s=%x\t", s->s_name, s->s_val);
	}
	printf("\n");
}
