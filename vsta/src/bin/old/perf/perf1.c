/*
 * swtst.c - test context switch, messaging time.
 *
 * This program is loosely based on a program called ctxsig.c posted to
 * comp.arch by Larry McVoy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <std.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/namer.h>

#define	MSGCNT	50000

static	port_t swtst_port;		/* swtst communicates thru this */
static	port_name swtst_port_name;	/*  ...its name */

/*
 * delta_timer - calculate the difference between the current time
 * and the value in 'timer'. Put the result in 'timer'.
 */
void	delta_timer(timer)
struct	time *timer;
{
	struct time current;

	time_get(&current);
	if(current.t_usec < timer->t_usec) {
		current.t_usec += 1000000;
		current.t_sec--;
	}
	timer->t_sec = current.t_sec - timer->t_sec;
	timer->t_usec = current.t_usec - timer->t_usec;
}

/*
 * print_timer - print the string corresponding to 'timer' into 'buffer'.
 */
char	*print_timer(timer, buffer)
struct	time *timer;
char	*buffer;
{
	sprintf(buffer, "%lu.%02lu", timer->t_sec, timer->t_usec / 10000);
	return(buffer);
}

/*
 * print_quotient - print the string corresponding to d1 / d2.
 */
char	*print_quotient(unsigned long d1, unsigned long d2, char *buffer)
{
	sprintf(buffer, "%lu.%02lu", d1 / d2, ((d1 % d2) * 100) / d2);
	return(buffer);
}

void	child_thread()
{
	port_name pn;
	port_t p;
	struct	msg msg;
	int	i, calls;
	long	unsigned ms;
	struct time timer;
	static	char time_str[32], swpms_str[32], uspsw_str[32];

	pn = namer_find("swtst");
	if (pn < 0) {
		perror("namer_find");
		exit(1);
	}
	if((p = msg_connect(pn, ACC_READ)) < 0) {
		perror("msg_connect");
		exit(1);
	}

	msg.m_op = 10000;
	msg.m_buf = NULL;
	msg.m_buflen = 0;
	msg.m_nseg = 0;
	msg.m_arg = 0;
	msg.m_arg1 = 0;

	time_get(&timer);
	
	for(i = 0; i < MSGCNT; i++) {
		if (msg_send(p, &msg) < 0) {
			perror("msg_send");
			exit(0);
		}
	}
/*
 * Print the results
 */
	delta_timer(&timer);
	ms = 1000 * timer.t_sec + timer.t_usec / 1000;
	calls = MSGCNT * 2;
	print_timer(&timer, time_str);
	print_quotient(calls, ms, swpms_str);
	print_quotient(ms * 1000, calls, uspsw_str);
	printf("%d context switches in %s secs, %s/millisec %s microsec/switch\n",
	       calls, time_str, swpms_str, uspsw_str);

/*	
	printf("%.0f context switches in %.2f secs, %.2f/millisec %.0f microsec/switch\n",
	    (double)calls, ms / 1000.0, (double)calls/ms, (ms * 1000) / calls);
 */	    

/*
	delta_timer(&timer);
	print_timer(&timer, time_str);
	printf("Elapsed time = %s\n", time_str);
 */
	exit(0);
}

void	usage()
{
	fprintf(stderr, "Usage: swtst [-t]\n");
	fprintf(stderr,  "\t-t\t\t- use tfork() instead of fork()\n");
	exit(1);
}

void	main(int argc, char **argv)
{
	char	**av;
	int	ac, pid, count = 0, tflag = 0;
	struct	msg msg;
/*
 * Parse command line arguments.
 */
 	for(ac = 1, av = &argv[1]; ac < argc; ac++) {
 		if(strcmp(*av, "-t") == 0) {
 			tflag++;
 		} else
 			usage();
 	}
/*
 * Register a port for paret/child messaging.
 */ 
	swtst_port = msg_port((port_name)0, &swtst_port_name);
	if (namer_register("swtst", swtst_port_name) < 0) {
		perror("namer_register");
		exit(1);
	}
/*
 * Fork a child process or thread.
 */
	if(tflag) {
		tfork(child_thread);
	} else {
		if((pid = fork()) < 0) {
			perror("fork");
			exit(1);
		}
		if(pid == 0)
			child_thread();
	}

	for(;;) {
		if(msg_receive(swtst_port, &msg) < 0) {
			perror("message receive error");
			exit(0);
		}
		switch(msg.m_op) {
		case M_CONNECT:
			msg_accept(msg.m_sender);
			break;
		case 10000:
			msg.m_arg = msg.m_buflen = 0;
			msg.m_nseg = msg.m_arg1 = 0;
			msg_reply(msg.m_sender, &msg);
			if(++count >= MSGCNT) {
				exit(0);
			}
			break;
		default:
			printf("Unknown message op %d\n", msg.m_op);
			exit(0);
		}
	}

	exit(0);
}
