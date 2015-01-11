/*
 * printf.c
 *	Interface to printf()-like function from shell
 */
#include <stdio.h>
#include <alloc.h>
#include <ctype.h>
#include <string.h>
#include <std.h>

#define MAXBUF 1024

static char *prog;

/*
 * get_fmt()
 *	Extract format portion of printf() string; return length
 */
static int
get_fmt(char *in, char *out)
{
	int x;
	char c, *inp = in;

	out[0] = in[0];
	for (x = 1; in[x]; ++x) {
		c = out[x] = in[x];
		if (!isdigit(c) && (c != '.') && (c != '-')) {
			break;
		}
	}
	if (!in[x]) {
		fprintf(stderr, "%s: malformed format at '%s'\n", prog, inp);
		exit(1);
	}
	out[++x] = '\0';
	return(x);
}

int
main(int argc, char **argv)
{
	int x, arg, len, fmtlen, val;
	char c, *fmt, *buf;

	if (argc < 2) {
		fprintf(stderr, "Usage is: %s <fmt> [<arg> ...]\n", argv[0]);
		exit(1);
	}
	prog = argv[0];

	/*
	 * Grab some needed values into local variables
	 */
	len = strlen(argv[1]);
	if (len >= MAXBUF) {
		fprintf(stderr, "%s: buffer overrun\n", prog);
		exit(1);
	}
	fmt = argv[1];
	arg = 2;

	/*
	 * Worst-case buffering; size of arg
	 */
	buf = alloca(len+1);

	/*
	 * Walk format string
	 */
	for (x = 0; x < len; ) {
		/*
		 * Basic chars, output
		 */
		c = fmt[x];
		if (c != '%') {
			if (c == '\\') {
				c = fmt[++x];
				if (!c) {
					fprintf(stderr,
						"%s: incomplete string\n", prog);
					exit(1);
				}
				if (isdigit(c)) {
					(void)sscanf(fmt+x, "%3o", &val);
					c = val;
					for (val = 0; val < 3; ++val) {
						if (!isdigit(fmt[x])) {
							break;
						}
						x += 1;
					}
				} else {
					switch (c) {
					case 'n':
						c = '\n';
						break;
					case 'r':
						c = '\r';
						break;
					case 't':
						c = '\t';
						break;
					default:
						break;
					}
					x += 1;
				}
				putchar(c);
			} else {
				x += 1;
				putchar(c);
			}
			continue;
		}

		/*
		 * Extract format into buffer
		 */
		fmtlen = get_fmt(fmt+x, buf);
		x += fmtlen;

		/*
		 * Parse base format type
		 */
		switch (buf[fmtlen-1]) {
		case '%':
			putchar('%');
			break;
		case 'c':
			if (sscanf(argv[arg++], "%d", &val) != 1) {
				fprintf(stderr, "%s: bad char value '%s'\n",
					prog, argv[arg-1]);
				exit(1);
			}
			printf("%c", val);
			break;
		case 'x':
		case 'X':
			if (sscanf(argv[arg++], "%x", &val) != 1) {
				fprintf(stderr, "%s: bad hex '%s'\n",
					prog, argv[arg-1]);
				exit(1);
			}
			printf(buf, val);
			break;
		case 'd':
		case 'D':
			if (sscanf(argv[arg++], "%d", &val) != 1) {
				fprintf(stderr, "%s: bad decimal '%s'\n",
					prog, argv[arg-1]);
				exit(1);
			}
			printf(buf, val);
			break;
		case 'o':
		case 'O':
			if (sscanf(argv[arg++], "%o", &val) != 1) {
				fprintf(stderr, "%s: bad octal '%s'\n",
					prog, argv[arg-1]);
				exit(1);
			}
			printf(buf, val);
			break;
		case 's':
			printf(buf, argv[arg++]);
			break;
		default:
			fprintf(stderr, "%s: unknown format '%s'\n",
				prog, buf);
			exit(1);
		}
	}
	return(0);
}
