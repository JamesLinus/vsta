/*
 * Filename:	dumpsect.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	6th March 1994
 * Last Update: 14th March 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description:	Utility to read a file sector.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


/*
 * usage()
 *	When the user's tried something illegal we tell them what was valid
 */
static void usage(char *util_name)
{
  fprintf(stderr, "Usage: %s <file_name> <sector_number>\n", util_name);
  exit(1);
}


/*
 * print_stat()
 *	Print the stat field of the specified file, returning 0 for success
 *
 * We do a few checks, and only display data that was actually read - in other
 * words we take some notice of EOF markers
 */
static int print_sect(char *name, int sect)
{
  FILE *fd;
  int x = 0, y = 0, xh, rem, rd;
  char dat[512];
	
  /*
   * Open the specified file
   */
  fd = fopen(name, "rb");
  if (fd == NULL) {
    perror(name);
    return 1;
  }

  /*
   * Position at the appropriate sector
   */
  fseek(fd, sect * 512, SEEK_SET);
  rd = fread(dat, 1, 512, fd); 
  fclose(fd);
  xh = rd / 0x18;
  rem = rd % 0x18;

  /*
   * Display the sector contents
   */
  for(x = 0; x < xh; x++) {
    printf("%04x: ", (x * 0x18));
    for(y = 0; y < 0x18; y++) {
      printf("%02x ", (uchar)dat[(x * 0x18) + y]);
    }
    printf("\n");
  }

  if (rem) {
    printf("%04x: ", (x * 0x18));
    for(y = 0; y < rem; y++) {
      printf("%02x ", (uchar)dat[rd - rem + y]);
    }
    printf("\n");
  }

  return 0;
}


/*
 * main()
 *	Sounds like a good place to start things :-)
 */
int main(int argc, char **argv)
{
  int exit_code = 0;
  int x;
	
  /*
   * Do we have anything that looks vaguely reasonable
   */
  if (argc < 3) {
    usage(argv[0]);
  }

  sscanf(argv[2], "%d", &x);
  exit_code = print_sect(argv[1], x);
	
  return exit_code;
}
