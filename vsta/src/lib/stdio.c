/*
 * stdio.c
 *	Stuff in support of standard I/O
 */
#include <stdio.h>
#include <unistd.h>
#include <std.h>
#include <fcntl.h>
#include <alloc.h>

/*
 * Memory for the three predefined files.  If stdin/stdout are hooked
 * to terminals the fill/flush vectors will be set to routines which
 * do TTY-style processing.
 */
FILE __iob2[3] = {
	{_F_READ, 0, 0, 0, 0, 0, 0, 0},
	{_F_WRITE|_F_LINE, 1, 0, 0, 0, 0, 0, 0},
	{_F_WRITE|_F_UNBUF, 2, 0, 0, 0, 0, 0, 0}
};

/*
 * Return pointer to iob array
 */
FILE *
__get_iob(void)
{
	return(__iob2);
}

/*
 * Used to enumerate all open files
 */
static FILE *allfiles = 0;

/*
 * add_list()
 *	Add a FILE to a list
 */
static void
add_list(FILE **hd, FILE *fp)
{
	FILE *f = *hd;

	/*
	 * First entry
	 */
	if (f == 0) {
		*hd = fp;
		fp->f_next = fp->f_prev = fp;
		return;
	}

	/*
	 * Insert at tail
	 */
	fp->f_next = f;
	fp->f_prev = f->f_prev;
	f->f_prev = fp;
	fp->f_prev->f_next = fp;
}

/*
 * del_list()
 *	Delete FILE from list
 */
static void
del_list(FILE **hd, FILE *fp)
{
	/*
	 * Ignore those which were never active
	 */
	if (fp->f_next == 0) {
		return;
	}

	/*
	 * Last entry
	 */
	if (fp->f_next == fp) {
		*hd = 0;
		return;
	}

	/*
	 * Delete from list
	 */
	fp->f_next->f_prev = fp->f_prev;
	fp->f_prev->f_next = fp->f_next;
	*hd = fp->f_next;
}

/*
 * fillbuf()
 *	Fill buffer from block-type device
 *
 * Returns 0 on success, 1 on error.
 */
static
fillbuf(FILE *fp)
{
	int x;

	/*
	 * Always start with fresh buffer
	 */
	fp->f_pos = fp->f_buf;
	fp->f_cnt = 0;

	/*
	 * Hard EOF/ERR
	 */
 	if (fp->f_flags & (_F_EOF|_F_ERR)) {
		return(1);
	}

	/*
	 * Read next buffer-full
	 */
	x = read(fp->f_fd, fp->f_buf, fp->f_bufsz);

	/*
	 * On error, leave flag (hard errors) and return
	 */
	if (x <= 0) {
		fp->f_flags |= ((x == 0) ? _F_EOF : _F_ERR);
		return(1);
	}

	/*
	 * Update count in buffer, return success
	 */
	fp->f_cnt = x;
	return(0);
}

/*
 * flushbuf()
 *	Flush block-type buffer
 *
 * Returns 0 on success, 1 on error.
 */
static
flushbuf(FILE *fp)
{
	int x, cnt;

#ifdef XXX
	/*
	 * Hard EOF/ERR
	 * Desirable on output?  Seems not to me....
	 */
 	if (fp->f_flags & (_F_EOF|_F_ERR)) {
		return(1);
	}
#endif

	/*
	 * No data movement--always successful
	 */
	if (fp->f_cnt == 0) {
		return(0);
	}

	/*
	 * Write next buffer-full
	 */
	cnt = fp->f_cnt;
	x = write(fp->f_fd, fp->f_buf, cnt);

	/*
	 * Always leave fresh buffer
	 */
	fp->f_pos = fp->f_buf;
	fp->f_cnt = 0;
	fp->f_flags &= ~_F_DIRTY;

	/*
	 * On error, leave flag (hard errors) and return
	 */
	if (x != cnt) {
		fp->f_flags |= ((x < 0) ? _F_ERR : _F_EOF);
		return(1);
	}

	/*
	 * Return success
	 */
	return(0);
}

