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
uchar dbg_names[DBG_NAMESZ] = {DBG_END, 'g', 'l', 'o', 'r', 'k', 'z'};

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
	s = (void *)dbg_names;
	while (s->s_type != DBG_END) {
		s = NEXTSYM(s);
	}

	/*
	 * Install our new entry there
	 */
	s->s_type = DBG_TEXT;
	bcopy(&val, &s->s_val, sizeof(val));
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
 * findent()
 *	Search for symbol
 */
static struct sym *
findent(char *nm)
{
	struct sym *s;

	/*
	 * Walk table looking for the symbol
	 */
	for (s = (void *)dbg_names; s->s_type != DBG_END; s = NEXTSYM(s)) {
		/*
		 * Match?  Return entry.
		 */
		if (!strcmp(s->s_name, nm)) {
			return(s);
		}
	}
	return(0);
}

/*
 * find_ent()
 *	Given name, return pointer to entry
 */
static struct sym *
find_ent(char *nm)
{
	struct sym *s;

	/*
	 * Find symbol.  If no match, try again without leading underscore
	 * (if any)
	 */
	s = findent(nm);
	if (!s && (nm[0] == '_')) {
		s = findent(nm+1);
	}
	return(s);
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
	ulong val;

	s = find_ent(name);
	if (!s) {
		return(0);
	}
	bcopy(&s->s_val, &val, sizeof(val));
	return(val);
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
	ulong oval;

	s = find_ent(name);
	if (s) {
		bcopy(&s->s_val, &oval, sizeof(oval));
		printf("Warning: previous value for %s was %x\n",
			name, oval);
		bcopy(&val, &s->s_val, sizeof(val));
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
	ulong val;

	for (s = (void *)dbg_names; s->s_type != DBG_END; s = NEXTSYM(s)) {
		/*
		 * Quit when get exact match
		 */
		bcopy(&s->s_val, &val, sizeof(val));
		if (loc == val) {
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
	ulong val, closest = 99999999L;
	struct sym *s, *sclosest = 0;
	static char buf[48];
	extern char _end[];

	/*
	 * Symbols outside our executable, just provide hex
	 */
	if (loc > (off_t)_end) {
		sprintf(buf, "<%x>", loc);
		return(buf);
	}

	/*
	 * Otherwise scan for nearest fit
	 */
	for (s = (void *)dbg_names; s->s_type != DBG_END; s = NEXTSYM(s)) {

		/* Done on exact match */
		bcopy(&s->s_val, &val, sizeof(val));
		if (val == loc) {
			return(s->s_name);
		}

		/* Don't care about values below ours */
		if (val > loc) {
			continue;
		}

		/* Record nearest miss */
		if ((loc - val) < closest) {
			closest = loc - val;
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
