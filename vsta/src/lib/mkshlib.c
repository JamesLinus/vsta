/*
 * mkshlib.c
 *	Create files needed for a VSTa shared library
 *
 * This involves several things:
 *	- Creating stubs for each global text symbol exported
 *	- Appending the stub handler to map the shared library
 *	- Reading in the database for the shared library
 *	- Noting unknown additions and missing entries
 *
 * Input files:
 *	- LIB.db	Database describing entry points
 *	- LIB.tmp	All the object files for the library
 * Output files:
 *	- LIB.shl	Relocated object with prefix jump table
 *	- LIB.a		Library of stubs to each exported function
 * Auxilary files:
 *	- shlib.o	Bootstrap loader of "ld.shl"
 *
 * This code uses other utilities and system() whenever possible.
 * Symbol tables are extracted using nm(1); stub .o's are generated
 * by writing out .c's and then invoking gcc(1).  .a's are created
 * by calling ar(1).
 *
 * Note that the order within the shared library database file is
 * critical and must be preserved.  Add new entries at the end.
 *
 * This code isn't very careful about checking returned error values.
 * In the words of Ken Thompson "The experienced user will usually
 * know what's wrong".
 */
#include <mach/vm.h>		/* For SHLIB_BASE */
#include <mach/aout.h>		/* For sizeof(struct aout) */
#include <sys/param.h>		/* For roundup() */
#include <stdio.h>
#include <string.h>
#include <alloc.h>

#undef DEBUG

static char *curlib,	/* Name of library being built */
	*curfile,	/* Filename being processed */
	*curinput,	/* <library>.tmp */
	*curoutput,	/* <library>.shl */
	*libname;	/* <library> */
static ulong lib_base	/* Base of each library */
	= SHLIB_BASE;
static char **obj_syms;	/* Symbols in the actual library */
static int nobj_syms;	/*  ...count */
static char **db_syms;	/* Symbols in library database */
static int ndb_syms;	/*  ...count */
static char **db_hide;	/* Symbols global but not exported */
static int ndb_hide;	/*  ...count */
static uint page_size;	/* getpagesize() value */

static void objfile(char *);

/*
 * do_system()
 *	Just a wrapper for debugging
 */
static void
do_system(char *cmd)
{
#ifdef DEBUG
	int x;

	printf("Run: %s\n", cmd);
	x =
#else
	(void)
#endif
	system(cmd);
#ifdef DEBUG
	printf(" --returned %d\n", x);
#endif
}

/*
 * obj_sym()
 *	Add another symbol name to the list we keep of object file symbols
 */
static void
obj_sym(char *p)
{
	obj_syms = realloc(obj_syms, ++nobj_syms * sizeof(char *));
	obj_syms[nobj_syms-1] = strdup(p);
}

/*
 * start_library()
 *	Set up to build a new shared library interface
 */
static void
start_library(char *name)
{
	char *p;

	/*
	 * Record library name, build only one at a time
	 */
	if (curlib) {
		fprintf(stderr, "Already building library '%s'\n", curlib);
		exit(1);
	}
	curlib = strdup(name);
	printf("Library %s @ 0x%lx\n", curlib, lib_base);

	/*
	 * Generate the <lib>, <lib>.shl and <lib>.tmp versions of name
	 */
	libname = strdup(curlib);
	p = strrchr(libname, '.');
	if (p == NULL) {
		fprintf(stderr, "Illegal library name: %s\n", curlib);
		exit(1);
	}
	*p = '\0';
	curoutput = malloc(strlen(libname) + 5);
	sprintf(curoutput, "%s.shl", libname);
	curinput = malloc(strlen(libname) + 5);
	sprintf(curinput, "%s.tmp", libname);

	/*
	 * Feed in the symbols from the -r input
	 */
	objfile(curinput);
}

/*
 * cleanup()
 *	Free all elements of the array
 */
static void
cleanup(char **vec)
{
	char **pp = vec;

	while (*pp) {
		free(*pp++);
	}
	free(vec);
}

/*
 * end_library()
 *	Clean up in preparation for the next library
 */
static void
end_library(void)
{
	free(curlib); curlib = NULL;
	free(curoutput); curoutput = NULL;
	free(curinput); curinput = NULL;
	cleanup(obj_syms); obj_syms = NULL; nobj_syms = 0;
	cleanup(db_syms); db_syms = NULL; ndb_syms = 0;
	cleanup(db_hide); db_hide = NULL; ndb_hide = 0;
}

/*
 * objfile()
 *	Process the -r object file
 */
static void
objfile(char *p)
{
	char tmpf[32], buf[128];
	FILE *fp;

	/*
	 * Use nm(1) and grep(1) to get all the text symbols.
	 */
	sprintf(tmpf, "/tmp/shl%d", getpid());
	sprintf(buf, "nm %s | grep ' T ' > %s", p, tmpf);
	do_system(buf);

	/*
	 * Consume the symbol names.  The format of each line is:
	 * <addr> T <name>.  We tally them into obj_syms.
	 */
	fp = fopen(tmpf, "r");
	if (fp == NULL) {
		perror(tmpf);
		exit(1);
	}
	while (fscanf(fp, "%*08lx T %s\n", buf) == 1) {
		obj_sym(buf);
	}
	fclose(fp);
	unlink(tmpf);
}

