#include <stdio.h>

main()
{
	FILE *fp;
	char buf[80];
	int x;

	fp = fopen("test.c", "r");
	x = fread(buf, 10, 1, fp);
	printf("Read of 10 gets: %d\n", x);
	write(1, buf, 10);
	printf("\nUnget '*'\n");
	ungetc('*', fp);
	x = fread(buf, 10, 1, fp);
	printf("Read of 10 gets: %d\n", x);
	write(1, buf, 10);
	fclose(fp);
	return(0);
}
