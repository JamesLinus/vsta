/*
 * setterm - adjust virtual terminal attributes 
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

/* forward declarations */
int getcolor(const char *arg);
void usage();

/* entry point */
int main(int argc, char **argv) 
{
	int fcolor = -1;
	int bcolor = -1;
	int x;
	
	if(argc < 2) {
		usage();
		return 1;
	}
	
	while((x = getopt(argc,argv,"f:b:h")) > 0) {
		switch(x)
		{
			case 'h':
			usage();
			return 0;
			break;
			
			case 'f':
			/* foreground color */
			if((fcolor = getcolor(optarg)) == -1) {
				usage();
				return 1;
			}	
			break;
			
			case 'b':
			
			/* background color */
			if((bcolor = getcolor(optarg)) == -1) {
				usage();
				return 1;
			}	
			break;
		}
	}
	
	if(optind < argc) {
		usage();
		return 1;
	}

	if(fcolor != -1) {
		printf("\33[3%dm",fcolor);
	}	
	
	if(bcolor != -1) {
		printf("\33[4%dm",bcolor);
	}	
	
	return 0;
}
/* 
 * parse arg, return color zero based index 
 * -1 on error 
 */
int getcolor(const char *arg)
{
	static char *cnames[] = {
		"black",
		"red",
		"green",
		"yellow",
		"blue",
		"magenta",
		"cyan",
		"white" };
		
	static int num = sizeof(cnames)/sizeof(char *);
	int i;
			
	if(arg == NULL) {
		return -1;
	}
	for(i = 0; i < num; i++) {
		if(stricmp(arg,cnames[i]) == 0)
			return i;
	}
	return -1;
}

/* show program usage */
void usage()
{
	printf("setterm [-f color] [-b color]\n");
	printf("\t-f set foreground color\n");
	printf("\t-b set background color\n");
	printf("\tvalid colors: black, red, green, yellow, " \
		"blue, magenta, cyan, and white.\n");		
}