/*
 * add_entry()
 *	Tabulate another object expected within the library
 */
static void
add_entry(char *p, char *arg)
{
	if (curlib == NULL) {
		fprintf(stderr, "Error in %s; expected 'library' first\n",
			curfile);
		exit(1);
	}
	if (p && arg) {
		if (strcmp(arg, "hidden")) {
			fprintf(stderr, "Bad modifier to %s: %s\n",
				p, arg);
			exit(1);
		}
		db_hide = realloc(db_hide, ++ndb_hide * sizeof(char *));
		db_hide[ndb_hide-1] = strdup(p);
	} else {
		db_syms = realloc(db_syms, ++ndb_syms * sizeof(char *));
		db_syms[ndb_syms-1] = strdup(p);
	}
}

/*
 * check_lost()
 *	Try to find an entry in "array1" but not "array2"
 *
 * Prints out "complaint" if it does.  If "hide" is non-NULL, it
 * will be checked as well as "array2".
 */
static void
check_lost(char *complaint, char **array1, char **array2, char **hide)
{
	int x, found, banner = 0;

	for (x = 0; array1[x]; ++x) {
		int y;

		found = 0;
		for (y = 0; array2[y]; ++y) {
			if (!strcmp(array1[x], array2[y])) {
				found = 1;
				break;
			}
		}
		for (y = 0; hide && !found && hide[y]; ++y) {
			if (!strcmp(array1[x], hide[y])) {
				found = 1;
			}
		}
		if (!found) {
			if (!banner) {
				fprintf(stderr, "%s:\n", complaint);
				banner = 1;
			}
			fprintf(stderr, " %s\n", array1[x]);
		}
	}
}

/*
 * generate_stubs()
 *	Create the libc.a containing only stubs
 *
 * Actually, libc.a contains all the stubs, plus a module with the
 * minimal code needed to map in the real library.
 */
static void
generate_stubs(void)
{
	int x;
	FILE *fp;
	char *sym, buf[128];

	/*
	 * Clean up any old files
	 */
	do_system("rm -f /tmp/sho*.[cso]");

	/*
	 * Generate the loader stub
	 */
	fp = fopen("/tmp/sho.c", "w");
	if (fp == NULL) {
		perror("/tmp/sho.c");
		exit(1);
	}
	fprintf(fp,
"void *_addr_%s;\n"
"_load_%s() {\n"
"	extern void *_load();\n"
"	_addr_%s = _load(\"%s\");\n"
"	if (_addr_%s == 0) _notify(0, 0, \"kill\", 4); }\n",
	libname, libname, libname, curoutput, libname);
	fclose(fp);

	/*
	 * Generate a stub for each entry needed
	 */
	for (x = 0; sym = db_syms[x]; ++x) {
		sprintf(buf, "/tmp/sho%d.s", x);
		fp = fopen(buf, "w");
		if (fp == NULL) {
			perror(buf);
			exit(1);
		}
#ifdef i386
	/*
	 * Yes, this is pretty gross.  I should have an abstraction
	 * layer, with a nice, complicated way to communicate strings
	 * with arguments back and forth.
	 */
		fprintf(fp,
"	.globl	%s\n"
"%s:	movl	__addr_%s,%%eax\n"
"	testl	%%eax,%%eax\n"
"	je	1f\n"
"	addl	$%d,%%eax\n"
"	jmp	0(%%eax)\n"
"1:	call	__load_%s\n"
"	jmp	%s\n",
				sym, sym, libname, x * sizeof(ulong),
				libname, sym);
#endif
		fclose(fp);
	}

	/*
	 * Create a library with all stubs, as well as our library
	 * loader.
	 */
	do_system("cd /tmp; gcc -c sho*.[sc]");
	unlink(curlib);
	sprintf(buf, "ar -crs %s /tmp/sho*.o shlib.o syscalls.o", curlib);
	do_system(buf);

	/*
	 * Clean up
	 */
	do_system("rm -f /tmp/sho*.[cso]");
}

/*
 * generate_shlib()
 *	Take relocatable input image of library, generate mappable one
 *
 * Updates library base after generation so successive libraries
 * are allocated upwards in address.
 * We place data immediately following text.
 */
