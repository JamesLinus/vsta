/*
 * Filename:	stat.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	3rd February 1994
 * Last Update: 13th May 1994
 * Implemented:	GNU GCC version 1.42 (VSTa 1.3.1)
 *
 * Description:	Utility to read the status fields of a file.
 */


#include <fcntl.h>
#include <fdl.h>
#include <stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/fs.h>
#include <sys/msg.h>


extern port_t path_open(char *, int);


/*
 * usage()
 *	When the user's tried something illegal we tell them what was valid
 */
static void usage(char *util_name)
{
  fprintf(stderr,
  	  "Usage: %s [-r] [-s] [-v] [-w] [-p] <path_name> <fields_names...>\n",
          util_name);
  exit(1);
}


/*
 * read_stat()
 *	Print a stat field from the specified file.
 *
 * If the specified field is NULL we pick up all of the details.  Returns 0
 * for success, non-zero otherwise.
 */
static int read_stat(char *name, int use_port, char *field, int verbose)
{
  int fd;
  int x = 0, y = 0;
  char *statstr;
  char fmtstr[512];
  port_t pt;
	
  if (!use_port) {
    /*
     * Open the named file, stat it and then close it!
     */
    fd = open(name, O_RDONLY);
    if (fd < 0) {
      perror(name);
      return 1;
    }
    statstr = rstat(__fd_port(fd), field);
    close(fd);
  } else {
    pt = path_open(name, ACC_CHMOD);
    if (pt < 0) {
      perror(name);
      return 1;
    }
    statstr = rstat(pt, field);
    msg_disconnect(pt);
  }

  if (statstr == NULL) {
    perror(name);
    return 0;
  }

  /*
   * Format the result in an appropriate way
   */
  if (field) {
    if (verbose) {
      printf("\t%s=", field);
    }
    printf("%s\n", statstr);
  } else {
    /*
     * Scan the stat string, expanding the first char in any line
     * so that it is preceded by a tab if we're verbose!
     */
    if (verbose) {
      fmtstr[y++] = '\t';
      while (statstr[x] != '\0') {
        fmtstr[y++] = statstr[x++];
        if (statstr[x - 1] == '\n') {
          fmtstr[y++] = '\t';
        }
      }
      fmtstr[y - 1] = '\0';
      printf("%s", fmtstr);
    } else {
      /*
       * If we're not verbose, just dump the results out raw
       */
      printf("%s", statstr);
    }
  }

  return 0;		/* Return back OK status */
}


/*
 * write_stat()
 *	Write a stat field to the specified file.
 */
static int write_stat(char *name, int use_port, char *field, int verbose)
{
  int fd;
  int rcode;
  char statstr[128];
  port_t pt;
	
  strcpy(statstr, field);
  strcat(statstr, "\n");
  if (verbose) {
    printf("\t%s", statstr);
  }
	
  if (!use_port) {
    /*
     * Open the named file, stat it and then close it!
     */
    fd = open(name, O_WRONLY);
    if (fd < 0) {
      perror(name);
      return 1;
    }
    rcode = wstat(__fd_port(fd), statstr);
    close(fd);
  } else {
    pt = path_open(name, ACC_CHMOD);
    if (pt < 0) {
      perror(name);
      return 1;
    }
    rcode = wstat(pt, statstr);
    msg_disconnect(pt);
  }

  return rcode;
}


/*
 * main()
 *	Sounds like a good place to start things :-)
 */
int main(int argc, char **argv)
{
  int exit_code = 0;
  int x;
  char c;
  int doing_wstat = 0, verbose = 0, using_port = 0;
  port_t pt;

  /*
   * Do we have anything that looks vaguely reasonable
   */
  if (argc < 2) {
    usage(argv[0]);
  }

  /*
   * Scan for command line options
   */
  while ((c = getopt(argc, argv, "prsvw")) != EOF) {
    switch(c) {
    case 'p' :			/* Use port name instead of file name */
      using_port = 1;
      break;
      
    case 'r' :			/* Read stat info */
      doing_wstat = 0;
      break;

    case 's' :			/* Silent */
      verbose = 0;
      break;

    case 'v' :			/* Verbose */
      verbose = 1;
      break;
    
    case 'w' :			/* Write stat info */
      doing_wstat = 1;
      break;

    default :			/* Only error's should get us here? */
      usage(argv[0]);
    }
  }

  if (doing_wstat) {
    if (optind == argc) {
      usage(argv[0]);
    }

    /*
     * Handle any necessary verbosity
     */
    if (verbose) {
      printf("%s: sending FS_WSTAT message:\n", argv[optind]);
    }

    /*
     * We're writing, so loop round writing stat codes
     */
    for (x = optind + 1; x < argc; x++) {
      if ((exit_code = write_stat(argv[optind], using_port,
      				  argv[x], verbose))) {
        return exit_code;
      }
    }
  } else {
    /*
     * Handle any necessary verbosity
     */
    if (verbose) {
      printf("%s: sending FS_STAT message:\n", argv[optind]);
    }
    if (argc == optind) {
      usage(argv[0]);
    } else if (argc == optind + 1) {
      argc++;
      argv[optind + 1] = NULL;
    }

    /*
     * We're reading, so loop round displaying the specified stat codes
     */
    for (x = optind + 1; x < argc; x++) {
      if ((exit_code = read_stat(argv[optind], using_port,
      				 argv[x], verbose))) {
        return exit_code;
      }
    }		
  }
  return 0;
}
