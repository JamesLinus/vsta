#ifdef KDB
/*
 * names.c
 *	Stuff for keeping a namelist within the kernel image
 */
#include <sys/types.h>
#include "../dbg/dbg.h"
#include <sys/assert.h>

/*
 * The array and its size.  Filled in by the dbsym(1) utility.
 */
int dbg_names_len = DBG_NAMESZ;
uchar dbg_names[DBG_NAMESZ] = {DBG_END};
struct sym *dbg_start = (struct sym *)dbg_names;

/*
 * add_ent()
 *	Add a symbol to the table
 */
static void
add_ent(char *nm, ulong val)
{
	struct sym *s;

	/*
	 * Skip to DBG_END marker
	 */
	s = dbg_start;
	while (s->s_type != DBG_END) {
		s = NEXTSYM(s);
	}

	/*
	 * Install our new entry there
	 */
	s->s_type = DBG_TEXT;
	s->s_val = val;
	strcpy(s->s_name, nm);
	s = NEXTSYM(s);
	ASSERT((uchar *)s < &dbg_names[DBG_NAMESZ],
		"add_ent: overflow");

	/*
	 * Fill in the new "end" entry
	 */
	s->s_type = DBG_END;
}

/*
 * find_ent()
 *	Given name, return pointer to entry
 */
static struct sym *
find_ent(char *nm)
{
	struct sym *s;
	int x, loops = 1;

	/*
	 * Ignore leading '_' on the first pass through the name table
	 */
	if (nm[0] == '_') {
		++nm;
		loops = 2;
	}

	/*
	 * Walk table looking for the symbol
	 */
	for (x = 0; x < loops; x++) {
		for (s = dbg_start; s->s_type != DBG_END; s = NEXTSYM(s)) {
			/*
			 * Match?  Return entry.
			 */
			if (!strcmp(s->s_name, nm)) {
				return(s);
			}
		}

		/*
		 * If we started with an underscore, let's see if it
		 * really was important!
		 */
		--nm;
	}

	return(0);
}

/*
 * symval()
 *	Given name, do hashed lookup to value for symbol
 *
 * longjmp()'s on failure
 */
ulong
symval(char *name)
{
	struct sym *s;

	s = find_ent(name);
	if (!s) {
		return(0);
	}
	return(s->s_val);
}

/*
 * setsym()
 *	Set named symbol to given value
 *
 * Will warn if overwriting an existing symbol
 */
void
setsym(char *name, ulong val)
{
	struct sym *s;

	s = find_ent(name);
	if (s) {
		printf("Warning: previous value for %s was %x\n",
			name, s->s_val);
		s->s_val = val;
	} else {
		add_ent(name, val);
	}
}

/*
 * nameval()
 *	Give a symbol for the named value
 */
char *
nameval(ulong loc)
{
	struct sym *s;

	for (s = dbg_start; s->s_type != DBG_END; s = NEXTSYM(s)) {
		/*
		 * Quit when get exact match
		 */
		if (s->s_val == loc) {
			return(s->s_name);
		}
	}
	return(0);
}

/*
 * symloc()
 *	Return pointer to string describing the given location
 */
char *
symloc(off_t loc)
{
	ulong closest = 99999999L;
	struct sym *s, *sclosest = 0;
	static char buf[48];

	for (s = dbg_start; s->s_type != DBG_END; s = NEXTSYM(s)) {

		/* Done on exact match */
		if (s->s_val == loc) {
			return(s->s_name);
		}

		/* Don't care about values below ours */
		if (s->s_val > loc) {
			continue;
		}

		/* Record nearest miss */
		if ((loc - s->s_val) < closest) {
			closest = loc - s->s_val;
			sclosest = s;
		}
	}
	if (sclosest) {
		sprintf(buf, "%s+%x", sclosest->s_name, closest);
	} else {
		sprintf(buf, "(%x)", loc);
	}
	return(buf);
}

#endif /* KDB */
