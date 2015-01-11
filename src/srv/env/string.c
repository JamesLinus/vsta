/*
 * string.c
 *	Handling of struct string
 */
#include "env.h"
#include <std.h>

/*
 * ref_val()
 *	Add reference to the value
 */
void
ref_val(struct string *s)
{
	if (!s) {
		return;
	}
	s->s_refs += 1;
}

/*
 * deref_val()
 *	Remove reference to value, free node on last reference
 */
void
deref_val(struct string *s)
{
	if (!s) {
		return;
	}
	if ((s->s_refs -= 1) == 0) {
		free(s->s_val);
		free(s);
	}
}

/*
 * alloc_val()
 *	Allocate a new string node
 */
struct string *
alloc_val(char *p)
{
	struct string *s;

	s = malloc(sizeof(struct string));
	if (s == 0) {
		return(0);
	}
	s->s_refs = 1;
	if ((s->s_val = malloc(strlen(p)+1)) == 0) {
		free(s);
		return(0);
	}
	return(s);
}
