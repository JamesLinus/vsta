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
typedef unsigned long size_t;
struct time {
	ulong t_sec;
	ulong t_usec;
};
typedef int port_t;
typedef int port_name;
typedef long pid_t;
typedef void (*voidfun)();
typedef int (*intfun)();

/*
 * For talking about a structure without knowing its definition
 */
#define STRUCT_REF(type) typedef struct type __##type##_;

#endif /* _TYPES_H */
