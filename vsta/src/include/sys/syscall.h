#ifndef _SYSCALL_H
#define _SYSCALL_H
/*
 * syscall.h
 *	Values for system calls
 */
#define S_MSG_PORT 0
#define S_MSG_CONNECT 1
#define S_MSG_ACCEPT 2
#define S_MSG_SEND 3
#define S_MSG_RECEIVE 4
#define S_MSG_REPLY 5
#define S_MSG_DISCONNECT 6
#define S_MSG_ERR 7
#define S_EXIT 8
#define S_FORK 9
#define S_THREAD 10
#define S_ENABIO 11
#define S_ISR 12
#define S_MMAP 13
#define S_MUNMAP 14
#define S_STRERROR 15
#define S_NOTIFY 16
#define S_CLONE 17
#define S_PAGE_WIRE 18
#define S_PAGE_RELEASE 19
#define S_ENABLE_DMA 20
#define S_TIME_GET 21
#define S_TIME_SLEEP 22
#define S_DBG_ENTER 23
#define S_EXEC 24
#define S_WAITS 25
#define S_PERM_CTL 26
#define S_SET_SWAPDEV 27
#define S_RUN_QIO 28
#define S_SET_CMD 29
#define S_PAGEOUT 30
#define S_GETID 31
#define S_UNHASH 32
#define S_HIGH S_UNHASH

#endif /* _SYSCALL_H */