static void
generate_shlib(void)
{
	char buf[128], tmpf[32], tabf[32];
	FILE *fp;
	int x, cmdlen;
	ulong txtsize, datasize, bsssize;

	/*
	 * Build a jump table to prepend to the .shl output
	 */
	sprintf(tabf, "/tmp/sht%d.s", getpid());
	fp = fopen(tabf, "w");
	if (fp == NULL) {
		perror(tabf);
		exit(1);
	}
	fprintf(fp, "\t.text\n");
	fprintf(fp, "tbl_%s:\n", curlib);
	for (x = 0; db_syms[x]; ++x) {
		fprintf(fp, "\t.long\t%s\n", db_syms[x]);
	}
	fclose(fp);
	sprintf(buf, "cd /tmp; gcc -c %s", tabf);
	do_system(buf);
	unlink(tabf);

	/*
	 * Convert jump table filename to a .o
	 */
	tabf[strlen(tabf)-1] = 'o';

	/*
	 * Get shared library text size using nm(1)
	 */
	sprintf(tmpf, "/tmp/shll%d", getpid());
	sprintf(buf, "size %s > %s", curinput, tmpf);
	do_system(buf);
	fp = fopen(tmpf, "r");
	(void)fgets(buf, sizeof(buf), fp);
	if (fscanf(fp, "%ld", &txtsize) != 1) {
		fprintf(stderr, "Error: corrupt size(1) output in %s\n",
			tmpf);
		exit(1);
	}
	fclose(fp);
	unlink(tmpf);

	/*
	 * Generate the library at its assigned address
	 */
	txtsize += (sizeof(struct aout) + (sizeof(ulong) * (ndb_syms-1)));
	txtsize = roundup(txtsize, page_size);
	sprintf(buf, "ld -o %s -Ttext %lx -Tdata %lx %s %s",
		curoutput,
		lib_base + sizeof(struct aout),
		lib_base + txtsize,
		tabf, curinput);

	/*
	 * Run the command to get our object
	 */
	unlink(curoutput);
	do_system(buf);

	/*
	 * Dig up data size so we can bump lib_base forward
	 * the right amount.  We couldn't calculate this until
	 * now, because BSS/common has an undecided size until
	 * finally linked.
	 */
	sprintf(tmpf, "/tmp/xsh%d", getpid());
	sprintf(buf, "size %s > %s", curoutput, tmpf);
	do_system(buf);
	fp = fopen(tmpf, "r");
	if (fp == NULL) {
		perror(tmpf);
		exit(1);
	}

	/*
	 * Skip header line, get data and BSS size
	 */
	(void)fgets(buf, sizeof(buf), fp);
	if (fscanf(fp, "%ld\t%ld\t%ld", &txtsize, &datasize, &bsssize) != 3) {
		fprintf(stderr, "Error: corrupt size(1) output in %s\n",
			tmpf);
		exit(1);
	}
	fclose(fp);

	/*
	 * Advance library base.  Make sure everything still flies
	 * as a shared library--can't map partial pages!
	 */
	txtsize += sizeof(struct aout);
	lib_base += (txtsize + datasize + roundup(bsssize, page_size));
	if (lib_base & (page_size - 1)) {
		fprintf(stderr, "Error: lib_base no longer page aligned\n");
		fprintf(stderr, " txt 0x%lx data 0x%lx BSS 0x%lx base 0x%x\n",
			txtsize, datasize, bsssize, lib_base);
		exit(1);
	}
	unlink(tmpf);
	unlink(tabf);
}

int
main(int argc, char **argv)
{
	FILE *fp;
	char *keyword, *p, buf[128];
	int x;

	if (argc < 2) {
		fprintf(stderr,
			"Usage is: %s <database> [ <database>...]\n",
			argv[0]);
		exit(1);
	}

	page_size = getpagesize();
	for (x = 1; x < argc; ++x) {
		/*
		 * Access database file
		 */
		curfile = argv[x];
		fp = fopen(curfile, "r");
		if (fp == NULL) {
			perror(curfile);
			exit(1);
		}

		/*
		 * Process operations while there are lines in file
		 */
		while (fgets(buf, sizeof(buf), fp)) {
			/*
			 * Break out word at start of line from
			 * arguments.  Convert \n termination to \0.
			 */
			keyword = p = buf;
			p[strlen(p) - 1] = '\0';
			while (*p && (*p != '\n') && !isspace(*p)) {
				++p;
			}

			/*
			 * Null-terminate the starting word
			 */
			if (*p) {
				*p++ = '\0';
			}
			while (*p && isspace(*p))
				++p;

			/*
			 * Continue on comments
			 */
			if (!keyword[0] || (keyword[0] == '#')) {
				continue;
			}

			/*
			 * Decode keyword, call out to appropriate function
			 */
			if (!strcmp(keyword, "library")) {
				start_library(p);
			} else {
				add_entry(keyword, *p ? p : 0);
			}
		}
		fclose(fp);

		add_entry(NULL, NULL);	/* Sentinels */
		obj_sym(NULL);

		/*
		 * A couple sanity checks
		 */
		check_lost("Objects no longer in library",
			db_syms, obj_syms, NULL);
		check_lost("Entry points new to library",
			obj_syms, db_syms, db_hide);

		/*
		 * End of file.  Start building actual files.
		 */
		generate_stubs();
		generate_shlib();

		/*
		 * Done with this library
		 */
		end_library();
	}
	return(0);
}