/*
 * setup_fp()
 *	Get buffers
 */
static
setup_fp(FILE *fp)
{
	/*
	 * This handles stdin/out/err; opens via fopen() already
	 * have their buffer.
	 */
	if (fp->f_buf == 0) {
		/*
		 * Set up buffer
		 */
		fp->f_buf = malloc(BUFSIZ);
		if (fp->f_buf == 0) {
			return(1);
		}
		fp->f_bufsz = BUFSIZ;
		fp->f_pos = fp->f_buf;
		fp->f_cnt = 0;

		/*
		 * Add to "all files" list
		 */
		if (fp->f_next == 0) {
			add_list(&allfiles, fp);
		}
	}

	/*
	 * Flag set up
	 */
	fp->f_flags |= _F_SETUP;
	return(0);
}

/*
 * set_read()
 *	Do all the cruft needed for a read
 *
 * Returns zero on success, 1 on error.
 * This represents a lot more sanity checking than most implementations.
 * XXX does it hurt enough to throw out?
 */
static
set_read(FILE *fp)
{
	/*
	 * Allowed?
	 */
	if ((fp->f_flags & _F_READ) == 0) {
		return(1);
	}

	/*
	 * If switching from write to read, flush dirty stuff out
	 */
	if (fp->f_flags & _F_DIRTY) {
		flushbuf(fp);
		fp->f_flags &= ~_F_DIRTY;
	}

	/*
	 * Fill buffer if nothing in it
	 */
	if (fp->f_cnt == 0) {
		/*
		 * Do one-time setup
		 */
	 	if ((fp->f_flags & _F_SETUP) == 0) {
			if (setup_fp(fp)) {
				return(1);
			}
		}

		/*
		 * Call fill routine
		 */
		if (fillbuf(fp)) {
			return(1);
		}
	}
	return(0);
}

/*
 * set_write()
 *	Set FILE for writing
 *
 * Returns 1 on error, 0 on success.
 */
static
set_write(FILE *fp)
{
	/*
	 * Allowed?
	 */
	if ((fp->f_flags & _F_WRITE) == 0) {
		return(1);
	}

	/*
	 * Do one-time setup
	 */
	if ((fp->f_flags & _F_SETUP) == 0) {
		if (setup_fp(fp)) {
			return(1);
		}
	}

	/*
	 * Switching modes?
	 */
	if (!(fp->f_flags & _F_DIRTY)) {
		/*
		 * Have to get file position back to where last user-seen
		 * read finished.
		 */
		if (fp->f_cnt) {
			if (lseek(fp->f_fd,
				lseek(fp->f_fd, 0L, SEEK_CUR) - fp->f_cnt,
				 SEEK_SET) == -1) {
					return(1);
			}
		}

		/*
		 * Nothing there, ready for action
		 */
		fp->f_cnt = 0;
		fp->f_pos = fp->f_buf;
		fp->f_flags |= _F_DIRTY;
	}

	return(0);
}

/*
 * fgetc()
 *	Get a character from the FILE
 */
fgetc(FILE *fp)
{
	uchar c;

	/*
	 * Sanity for reading
	 */
 	if (set_read(fp)) {
		return(EOF);
	}

	/*
	 * Pull next character from buffer
	 */
	c = *(fp->f_pos);
	fp->f_pos += 1; fp->f_cnt -= 1;
	return(c);
}

/*
 * fputc()
 *	Put a character to a FILE
 */
fputc(int c, FILE *fp)
{
	/*
	 * Sanity for writing
	 */
	if (set_write(fp)) {
		return(EOF);
	}

	/*
	 * Flush when full
	 */
	if (fp->f_cnt >= fp->f_bufsz) {
		if (flushbuf(fp)) {
			return(EOF);
		}
	}

	/*
	 * Add to buffer
	 */
	*(fp->f_pos) = c;
	fp->f_pos += 1;
	fp->f_cnt += 1;
	fp->f_flags |= _F_DIRTY;

	/*
	 * If line-buffered and newline or unbuffered, flush
	 */
	if (fp->f_flags & (_F_LINE|_F_UNBUF)) {
		if (((fp->f_flags & _F_LINE) && (c == '\n')) ||
				(fp->f_flags & _F_UNBUF)) {
			if (flushbuf(fp)) {
				return(EOF);
			}
			return(0);
		}
	}

	/*
	 * Otherwise just leave in buffer
	 */
	return(0);
}

