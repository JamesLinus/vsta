/*
 * id.c
 *	Print out our current IDs
 */
#include <sys/param.h>
#include <sys/perm.h>

main()
{
	int x, y, disabled, printed = 0;
	struct perm perm;

	/*
	 * Get ID and print
	 */
	for (x = 0; x < PROCPERMS; ++x) {
		/*
		 * Get next slot
		 */
		if (perm_ctl(x, (void *)0, &perm) < 0) {
			continue;
		}

		/*
		 * Clear disabled flag, then see if this slot
		 * has anything worth printing.
		 */
		disabled = 0;
		if (!PERM_ACTIVE(&perm)) {
			if (PERM_DISABLED(&perm)) {
				disabled = 1;
				PERM_ENABLE(&perm);
			} else {
				continue;
			}
		}

		/*
		 * Comma separated
		 */
		if (printed > 0) {
			printf(", ");
		}
		printed++;

		/*
		 * Print digits or <root>
		 */
		if (PERM_LEN(&perm) == 0) {
			printf("<root>");
		} else {
			for (y = 0; y < PERM_LEN(&perm); ++y) {
				if (y > 0) {
					printf(".");
				}
				printf("%d", perm.perm_id[y]);
			}
		}

		/*
		 * Print UID tag if present
		 */
		if (perm.perm_uid) {
			printf("(%d)", perm.perm_uid);
		}

		/*
		 * Valid but currently disabled
		 */
		if (disabled) {
			printf("(disabled)");
		}
	}
	printf("\n");
	return(0);
}
