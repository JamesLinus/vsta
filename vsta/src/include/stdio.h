#ifndef _STDIO_H
#define _STDIO_H
/*
 * stdio.h
 *	My poor little stdio emulation
 */
#include <sys/types.h>

/*
 * A open, buffered file
 */
typedef struct __file {
	ushort f_flags;			/* See below */
	ushort f_fd;			/* File descriptor we use */
	char *f_buf, *f_pos;		/* Buffer */
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
 * Smoke and mirrors
 */
#define getc(f) fgetc(f)
#define putc(c, f) fputc(c, f)
#define getchar() fgetc(stdin)
#define putchar(c) fputc(c, stdout)

/*
 * Pre-allocated stdio structs
 */
extern FILE __iob[3];
#define stdin (&__iob[0])
#define stdout (&__iob[1])
#define stderr (&__iob[2])

/*
 * stdio routines
 */
extern FILE *fopen(char *fname, char *mode),
	*freopen(char *, char *, FILE *),
	*fdopen(int, char *);
extern int fclose(FILE *),
	fread(void *, int, int, FILE *),
	fwrite(void *, int, int, FILE *),
	feof(FILE *), ferror(FILE *),
	getc(FILE *), putc(int, FILE *),
	fgetc(FILE *), fputc(int, FILE *),
	fileno(FILE *), ungetc(int, FILE *);
extern off_t fseek(FILE *, off_t, int), ftell(FILE *);
extern char *gets(char *), *fgets(char *, int, FILE *);
extern int puts(char *), fputs(char *, FILE *);
extern void clearerr(FILE *), setbuf(FILE *, char *),
	setbuffer(FILE *, char *, uint);
#ifndef __PRINTF_INTERNAL
/*
 * These prototypes are guarded by an #ifdef so that our actual
 * implementation can add an extra parameter, so that it can take
 * the address of the first arg from the stack.
 *
 * We *should* use varargs/stdargs, but these interfaces are
 * unpleasant.
 */
extern int printf(const char *, ...),
	fprintf(FILE *, const char *, ...),
	sprintf(char *, const char *, ...);
#endif

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
