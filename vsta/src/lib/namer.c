/*
 * namer.c
 *	Stubs for talking to namer
 *
 * The namer routines are not syscalls nor even central system
 * routines.  They merely encapsulate a message-based conversation
 * with a namer daemon.  So this file should grow to be some code
 * for connecting to a well-known port number, and talking with
 * a certain format of messages.
 */
#include <sys/msg.h>

port_name namer_find(char *class, int flags) { }
int namer_register(char *class, port_t port) { }
