/*
 * syscalls.c
 *	Actual entry points into the VSTa operating system
 */
#include <sys/syscall.h>

/*
 * syserr()
 *	Minimal syscalls needed to bring in a shared library
 */
#define ENTRY(n, v)	.globl	_##n##_shl ; \
	_##n##_shl: movl $(v),%eax ; int $0xFF ; ret

ENTRY(msg_port, S_MSG_PORT)
ENTRY(msg_connect, S_MSG_CONNECT)
ENTRY(msg_send, S_MSG_SEND)
ENTRY(msg_disconnect, S_MSG_DISCONNECT)
ENTRY(_mmap, S_MMAP)
ENTRY(munmap, S_MUNMAP)
ENTRY(_notify, S_NOTIFY)
