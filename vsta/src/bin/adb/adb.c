/*
 * adb.c
 *	Main command handling for assembly debugger
 */
#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include "map.h"

extern char *getnum();

static pid_t corepid;	/* PID we debug, if any */
uint addr, count = 1;	/* Addr/count for each command */
static FILE *objfile;	/* Open FILE * to object file */
jmp_buf errjmp;		/* Recovery from parse errors */
static struct map	/* Mappings for a.out and core */
	textmap,
	coremap;
static struct map	/* Points to a map to use */
	*read_from;

/* Macro to skip whitespace */
#define SKIP(p) {while (isspace(*(p))) p += 1; }

/*
 * usage()
 *	Show how to invoke adb
 */
static void
usage(void)
{
	printf("Usage is: adb <prog> [ <pid> ]\n");
	exit(1);
}

/*
 * readloc()
 */
ulong
readloc(ulong addr, int size)
{
	ulong off;

	if (read_from == &coremap) {
		abort();
	} else {
		uchar c;
		ushort w;
		ulong l;

		/*
		 * vaddr 0x1000 maps to offset 0 in an a.out
		 */
		if (do_map(read_from, (void *)addr, &off)) {
			return(0);
		}
		fseek(objfile, off, 0);

		/*
		 * Read required amount of data at this location
		 */
		switch (size) {
		case 1:	fread(&c, sizeof(c), 1, objfile); return(c);
		case 2:	fread(&w, sizeof(w), 1, objfile); return(w);
		case 4:	fread(&l, sizeof(l), 1, objfile); return(l);
		}
		abort();
	}
}

/*
 * dollar_cmds()
 *	Handle $ commands
 */
static void
dollar_cmds(char *p)
{
	if (*p == 'q') {	/* Quit */
		exit(0);
	}
	printf("Unknown commands: $%s\n", p);
}

/*
 * do_dump()
 *	Actual workhorse to do dumping of memory
 *
 * Some hard-coded knowledge of word size here, sorry.
 */
static void
do_dump(char *fmt, int size, int cols)
{
	int x, col = 0;
	unsigned char c;
	unsigned short w;
	unsigned long l;

	for (x = 0; x < count; ++x) {
		switch (size) {
		case 1:
			c = readloc(addr, 1);
			printf(fmt, c);
			break;
		case 2:
			w = readloc(addr, 2);
			printf(fmt, w);
			break;
		case 4:
			l = readloc(addr, 4);
			printf(fmt, l);
			break;
		}
		col += 1;
		addr += size;
		if (col > cols) {
			printf("\n");
			col = 0;
		}
	}
	if (col) {
		printf("\n");
	}
}

/*
 * dump_s()
 *	Dump strings, with or without de-escaping
 */
static void
dump_s(int quote)
{
	uchar c;
	uint x;

	for (x = 0; x < count; ++x) {
		c = readloc(addr, sizeof(c));
		if (quote) {
			if (c & 0x80) {
				printf("M-");
				c &= ~0x80;
			}
			if (c < ' ') {
				putchar('^');
				c += 'A';
			}
			if (c == 0x7F) {
				printf("DEL");
			} else {
				putchar(c);
			}
		} else {
			putchar(c);
		}
	}
	addr += count;
}

/*
 * dump_mem()
 *	Dump memory in a variety of formats
 */
static void
dump_mem(char *p)
{
	char fmt;
	int x, cnt, oldcnt;

	/*
	 * Optional count here as well.  The two interact in a way
	 * more complex than this in Sun's adb, but I can't figure
	 * it out.
	 */
	if (isdigit(*p)) {
		p = getnum(p, &cnt);
		SKIP(p);
		cnt *= count;
	} else {
		cnt = count;
	}
	oldcnt = count;
	count = cnt;

	/*
	 * Label location, start dumping
	 */
	printf("%s:\n", nameval(addr));
	fmt = *p++;
	switch (fmt) {
	case 'x': do_dump(" %4x", 2, 8); break;
	case 'X': do_dump(" %8X", 4, 4); break;
	case 'o': do_dump(" %8o", 2, 4); break;
	case 'O': do_dump(" %12O", 4, 4); break;
	case 'd': do_dump(" %5d", 2, 8); break;
	case 'D': do_dump(" %10d", 4, 4); break;
	case 'i':
		for (x = 0; x < count; ++x) {
			addr = db_disasm(addr, 0);
		}
		break;
	case 's': dump_s(0); break;
	case 'S': dump_s(1); break;
	case 'c': do_dump(" %02x", 1, 16); break;
	default:
		printf("Unknown format '%c'\n", fmt);
		return;
	}
	count = oldcnt;
}

