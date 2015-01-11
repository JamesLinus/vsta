/* vsta_io.c  -  VSTa disk input/output via ABC library */

/* Written 2001 by Andy Valencia */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <abc.h>

#define OUR_LINUX_IO
#include "linux.h"
#include "dosfsck.h"
#include "common.h"
#include "io.h"


typedef struct _change {
    void *data;
    loff_t pos;
    int size;
    struct _change *next;
} CHANGE;


static CHANGE *changes,*last;
static int fd,did_change = 0;

unsigned device_no;

void
fs_open(char *path, int rw)
{
    struct stat stbuf;
    
    if ((fd = open(path, rw ? O_RDWR : O_RDONLY)) < 0) {
	pdie("open %s", path);
    }
    changes = last = NULL;
    did_change = 0;

    /*
     * Set up ABC, with 1/2 meg of buffering
     */
    init_buf(__fd_port(fd), 1024);

    if (fstat(fd, &stbuf) < 0) {
	pdie("fstat",path);
    }
    device_no = S_ISBLK(stbuf.st_mode) ? (stbuf.st_rdev >> 8) & 0xff : 0;
}


/*
 * fs_read()
 *	Read filesystem data
 *
 * Returned data reflects any pending fs_write() data.
 */
void
fs_read(loff_t pos, int size, void *data)
{
    CHANGE *walk;
    loff_t data_pos = pos;
    int got, off, avail, data_size = size;
    daddr_t d;
    struct buf *b;
    unsigned char *ptr, *data_ptr = data;

    /*
     * Walk across sectors, getting them via ABC and copying out
     * contents.
     */
    while (data_size > 0) {
    	d = data_pos / SECSZ;
	b = find_buf(d, 1, ABC_FILL);
	ptr = index_buf(b, 0, 1);
	off = data_pos & (SECSZ-1);
	avail = SECSZ-off;
	if (avail > data_size) {
		avail = data_size;
	}
	bcopy(ptr + off, data_ptr, avail);
	data_size -= avail;
	data_ptr += avail;
	data_pos += avail;
    }

    /*
     * Overwrite pending changes on top of our data view
     */
    for (walk = changes; walk; walk = walk->next) {
	if (walk->pos < pos+size && walk->pos+walk->size > pos) {
	    if (walk->pos < pos)
		memcpy(data,(char *) walk->data+pos-walk->pos,min(size,
		  walk->size-pos+walk->pos));
	    else memcpy((char *) data+walk->pos-pos,walk->data,min(walk->size,
		  size+pos-walk->pos));
	}
    }
}


/*
 * fs_test()
 *	See if we can read a given sector
 */
int
fs_test(loff_t pos,int size)
{
    /* TBD: actual I/O exercise */
    return(1);
}

static void
do_write(loff_t pos, int size, void *data)
{
	unsigned int off, avail;
	struct buf *b;
	unsigned char *ptr;

	while (size > 0) {
		/*
		 * See where we are in this sector
		 */
		off = pos & (SECSZ-1);
		avail = SECSZ-off;
		if (avail > size) {
			avail = size;
		}

		/*
		 * Get the buffer.  Pull in its current contents unless
		 * we're going to overwrite the whole thing right now.
		 */
		b = find_buf(pos / SECSZ, 1, (avail == SECSZ) ? 0 : ABC_FILL);
		ptr = index_buf(b, 0, 1);
		bcopy(data, ptr + off, avail);
		dirty_buf(b, NULL);
		size -= avail;
		pos += avail;
		data = (char *)data + avail;
	}
}

/*
 * fs_write()
 *	Either write actual data, or record it in our pending changes
 */
void
fs_write(loff_t pos, int size, void *data)
{
    CHANGE *new;
    int did;

    /*
     * Immediate write of data
     */
    if (write_immed) {
	did_change = 1;
	do_write(pos, size, data);
	return;
    }

    /*
     * Record the pending change
     */
    new = alloc(sizeof(CHANGE));
    new->pos = pos;
    new->data = alloc(new->size = size);
    memcpy(new->data, data, size);
    new->next = NULL;
    if (last) {
	    last->next = new;
    } else {
	    changes = new;
    }
    last = new;
}


/*
 * fs_flush()
 *	Sync out pending changes
 */
static void
fs_flush(void)
{
    CHANGE *this;
    int size;

    while (changes) {
	this = changes;
	changes = changes->next;
	do_write(this->pos, this->size, this->data);
	free(this->data);
	free(this);
    }
    sync_bufs(NULL);
}


int fs_close(int write)
{
    CHANGE *next;
    int changed;

    changed = !!changes;
    if (write) {
	    fs_flush();
    } else {
        while (changes) {
	    next = changes->next;
	    free(changes->data);
	    free(changes);
	    changes = next;
	}
    }
    if (close(fd) < 0) {
	    pdie("closing file system");
    }
    return changed || did_change;
}


int fs_changed(void)
{
    return !!changes || did_change;
}
