/*
 * id.c
 *	Print out our current IDs
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <pwd.h>

main(int argc, char **argv)
{
	int x, disabled, printed = 0, nflag;
	struct perm perm;

	/*
	 * If -n, print numeric only
	 */
	if ((argc > 1) && !strncmp(argv[1], "-n", 2)) {
		nflag = 1;
	} else {
		nflag = 0;
	}

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
		} else if (nflag) {
			int y;

			for (y = 0; y < PERM_LEN(&perm); ++y) {
				if (y > 0) {
					printf(".");
				}
				printf("%d", perm.perm_id[y]);
			}
		} else {
			printf("%s", cvt_id(perm.perm_id, perm.perm_len));
		}

		/*
		 * Print UID tag if present
		 */
		if (perm.perm_uid) {
			struct passwd *pw;

			pw = getpwuid(perm.perm_uid);
			if (pw && !nflag) {
				printf("(%s)", pw->pw_name);
			} else {
				printf("(%d)", perm.perm_uid);
			}
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