/*
 * fdopen()
 *	Open FILE on an existing file descriptor
 */
FILE *
fdopen(int fd, const char *mode)
{
	char c;
	const char *p;
	int m = 0, x;
	FILE *fp;

	/*
	 * Get FILE *
	 */
	if ((fp = malloc(sizeof(FILE))) == 0) {
		return(0);
	}

	/*
	 * Get data buffer
	 */
	if ((fp->f_buf = malloc(BUFSIZ)) == 0) {
		free(fp);
		return(0);
	}

	/*
	 * Interpret mode of file open
	 */
	for (p = mode; c = *p; ++p) {
		switch (c) {
		case 'r':
			m |= _F_READ;
			break;
		case 'w':
			m |= _F_WRITE;
			break;
		case 'b':
			m |= /* _F_BINARY */ 0 ;
			break;
		case '+':
			m |= _F_WRITE | _F_READ;
			break;
		default:
			free(fp->f_buf);
			free(fp);
			return(0);
		}
	}

	/*
	 * Set up rest of fp now that we know it's worth it
	 */
	fp->f_fd = fd;
	fp->f_flags = m;
	fp->f_pos = fp->f_buf;
	fp->f_bufsz = BUFSIZ;
	fp->f_cnt = 0;
	fp->f_next = 0;

	/*
	 * Insert in "all files" list
	 */
	add_list(&allfiles, fp);

	return(fp);
}

/*
 * freopen()
 *	Open a buffered file on given FILE
 */
FILE *
freopen(const char *name, const char *mode, FILE *fp)
{
	const char *p;
	char c;
	int m = 0, o = 0, x;

	/*
	 * Flush old data, if any
	 */
	if (fp->f_flags & _F_DIRTY) {
		fflush(fp);
	}
	if (fp->f_buf && !(fp->f_flags & _F_UBUF)) {
		free(fp->f_buf);
	}

	/*
	 * Get data buffer
	 */
	if ((fp->f_buf = malloc(BUFSIZ)) == 0) {
		return(0);
	}

	/*
	 * Interpret mode of file open
	 */
	for (p = mode; c = *p; ++p) {
		switch (c) {
		case 'r':
			m |= _F_READ;
			o |= O_READ;
			break;
		case 'w':
			m |= _F_WRITE;
			o |= O_TRUNC|O_WRITE;
			break;
		case 'b':
			m |= /* _F_BINARY */ 0 ;
			o |= O_BINARY;
			break;
		case '+':
			m |= (_F_WRITE | _F_READ);
			o |= (O_WRITE | O_READ);
			break;
		default:
			return(0);
		}
	}

	/*
	 * Open file
	 */
	x = open(name, o);
	if (x < 0) {
		return(0);
	}
	fp->f_fd = x;

	/*
	 * Set up rest of fp now that we know it's worth it
	 */
	fp->f_flags = m;
	fp->f_pos = fp->f_buf;
	fp->f_bufsz = BUFSIZ;
	fp->f_cnt = 0;
	fp->f_next = 0;

	/*
	 * Insert in "all files" list
	 */
	add_list(&allfiles, fp);

	return(fp);
}

/*
 * fopen()
 *	Open buffered file
 */
FILE *
fopen(const char *name, const char *mode)
{
	FILE *fp;

	/*
	 * Get FILE *
	 */
	if ((fp = malloc(sizeof(FILE))) == 0) {
		return(0);
	}

	/*
	 * freopen() looks at these
	 */
	fp->f_flags = 0;
	fp->f_buf = 0;

	/*
	 * Try to open file on it
	 */
	if (freopen(name, mode, fp) == 0) {
		free(fp);
		return(0);
	}
	return(fp);
}

