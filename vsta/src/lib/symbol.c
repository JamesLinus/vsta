/*
 * symbol.c
 *	Maintain table of symbols
 *
 * A C string pointer is not guaranteed to be unique; a pointer to
 * the characters "foo" in one place may not be the same pointer as
 * one pointing to "foo" somewhere else.  This module provides
 * registry of strings, guaranteeing that any unique string will have
 * a globally unique and consistent value.
 */
#include <sys/param.h>
#include <std.h>
#include <alloc.h>
#include <string.h>
#include <symbol.h>

/*
 * Our private symbol table format
 */
struct symbol {
	int sym_size;
	const char **sym_strings;
};

/*
 * Initial size.  From here we double our size each time until we
 * reach our max increment size.
 */
#define INIT_SIZE (32)
#define MAX_INCREMENT (512)

/*
 * sym_alloc()
 *	Allocate a new symbol table
 */
struct symbol *
sym_alloc(void)
{
	struct symbol *sp;

	sp = calloc(1, sizeof(struct symbol));
	if (sp == NULL) {
		return(NULL);
	}
	sp->sym_size = INIT_SIZE;
	sp->sym_strings = calloc(INIT_SIZE, sizeof(char *));
	if (sp->sym_strings == NULL) {
		free(sp);
		return(NULL);
	}
	return(sp);
}

/*
 * hashval()
 *	Generate a hashval for a string
 */
static unsigned int
hashval(unsigned const char *ptr)
{
	unsigned int h = 0, idx = 0;
	unsigned char c;

	while ((c = *ptr++)) {
		h ^= (c << (idx++ * NBBY));
		if (idx >= sizeof(h)) {
			idx = 0;
		}
	}
	return(h);
}

/*
 * _sym_resinsert()
 *	Insert an already-allocated string into a symbol table
 */
static void
_sym_reinsert(struct symbol *sp, const char *ptr)
{
	int hash = hashval((unsigned const char *)ptr);
	int start, x;

	start = x = hash % sp->sym_size;
	do {
		/*
		 * When find an open slot, this is where we'll put
		 * this new string.
		 */
		if (sp->sym_strings[x] == NULL) {
			sp->sym_strings[x] = ptr;
			return;
		}

		/*
		 * Advance to next slot
		 */
		if (++x >= sp->sym_size) {
			x = 0;
		}
	} while (x != start);

	/*
	 * Shouldn't happen
	 */
	abort();
}

/*
 * sym_lookup()
 *	Turn string into unique pointer
 */
const char *
sym_lookup(struct symbol *sp, const char *ptr)
{
	int hash = hashval((unsigned const char *)ptr);
	int start, x;
	const char *p;
	struct symbol *sp2;

	/*
	 * Scan starting at this hash point for our string
	 */
	start = x = hash % sp->sym_size;
	do {
		/*
		 * If find an open slot, this is where we'll put
		 * this new string.
		 */
		p = sp->sym_strings[x];
		if (!p) {
			return(sp->sym_strings[x] = strdup(ptr));
		}

		/*
		 * See if the entry matches
		 */
		if (!strcmp(p, ptr)) {
			return(p);
		}

		/*
		 * Advance to next slot
		 */
		if (++x >= sp->sym_size) {
			x = 0;
		}
	} while (x != start);

	/*
	 * We are full; resize to next growth increment and
	 * re-insert contents.
	 */
	sp2 = calloc(1, sizeof(struct symbol));
	if (!sp2) {
		return(NULL);
	}

	/*
	 * Choose growth increment
	 */
	if (sp->sym_size*2 > MAX_INCREMENT) {
		x = sp->sym_size + MAX_INCREMENT;
	} else {
		x = sp->sym_size * 2;
	}

	/*
	 * Allocate hash array
	 */
	sp2->sym_strings = calloc(x, sizeof(char *));
	if (sp2->sym_strings == NULL) {
		free(sp2);
		return(NULL);
	}
	sp2->sym_size = x;

	/*
	 * Insert all strings, then switch over to new structure.
	 */
	for (x = 0; x < sp->sym_size; ++x) {
		_sym_reinsert(sp2, sp->sym_strings[x]);
	}
	free(sp->sym_strings);
	*sp = *sp2;
	free(sp2);

	/*
	 * Now recurse to try and fit our requested string in again.
	 */
	return(sym_lookup(sp, ptr));
}
