/*
 * permsup.c
 *	Routines for looking at permission/protection structures
 */
#include <sys/types.h>
#include <sys/perm.h>
#include <std.h>
#include <ctype.h>

/*
 * perm_step()
 *	Calculate granted bits for a single permission struct
 */
static
perm_step(struct perm *perm, struct prot *prot)
{
	int x, y, granted;

	/*
	 * Everybody gets the first ones
	 */
	granted = prot->prot_default;

	/*
	 * Keep adding while the label matches
	 */
	for (x = 0; x < prot->prot_len; ++x) {
		/*
		 * This label dominates
		 */
		if (x >= PERM_LEN(perm)) {
			for (y = x; y < prot->prot_len; ++y) {
				granted |= prot->prot_bits[y];
			}
			return(granted);
		}

		/*
		 * Do we still match?
		 */
		if (prot->prot_id[x] != perm->perm_id[x]) {
			return(granted);
		}

		/*
		 * Yes, add bits and keep going
		 */
		granted |= prot->prot_bits[x];
	}
	return(granted);
}

/*
 * perm_calc()
 *	Given a permission and a protection array, return permissions
 */
perm_calc(struct perm *perms, int nperms, struct prot *prot)
{
	int x, granted;

	granted = 0;
	for (x = 0; x < nperms; ++x) {
		if (!PERM_ACTIVE(&perms[x])) {
			continue;
		}
		granted |= perm_step(&perms[x], prot);
	}
	return(granted);
}

/*
 * zero_ids()
 *	Zero out the permissions array for a process
 */
void
zero_ids(struct perm *perms, int nperms)
{
	int x;

	bzero(perms, sizeof(struct perm) * nperms);
	for (x = 0; x < nperms; ++x) {
		PERM_NULL(&perms[x]);
	}
}

/*
 * perm_dominates()
 *	Tell if a permission dominates another
 *
 * A permission dominates another if (1) it is shorter and matches
 * to its length, or (2) is identical except that it is disabled.
 */
perm_dominates(struct perm *us, struct perm *target)
{
	uint x;
	struct perm p;

	/*
	 * Always consider target as if it would be enabled
	 */
	p = *target;
	PERM_ENABLE(&p);

	/*
	 * Always allowed to shut off a slot
	 */
	if (!PERM_ACTIVE(&p)) {
		return(1);
	}

	/*
	 * We're shorter?
	 */
	if (PERM_ACTIVE(us) && (us->perm_len <= p.perm_len)) {
		/*
		 * Match leading values
		 */
		for (x = 0; x < PERM_LEN(us); ++x) {
			if (us->perm_id[x] != p.perm_id[x]) {
				break;
			}
		}

		/*
		 * Yup, we matched for all our digits
		 */
		if (x >= PERM_LEN(us)) {
			return(1);
		}
	}

	/*
	 * See if we're a match except for being disabled
	 */
	if (!PERM_DISABLED(us) || PERM_DISABLED(target)) {
		return(0);
	}
	if (PERM_LEN(us) != PERM_LEN(&p)) {
		return(0);
	}
	for (x = 0; x < PERM_LEN(us); ++x) {
		if (us->perm_id[x] != p.perm_id[x]) {
			return(0);
		}
	}
	return(1);
}
