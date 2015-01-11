#ifndef __PAR_PORT_H
#define __PAR_PORT_H

/*
 * par_port.h
 *	Structures, data types and prototypes for low level code for
 *	PC parallel port.
 */

struct par_port {
/* private: */
	int data;		/* I/O addresses */
	int status;
	int control;

	int mask;		/* currently used control word */

/* public: */
	int quiet;		/* wether to log messages to screen */
	char *last_error;	/* points to appropriate error message */
};

typedef enum {
	P_OK,			/* device can accept data */
	P_TIMEOUT,		/* try again later */
	P_ERROR			/* error condition, par_port points to
				   appropriate error msg */
} parallel_status;

extern int par_init(struct par_port *self, int port_no);
extern void par_reset(struct par_port *self);
extern parallel_status par_isready(struct par_port *self);
extern void par_putc(struct par_port *self, char c);

#endif
