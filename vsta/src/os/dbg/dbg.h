#ifndef _NAMES_H
#define _NAMES_H
/*
 * names.h
 *	Values shared between kernel and utiliies
 */
#include <sys/types.h>

#define DBG_NAMESZ (20*1024)	/* Buffer for namelist data */

/*
 * Type of each entry in dbg_names[]
 */
#define DBG_END (1)
#define DBG_TEXT (2)
#define DBG_DATA (3)

/*
 * Structure superimposed onto the stream of bytes in dbg_names[]
 */
struct sym {
	uchar s_type;		/* Must be first--see dbg_names[] */
	ulong s_val;
	char s_name[1];
};

/*
 * How to advance to end of current struct sym
 */
#define NEXTSYM(s) ((struct sym *)((s)->s_name + strlen((s)->s_name) + 1))

#endif /* _NAMES_H */
