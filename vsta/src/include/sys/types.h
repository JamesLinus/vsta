#ifndef _TYPES_H
#define _TYPES_H
/*
 * types.h
 *	Central repository for globally known types
 */
typedef unsigned int uint;
typedef unsigned int uint_t;
typedef unsigned int u_int;
typedef unsigned short ushort;
typedef unsigned short ushort_t;
typedef unsigned short u_short;
typedef unsigned char uchar;
typedef unsigned char uchar_t;
typedef unsigned char u_char;
typedef unsigned long ulong;
typedef unsigned long ulong_t;
typedef unsigned long u_long;
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