/*
 * fclose()
 *	Close a buffered file
 */
fclose(FILE *fp)
{
	int err = 0;

	/*
	 * Flush the buffer if necessary
	 */
	if (fp->f_flags & _F_DIRTY) {
		flushbuf(fp);
	}

	/*
	 * Remove from "all files" list
	 */
	del_list(&allfiles, fp);

	/*
	 * Close fd, free buffer space
	 */
	err |= close(fp->f_fd);
	if (fp->f_buf && !(fp->f_flags & _F_UBUF)) {
		free(fp->f_buf);
	}
	if ((fp < &__iob2[0]) || (fp >= &__iob2[3])) {
		free(fp);
	}

	return(err);
}

/*
 * fflush()
 *	Flush out buffers in FILE
 */
int
fflush(FILE *fp)
{
	/*
	 * No data--already flushed
	 */
	if ((fp->f_flags & _F_DIRTY) == 0) {
		return(0);
	}

	/*
	 * Call flush
	 */
	if (flushbuf(fp)) {
		return(EOF);
	}
	return(0);
}

/*
 * __allclose()
 *	Close all open FILE's
 */
void
__allclose(void)
{
	int x;
	FILE *fp, *start;

	/*
	 * Walk list of all files, flush out dirty buffers
	 */
	fp = start = allfiles;
	if (!fp) {
		return;
	}
	do {
		if (fp->f_flags & _F_DIRTY) {
			fflush(fp);
		}
		close(fp->f_fd);
		fp = fp->f_next;
	} while (fp != start);
}

/*
 * gets()
 *	Get a string, discard terminating newline
 *
 * Both gets() and fgets() have extra brain damage to coexist with
 * MS-DOS editors.
 */
char *
gets(char *buf)
{
	int c;
	char *p = buf;

	while ((c = fgetc(stdin)) != EOF) {
		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			break;
		}
		*p++ = c;
	}
	if (c == EOF) {
		return(0);
	}
	*p = '\0';
	return(buf);
}

/*
 * fgets()
 *	Get a string, keep terminating newline
 */
char *
fgets(char *buf, int len, FILE *fp)
{
	int x = 0, c;
	char *p = buf;

	while ((x++ < (len - 1)) && ((c = fgetc(fp)) != EOF)) {
		if (c == '\r') {
			continue;
		}
		*p++ = c;
		if (c == '\n') {
			break;
		}
	}
	if (c == EOF) {
		return(0);
	}
	*p++ = '\0';
	return(buf);
}

/*
 * ferror()
 *	Tell if there's an error on the buffered file
 */
ferror(FILE *fp)
{
 	return (fp->f_flags & _F_ERR);
}

/*
 * feof()
 *	Tell if at end of media
 */
feof(FILE *fp)
{
 	return (fp->f_flags & _F_EOF);
}

/*
 * fileno()
 *	Return file descriptor value
 */
fileno(FILE *fp)
{
	return (fp->f_fd);
}

/*
 * clearerr()
 *	Clear error state on file
 */
void
clearerr(FILE *fp)
{
	fp->f_flags &= ~(_F_EOF|_F_ERR);
}

/*
 * setbuffer()
 *	Set buffer with given size
 */
void
setbuffer(FILE *fp, char *buf, uint size)
{
	if (buf == 0) {
		fp->f_flags |= _F_UNBUF;
		return;
	}
	fp->f_pos = fp->f_buf = buf;
	fp->f_bufsz = size;
	fp->f_cnt = 0;
	fp->f_flags |= _F_UBUF;
}
/*
 * setbuf()
 *	Set buffering for file
 */
void
setbuf(FILE *fp, char *buf)
{
	setbuffer(fp, buf, BUFSIZ);
}

/*
 * puts()
 *	Put a string, add a newline
 */
