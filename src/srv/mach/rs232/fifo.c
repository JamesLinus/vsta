/*
 * fifo.c
 *	Those routines which aren't done in-line
 */
#include <std.h>
#include "fifo.h"

/*
 * fifo_alloc()
 *	Allocate a FIFO
 */
struct fifo *
fifo_alloc(int size)
{
	struct fifo *f;

	f = malloc(sizeof(struct fifo));
	if (f == 0) {
		return(0);
	}
	f->f_buf = malloc(size);
	if (f->f_buf == 0) {
		free(f);
		return(0);
	}
	f->f_size = size;
	f->f_cnt = f->f_hd = f->f_tl = 0;
	return(f);
}

/*
 * fifo_free()
 *	Free a FIFO
 */
void
fifo_free(struct fifo *f)
{
	free(f->f_buf);
	free(f);
}
