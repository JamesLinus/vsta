#ifndef _TYPES_H
#define _TYPES_H
/*
 * types.h
 *	Central repository for globally known types
 */
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned long off_t;
struct time {
	ulong t_sec;
	ulong t_usec;
};
typedef int port_t;
typedef int port_name;
typedef void (*voidfun)();
typedef int (*intfun)();

#endif /* _TYPES_H */
