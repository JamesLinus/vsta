#ifndef _PORTS_H
#define _PORTS_H
/*
 * ports.h
 *	Various global constants for port numbers
 */
#include <sys/types.h>

#define PORT_NAMER (port_name)1		/* Name to port # mapper */
#define PORT_TIMER (port_name)2		/* Central time service */
#define PORT_SWAP (port_name)3		/* Swap manager */
#define PORT_CONS (port_name)4		/* Console output */
#define PORT_KBD (port_name)5		/* Console keyboard */
#define PORT_ENV (port_name)6		/* Environment server */

#endif /* _PORTS_H */
