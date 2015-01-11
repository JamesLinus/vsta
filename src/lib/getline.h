#ifndef GETLINE_H
#define GETLINE_H

extern char *getline();		/* read a line of input */
extern void gl_setwidth();	/* specify width of screen */
extern void gl_histadd();	/* adds entries to hist */
extern void gl_strwidth();	/* to bind gl_strlen */

extern int (*gl_in_hook)();
extern int (*gl_out_hook)();
extern int (*gl_tab_hook)();

#endif /* GETLINE_H */
