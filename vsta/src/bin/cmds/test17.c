#include <stdio.h>

#define CYCLES 16

static void
report(long t1, ulong cnt)
{
	long secs, t2;

	time(&t2);
	secs = t2-t1;
	if (secs == 0) {
		printf("%dK in < 1 sec\n", cnt/1024);
	} else {
		printf("%dK/sec\n", (cnt/secs)/1024);
	}
}

main(int argc, char **argv)
{
	int c, x;
	long cnt, t1;
	FILE *fp;

	/*
	 * Open file, pull into cache
	 */
	if ((fp = fopen(argv[1], "r")) == NULL) {
		perror(argv[1]);
		exit(1);
	}
	while ((c = getc(fp)) != EOF) {
		cnt += 1;
	}
	printf("File length: %ld bytes\n", cnt);

	/*
	 * Pass 1: count characters with in-line getc()
	 */
	cnt = 0;
	time(&t1);
	for (x = 0; x < CYCLES; ++x) {
		fseek(fp, 0L, 0);
		while ((c = getc(fp)) != EOF) {
			cnt += 1;
		}
	}
	report(t1, cnt);

	/*
	 * Pass 2: fgetc--should be slower, since it's a function call
	 */
	cnt = 0;
	time(&t1);
	for (x = 0; x < CYCLES; ++x) {
		if (fseek(fp, 0L, 0) != 0) {
			perror("fseek");
		}
		while ((c = fgetc(fp)) != EOF) {
			cnt += 1;
		}
	}
	report(t1, cnt);

	fclose(fp);
	return(0);
}
