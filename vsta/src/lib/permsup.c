/*
 * perm.c
 *	Routines for looking at permission/protection structures
 */
#include <sys/perm.h>

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
		if (x > perm->perm_len) {
			for (y = x; y < prot->prot_len; ++y) {
				granted |= prot->prot_bits[x];
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
		if (perms[x].perm_len > PERMLEN) {
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
		perms[x].perm_len = PERMLEN+1;
	}
}
