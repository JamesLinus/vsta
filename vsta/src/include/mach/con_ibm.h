/*
 * con_ibm.h : IBM and clone specific header for the console driver.
 * 
 * Written by G.T.Nicol with bits borrowed from the original
 */

#ifndef __VSTA_CON_IBM_H__
#define __VSTA_CON_IBM_H__

#include <sys/types.h>
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>

#define CONS_MAX_PARAM   128
#define CONS_TAB_SIZE     8	/* Tab stops every 8 positions */

/*
 * Colors for the IBM et al.
 */
#define CONS_BLACK              0
#define CONS_BLUE               1
#define CONS_GREEN              2
#define CONS_CYAN               3
#define CONS_RED                4
#define CONS_MAGENTA            5
#define CONS_YELLOW             6
#define CONS_WHITE              7
#define CONS_BLINK             (1 << 8)
#define CONS_BOLD              (1 << 8)
#define CONS_FOREGROUND_MASK   0x0F
#define CONS_BACKGROUND_MASK   0xF0
#define CONS_SET_FORE(x,y) (((x) & ~CONS_FOREGROUND_MASK) | (y))
#define CONS_SET_BACK(x,y) (((x) & ~CONS_BACKGROUND_MASK) | ((y)<<4))

/*
 * These are used by con_move() to decide how to move the cursor
 */
#define CONS_MOVE_UP            1
#define CONS_MOVE_DOWN          2
#define CONS_MOVE_LEFT          3
#define CONS_MOVE_RIGHT         4

/*
 * These are used by con_erase() to decide where to clear to
 */
#define CONS_ERASE_EOS          1
#define CONS_ERASE_EOL          2
#define CONS_ERASE_SOS          3
#define CONS_ERASE_SOL          4
#define CONS_ERASE_SCREEN       5
#define CONS_ERASE_LINE         6

/*
 * These are used in con_insert() and con_delete()
 */
#define CONS_LINE_MODE          7
#define CONS_CHAR_MODE          8

/*
 * These are almost meaningless, but in the NEC version we have more states
 * for kanji mode etc.
 */
#define CONS_MODE_NORMAL         0
#define CONS_VTGRAPH       (1 << 1)	/* use the VT52 graphics set  */
#define CONS_VT52_PRIORITY (1 << 2)	/* interpret VT52 codes first     */

/*
 * These are the states of the escape sequence parser in con_putchar()
 */
#define STATE_NORMAL                0x00	/* Normal state             */
#define STATE_ESCAPE                0x01	/* ESC seen state           */
#define STATE_ESCAPE_LPAREN         0x02	/* ESC ( seen state         */
#define STATE_ESCAPE_RPAREN         0x03	/* ESC ) seen state         */
#define STATE_ESCAPE_BRACKET        0x04	/* ESC [ seen state         */
#define STATE_ESCAPE_SHARP          0x05	/* ESC # seen state         */
#define STATE_FUNCTION_KEY          0x06	/* ESC [[ seen state        */
#define STATE_GET_PARAMETERS        0x07	/* ESC [ seen. Read params  */
#define STATE_GOT_PARAMETERS        0x08	/* ESC [ seen. Got params   */

/*
 * These are the responses. Currently they are unused....
 */
#define RESPOND_VT52_ID     "\033/Z"
#define RESPOND_VT102_ID    "\033[?6c"
#define RESPOND_STATUS_OK   "\033[0n"

#define min(a,b) ((a) < (b) ? a : b)
#define max(a,b) ((a) > (b) ? a : b)

/*
 * An open file
 */
struct file {
	uint            f_gen;	/* Generation of access */
	uint            f_flags;/* User access bits */
};

/* main.c */
extern struct prot con_prot;

/* con_nec.c */
extern uint     con_max_rows;
extern uint     con_max_cols;
extern void     con_initialise(int argc, char **argv);
extern void     con_write_string(char *string, int count);

/* stat.c */
extern void     con_stat(struct msg * m, struct file * f);
extern void     con_wstat(struct msg * m, struct file * f);

#endif				/* __VSTA_CON_NEC_H__ */
