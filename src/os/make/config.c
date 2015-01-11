/*
 * config.c
 *	Poor man's makefile maker
 *
 * No longer used, but left for posterity
 */
#include <stdio.h>

#define COLS (50)		/* Max # cols used for OBJS list */

extern void *malloc(), *realloc();

char *objs = 0;

main()
{
	FILE *fp, *fpmk, *fpobj;
	int c, l, col = 0;
	char buf[128], dir[128];

	/*
	 * Open object list file.  We need this because of DOS'
	 * completely lame command length limit.
	 */
	if ((fpobj = fopen("objs", "wb")) == NULL) {
		perror("objs");
		exit(1);
	}

	/*
	 * Open output file
	 */
	if ((fpmk = fopen("makefile", "wb")) == NULL) {
		perror("makefile");
		exit(1);
	}

	/*
	 * Copy over the invariant part
	 */
	if ((fp = fopen("make.head", "r")) == NULL) {
		perror("make.head");
		exit(1);
	}
	while ((c = getc(fp)) != EOF)
		putc(c, fpmk);
	fclose(fp);

	/*
	 * Process file database
	 */
	if ((fp = fopen("files", "r")) == NULL) {
		perror("files");
		exit(1);
	}
	while (fgets(buf, sizeof(buf), fp)) {
		if ((buf[0] == '\n') || (buf[0] == '#'))
			continue;
		l = strlen(buf)-1;	/* Trim '\n' */
		buf[l] = '\0';

		/*
		 * If on to new directory, update dir variable
		 */
		if (buf[l-1] == ':') {
			buf[l-1] = '\0';
			strcpy(dir, buf);
			continue;
		}

		/*
		 * Otherwise generate a dependency
		 */
		if (buf[l-1] == 'c') {
			buf[l-2] = '\0';
			fprintf(fpmk,
"%s.o: ../%s/%s.c\n\t$(CC) $(CFLAGS) -c ../%s/%s.c\n\n",
				buf, dir, buf, dir, buf);
		} else if (buf[l-1] == 's') {
			buf[l-2] = '\0';
			fprintf(fpmk,
"%s.o: ../%s/%s.s\n\t$(CPP) $(INCS) $(DEFS) ../%s/%s.s %s.s\n\
\t$(AS) -o %s.o %s.s\n\n",
				buf, dir, buf, dir, buf, buf, buf, buf);
		} else {
			printf("Unknown file type: %s\n", buf);
			exit(1);
		}

		/*
		 * And tack on another object to our list
		 */
		col += strlen(buf)+3;
		if (objs) {
			int len;

			len = strlen(objs)+strlen(buf)+4;
			if (col > COLS) {
				len += 3;
			}
			objs = realloc(objs, len);
			if (col > COLS) {
				strcat(objs, " \\\n\t");
				col = 8;
			} else {
				strcat(objs, " ");
			}
		} else {
			objs = malloc(strlen(buf)+3);
			objs[0] = '\0';
		}
		strcat(objs, buf);
		strcat(objs, ".o");
		fprintf(fpobj, "%s.o\n", buf);
	}
	fclose(fp);

	/*
	 * Output object list to file
	 */
	fprintf(fpmk, "OBJS=%s\n\n", objs);

	/*
	 * Output make tailer
	 */
	if ((fp = fopen("make.tail", "r")) == NULL) {
		perror("make.tail");
		exit(1);
	}
	while ((c = getc(fp)) != EOF)
		putc(c, fpmk);
	fclose(fp);

	fclose(fpmk);
	fclose(fpobj);
	return(0);
}
