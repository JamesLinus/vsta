#ifndef _STDIO_H
#define _STDIO_H
/*
 * stdio.h
 *	My poor little stdio emulation
 */
#include <sys/types.h>
#include <stdarg.h>

/*
 * A open, buffered file
 */
typedef struct __file {
	ushort f_flags;			/* See below */
	ushort f_fd;			/* File descriptor we use */
	uchar *f_buf, *f_pos;		/* Buffer */
	int f_bufsz, f_cnt;		/*  ...size, bytes queued */
	struct __file			/* List of all open files */
		*f_next, *f_prev;
} FILE;

/*
 * Bits in f_flags
 */
#define _F_WRITE (1)		/* Can write */
#define _F_READ (2)		/*  ...read */
#define _F_DIRTY (4)		/* Buffer holds new data */
#define _F_EOF (8)		/* Sticky EOF */
#define _F_ERR (16)		/*  ...error */
#define _F_UNBUF (32)		/* Flush after each put */
#define _F_LINE (64)		/*  ...after each newline */
#define _F_SETUP (128)		/* Buffers, etc. set up now */
#define _F_UBUF (256)		/* User-provided buffer */

/*
 * These need to be seen before getc()/putc() are defined
 */
extern int fgetc(FILE *), fputc(int, FILE *);

#ifdef __GNUC__
/*
 * The following in-line functions replace the more classic UNIX
 * approach of gnarly ?: constructs.  I think that inline C in
 * a .h is disgusting, but a step forward from unreadably
 * complex C expressions.
 */

/*
 * getc()
 *	Get a char from a stream
 */
inline extern int
getc(FILE *fp)
{
	if (((fp->f_flags & (_F_READ|_F_DIRTY)) != _F_READ) ||
			(fp->f_cnt == 0)) {
		return(fgetc(fp));
	}
	fp->f_cnt -= 1;
	fp->f_pos += 1;
	return(fp->f_pos[-1]);
}

/*
 * putc()
 *	Put a char to a stream
 */
inline extern int
putc(char c, FILE *fp)
{
	if (((fp->f_flags & (_F_WRITE|_F_SETUP|_F_DIRTY)) !=
			(_F_WRITE|_F_SETUP)) ||
			(fp->f_cnt >= fp->f_bufsz) ||
			((fp->f_flags & _F_LINE) && (c == '\n'))) {
		return(fputc(c, fp));
	}
	*(fp->f_pos) = c;
	fp->f_pos += 1;
	fp->f_cnt += 1;
	fp->f_flags |= _F_DIRTY;
	return(0);
}
#else

/*
 * For non-GNU C, just live with the function call overhead for now
 */
#define getc(f) fgetc(f)
#define putc(c, f) fputc(c, f)

#endif

/*
 * Smoke and mirrors
 */
#define getchar() getc(stdin)
#define putchar(c) putc(c, stdout)

/*
 * Pre-allocated stdio structs
 */
extern FILE *__iob, *__get_iob();
#define stdin (__iob)
#define stdout (__iob+1)
#define stderr (__iob+2)

/*
 * stdio routines
 */
extern FILE *fopen(const char *fname, const char *mode),
	*freopen(const char *, const char *, FILE *),
	*fdopen(int, const char *);
extern int fclose(FILE *),
	fread(void *, int, int, FILE *),
	fwrite(const void *, int, int, FILE *),
	feof(FILE *), ferror(FILE *),
	fileno(FILE *), ungetc(int, FILE *);
extern off_t fseek(FILE *, off_t, int), ftell(FILE *);
extern char *gets(char *), *fgets(char *, int, FILE *);
extern int puts(const char *), fputs(const char *, FILE *);
extern int getw(FILE *), putw(int, FILE *);
extern void clearerr(FILE *);
extern int setvbuf(FILE *, char *, int, size_t);
extern void setbuf(FILE *, char *),
	setbuffer(FILE *, char *, size_t),
	setlinebuf(FILE *);
extern void rewind(FILE *);
extern int fflush(FILE *);
extern int printf(const char *, ...),
	fprintf(FILE *, const char *, ...),
	sprintf(char *, const char *, ...);
extern int vprintf(const char *, va_list),
	vfprintf(FILE *, const char *, va_list),
	vsprintf(char *, const char *, va_list);
extern int scanf(const char *, ...),
	fscanf(FILE *, const char *, ...),
	sscanf(char *, const char *, ...);
extern int vscanf(const char *, va_list),
	vfscanf(FILE *, const char *, va_list),
	vsscanf(char *, const char *, va_list);
extern FILE *tmpfile(void);
extern FILE *popen(const char *, const char *);
extern int pclose(FILE *);

/*
 * Pseudo-FDL at the FILE * layer
 */
extern FILE *funopen(void *, intfun, intfun, void *, intfun);
#define fropen(cookie, readfn) funopen(cookie, readfn, 0, 0, 0)
#define fwopen(cookie, writefn) funopen(cookie, 0, writefn, 0, 0)

/*
 * Buffer strategy constants
 */
#define _IOFBF 0		/* Fully buffered */
#define _IOLBF 1		/* Line buffered */
#define _IONBF 2		/* Not buffered */

/*
 * Miscellany
 */
#if !defined(TRUE) && !defined(FALSE)
#define TRUE (1)
#define FALSE (0)
#endif
#define EOF (-1)
#if !defined(MIN) && !defined(MAX)
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif
#define BUFSIZ (4096)
#ifndef NULL
#define NULL (0)
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) <= (b)) ? (a) : (b))
#endif

#endif /* _STDIO_H */