/*
 * colon_cmds()
 *	Do the ':' commands
 */
static void
colon_cmds(char *p)
{
	/* TBD */
}

/*
 * print_val()
 *	Print value of dot
 */
static void
print_val(char *p)
{
	char fmt[8];
	uint val;
	ulong vall;

	if (!*p) {
		p = "X";
	}
	switch (*p) {
	case 'd':
	case 'D':
	case 'x':
	case 'X':
	case 'o':
	case 'O':
		sprintf(fmt, "%%%c\n", *p);
		if (islower(*p)) {
			val = addr & 0xFFFF;
			printf(fmt, val);
		} else {
			vall = addr;
			printf(fmt, vall);
		}
		break;
	default:
		printf("Unknown format: %s\n", p);
		break;
	}
}

/*
 * cmdloop()
 *	Process commands
 *
 * General format is:
 *	<addr>,<count> <operator> <modifiers>
 *	<addr>, <count> are numbers or symbols
 *	<operator> is '/', '?', ':', '$'
 *	<modifiers> are specific to the operator
 */
static void
cmdloop(void)
{
	char buf[128], *p, c;
	static char lastcmd[32];

	/*
	 * Get a line, skip over white space
	 */
	gets(buf);
	p = buf;
	SKIP(p);

	/*
	 * Repeat last command on empty line
	 */
	if (!*p) {
		p = lastcmd;
	}

	/*
	 * If first field present, parse it.  Otherwise we inherit
	 * the address from the previous command.
	 */
	if (isalnum(*p) || (*p == '_')) {
		p = getnum(p, &addr);
		count = 1;	/* Count gets reset on explicit addr */
	}
	SKIP(p);

	/*
	 * Similarly for count
	 */
	if (*p == ',') {
		++p;
		SKIP(p);
		p = getnum(p, &count);
		SKIP(p);
	}

	/*
	 * Record last command so we can repeat
	 */
	if (p != lastcmd) {
		strncpy(lastcmd, p, sizeof(lastcmd)-1);
	}

	/*
	 * Pick out operator
	 */
	c = *p++;
	SKIP(p);
	switch (c) {
	case '?':
		read_from = &textmap;
		dump_mem(p);
		break;
	case '/':
		if (!corepid) {
			printf("No process attached\n");
			break;
		}
		read_from = &coremap;
		dump_mem(p);
		break;
	case ':':
		if (!corepid) {
			printf("No process attached\n");
			break;
		}
		colon_cmds(p);
		break;
	case '$':
		dollar_cmds(p);
		break;
	case '=':
		print_val(p);
		break;
	default:
		printf("Unknown modifier: '%c'\n", c);
		break;
	}
}

/*
 * main()
 *	Start up
 *
 * We handle:
 *	adb <prog>
 *	adb <prog> [ <pid> ]
 */
main(int argc, char **argv)
{
	/*
	 * Check out arguments
	 */
	if ((argc < 2) || (argc > 3)) {
		usage();
	}
	if (argc == 3) {
		corepid = atoi(argv[2]);
	}

	/*
	 * Read symbols from a.out, leave him open so we can
	 * do "?" examination of him.
	 */
	if ((objfile = fopen(argv[1], "r")) == NULL) {
		perror(argv[1]);
		exit(1);
	}
	rdsym(argv[1]);

	/*
	 * Now ask Mr. a.out to fill in our offsets
	 */
	map_aout(&textmap);

	/*
	 * If there's a process, set up for debugging him
	 */
	if (corepid) {
		port_name pn;
		port_t p;

		/*
		 * Create server port he will talk to us via
		 */
		p = msg_port(0, &pn);
		if (p < 0) {
			perror("Debug port");
			exit(1);
		}

		/*
		 * Tell ET to phone home.  He'll block until we start
		 * listening on our port.
		 */
		ptrace(corepid, pn);
	}

	/*
	 * For recovering from parse errors
	 */
	(void)setjmp(errjmp);

	/*
	 * Start processing commands
	 */
	for (;;) {
		cmdloop();
	}
}
