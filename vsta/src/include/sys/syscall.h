#ifndef _SYSCALL_H
#define _SYSCALL_H
/*
 * syscall.h
 *	Values/prototypes for system calls
 *
 * Not all of the system calls have prototypes defined here.  The ones that
 * are missing from here can be found in other (more appropriate) .h files
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
#define S_TIME_SET 33
#define S_PTRACE 34
#define S_MSG_PORTNAME 35
#define S_HIGH S_MSG_PORTNAME

/*
 * Some syscall prototypes
 */
#ifndef __ASM__
#include <sys/types.h>

extern int enable_io(int arg_low, int arg_high);
extern int enable_isr(port_t arg_port, int irq);
extern int clone(port_t arg_port);
extern int page_wire(void *arg_va, void **arg_pa);
extern int page_release(uint arg_handle);
extern int enable_dma(int);
extern int time_get(struct time *arg_time);
extern int time_sleep(struct time *arg_time);
extern void dbg_enter(void);
extern int set_swapdev(port_t arg_port);
extern void run_qio(void);
extern int set_cmd(char *arg_cmd);
extern int pageout(void);
extern int unhash(port_t arg_port, long arg_fid);
extern int time_set(struct time *arg_time);
extern int ptrace(pid_t pid, port_name name);

#endif /* __ASM__ */

#endif /* _SYSCALL_H */
