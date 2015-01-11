#include <sys/msg.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <sys/namer.h>
#include <alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "rd.h"

#ifdef HAVE_ZLIB

void rd_load(struct node *n)
{
	char buf[1024];
	int  bytesread,total;
	char *ptr;
	gzFile *fl;
	
	total = 0;
	ptr = n->n_buf;
	
	if((fl = gzopen(n->n_image,"rb")) == NULL) {
		fprintf(stderr,"rd: unable to open image '&s' - aborting.\n",
			n->n_image);
		exit(1);
	}

	while((bytesread = gzread(fl,buf,1024)) > 0) {
		if(bytesread + total > n->n_size) {
			fprintf(stderr,"rd: image is larger than ram drive -"\
				" aborting.\n");
			exit(1);
		}
		memcpy(ptr,buf,bytesread);
		ptr += bytesread;
		total += bytesread;		
	}
	/* image may be smaller */
	n->n_size = total;

	gzclose(fl);	
}

#else

void rd_load(struct node *n)
{
	char buf[1024];
	int  bytesread,total;
	char *ptr;
	FILE *fl;
	
	total = 0;
	ptr = n->n_buf;
	
	if((fl = fopen(n->n_image,"rb")) == NULL) {
		fprintf(stderr,"rd: unable to open image '&s' - aborting.\n",
			n->n_image);
		exit(1);
	}

	while((bytesread = fread(buf,sizeof(char),1024, fl)) > 0) {
		if(bytesread + total > n->n_size) {
			fprintf(stderr,"rd: image is larger than ram drive -"\
				" aborting.\n");
			exit(1);
		}
		memcpy(ptr,buf,bytesread);
		ptr += bytesread;
		total += bytesread;		
	}
	/* image may be smaller */
	n->n_size = total;

	fclose(fl);	
}

#endif