puts(const char *s)
{
	char c;

	while (c = *s++) {
		if (fputc(c, stdout) == EOF) {
			return(EOF);
		}
	}
	if (fputc('\n', stdout) == EOF) {
		return(EOF);
	}
	return(0);
}

/*
 * fputs()
 *	Put a string, no tailing newline (in the string already, probably)
 */
fputs(const char *s, FILE *fp)
{
	char c;

	while (c = *s++) {
		if (fputc(c, fp) == EOF) {
			return(EOF);
		}
	}
	return(0);
}

/*
 * fwrite()
 *	Write a buffer
 *
 * This could be sped up by noting the remaining buffer space and
 * blasting it all in a single bcopy().  Issues would remain WRT
 * end-of-line handling, etc.
 */
fwrite(const void *buf, int size, int nelem, FILE *fp)
{
	const char *p;
	uint len, x;

	p = buf;
	len = size * nelem;
	x = 0;
	while (x < len) {
		if (fputc(*p, fp)) {
			return(x / size);
		}
		++p; ++x;
	}
	return(nelem);
}

/*
 * fread()
 *	Read a buffer
 */
fread(void *buf, int size, int nelem, FILE *fp)
{
	char *p;
	uint len, x;
	int c;

	p = buf;
	len = size * nelem;
	x = 0;
	while (x < len) {
		c = fgetc(fp);
		if (c == EOF) {
			return(x / size);
		}
		*p++ = c;
		++x;
	}
	return(nelem);
}

/*
 * ungetc()
 *	Push back a character
 */
ungetc(int c, FILE *fp)
{
	/*
	 * Ensure state of buffered file allows for pushback
	 * of data.
	 */
	if ((c == EOF) ||
			!(fp->f_flags & _F_READ) ||
			(fp->f_buf == 0) ||
			(fp->f_pos == fp->f_buf) ||
			(fp->f_flags & (_F_DIRTY|_F_ERR))) {
		return(EOF);
	}

	/*
	 * If he hit EOF, then move back to pre-EOF state
	 */
	fp->f_flags &= ~(_F_EOF);

	/*
	 * Add data to buffer
	 */
	fp->f_pos -= 1;
	(fp->f_pos)[0] = c;
	fp->f_cnt += 1;
	return(c);
}

/*
 * ftell()
 *	Tell position of file
 *
 * Needs to keep in mind both underlying file position and state
 * of buffer.
 */
off_t
ftell(FILE *fp)
{
	long l;

	/*
	 * Get basic position in file
	 */
	l = lseek(fp->f_fd, 0L, SEEK_CUR);

	/*
	 * Dirty buffer--position is file position plus amount
	 * of buffered data.
	 */
	if (fp->f_flags & _F_DIRTY) {
		return(l + fp->f_cnt);
	}

	/*
	 * Clean buffer--position is file position minus amount
	 * buffered but not yet read.
	 */
	return(l - fp->f_cnt);
}

/*
 * fseek()
 *	Set buffered file position
 */
off_t
fseek(FILE *fp, off_t off, int whence)
{
	/*
	 * Clear out any pending dirty data.  Reset buffering so
	 * we will fill from new position.
	 */
	if (fp->f_flags & _F_DIRTY) {
		flushbuf(fp);
	} else {
		fp->f_pos = fp->f_buf;
		fp->f_cnt = 0;
	}

	/*
	 * Flush out any sticky conditions
	 */
	clearerr(fp);

	/*
	 * Let lseek() do the work - we have slightly different return
	 * results however
	 */
	return((lseek(fp->f_fd, off, whence) >= 0) ? 0 : -1);
}

/*
 * rewind()
 *	Set file position to start
 */
void
rewind(FILE *fp)
{
	fseek(fp, (off_t)0, SEEK_SET);
}

/*
 * tmpfile()
 *	Open a tmp file
 */
FILE *
tmpfile(void)
{
	char *buf;

	buf = alloca(32);
	if (buf == 0) {
		return(0);
	}
	sprintf(buf, "/tmp/tf%d", getpid());
	return(fopen(buf, "w"));
}
