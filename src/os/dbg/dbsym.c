/*
 * dbsym.c
 *	Path symbols into a kernel image
 *
 * Unlike the original, this implementation tries to be independent
 * of the specifics of the executable format.  It drives nm(1) to get
 * the symbols, and finds where to patch them into the executable by
 * searching for a magic tag.
 */
#ifdef KDB
#include <stdio.h>
#include "dbg.h"

extern void *malloc();

usage()
{
	fprintf(stderr, "usage: <file>\n");
	exit(1);
}

main(int argc, char **argv)
{
	FILE *fp, *nmfp;
	char *nm = "nm", *name, type;
	int c, matches;
	unsigned long matchoff, loc;
	char buf[128], buf2[128];

	/*
	 * Single argument--kernel to patch
	 */
	if ((argc < 2) || (argc > 3)) {
		usage();
	}
	name = argv[1];
	if (argc == 3) {
		nm = argv[2];
	}

	/*
	 * Open kernel
	 */
	if ((fp = fopen(name, "r+b")) == NULL) {
		fprintf(stderr, "can't open %s\n", name);
		exit(1);
	}

	/*
	 * Find where to stick stuff
	 */
	matches = 0;
	while ((matches < 7) && ((c = fgetc(fp)) != EOF)) {
		switch (matches) {
		case 0:
			if (c == DBG_END) {
				matchoff = ftell(fp) - 1;
				matches += 1;
			}
			break;
		case 1:
			if (c == 'g') {
				matches += 1;
			} else {
				matches = 0;
			}
			break;
		case 2:
			if (c == 'l') {
				matches += 1;
			} else {
				matches = 0;
			}
			break;
		case 3:
			if (c == 'o') {
				matches += 1;
			} else {
				matches = 0;
			}
			break;
		case 4:
			if (c == 'r') {
				matches += 1;
			} else {
				matches = 0;
			}
			break;
		case 5:
			if (c == 'k') {
				matches += 1;
			} else {
				matches = 0;
			}
			break;
		case 6:
			if (c == 'z') {
				matches += 1;
			} else {
				matches = 0;
			}
			break;
		}
	}

	/*
	 * If we couldn't find our magic tag, bail
	 */
	if (matches != 7) {
		printf("No symbol table buffer in %s\n", argv[1]);
		exit(1);
	}

	/*
	 * Seek back to initialized data
	 */
	fseek(fp, matchoff, 0);

	/*
	 * Get the symbol table
	 */
	sprintf(buf, "%s %s", nm, argv[1]);
	nmfp = popen(buf, "r");
	if (nmfp == NULL) {
		perror(buf);
		exit(1);
	}

	/*
	 * While there are symbols, extract the entries, and stuff
	 * symbols into the symbol table.
	 */
	while (fgets(buf, sizeof(buf), nmfp)) {
		/*
		 * Explode next entry
		 */
		if (sscanf(buf, "%lx %c %s", &loc, &type, buf2) != 3) {
			continue;
		}

		/*
		 * Ignore filler
		 */
		if (strchr(buf2, '.') || !strncmp(buf2, "__gnu", 5)) {
			continue;
		}

		/*
		 * Map types
		 */
		switch (type) {
		case 'b':
		case 'B':
		case 'd':
		case 'D':
			fputc(DBG_DATA, fp);
			break;
		case 't':
		case 'T':
		default:
			fputc(DBG_TEXT, fp);
			break;
		}

		/*
		 * Pad to 4-byte boundary
		 */
		fputc(0, fp);
		fputc(0, fp);
		fputc(0, fp);

		/*
		 * Write value
		 */
		fwrite(&loc, sizeof(loc), 1, fp);

		/*
		 * Write name
		 */
		fwrite(buf2, sizeof(char), strlen(buf2)+1, fp);
	}
	pclose(nmfp);

	/*
	 * Cap size
	 */
	if ((ftell(fp) - matchoff) >= DBG_NAMESZ) {
		fprintf(stderr, "Error: too many symbols for %s\n", argv[1]);
		fprintf(stderr, "%s corrupt; deleted.\n", argv[1]);
		unlink(argv[1]);
		exit(1);
	}

	/*
	 * Tag end of symbol table, and done
	 */
	fputc(DBG_END, fp);
	fclose(fp);

	return(0);
}

#else /* !KDB */

/*
 * When not debugging, no need to stuff symbols into kernel
 */
main()
{
	return(0);
}

#endif /* KDB */
