/*
 * qio.c
 *	Routines for providing a asynch queued-I/O service
 */
#include <sys/param.h>
#include <sys/qio.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/msg.h>
#include <sys/seg.h>
#include <sys/vm.h>
#include <sys/fs.h>
#include <sys/port.h>
#include <lib/alloc.h>

static sema_t qio_sema;		/* Counting semaphore for # elements */
static struct qio *qios = 0;	/* Free elements */
static sema_t qio_gate;		/* For waking a process to run the qio */
static struct qio		/* Elements waiting to be run */
	*qio_hd, *qio_tl;
static lock_t qio_lock;		/* Spinlock for run list */

/*
 * qio()
 *	Queue an I/O
 */
void
qio(struct qio *q)
{
	p_lock(&qio_lock, SPL0);
	q->q_next = 0;
	if (qio_hd == 0) {
		qio_hd = q;
	} else {
		qio_tl->q_next = q;
	}
	qio_tl = q;
	v_lock(&qio_lock, SPL0);
	v_sema(&qio_gate);
}

/*
 * qio_msg_send()
 *	Do a msg_send()'ish operation based on a QIO structure
 */
static void
qio_msg_send(struct qio *q)
{
	struct seg *s = 0;
	struct sysmsg *sm = 0;
	struct portref *pr = q->q_port;
	int error = 0;
	extern struct seg *kern_mem();

	/*
	 * Get our temp buffers
	 */
	ASSERT_DEBUG(q->q_cnt == NBPG, "qio: not a page");
	s = kern_mem(ptov(ptob(q->q_pp->pp_pfn)), NBPG);
	sm = malloc(sizeof(struct sysmsg));

	/*
	 * Become sole I/O through port
	 */
	p_sema(&pr->p_sema, PRIHI);
	p_lock(&pr->p_lock, SPL0);

	/*
	 * We lose if the server for the port has left
	 */
	if (!pr->p_port) {
		error = 1;
		goto out;
	}

	/*
	 * Send a seek+r/w
	 */
	sm->m_op = q->q_op;
	sm->m_arg = NBPG;
	sm->m_arg1 = q->q_off;
	sm->m_nseg = 1;
	sm->m_seg[0] = s;
	sm->m_sender = pr;
	pr->p_msg = sm;
	pr->p_state = PS_IOWAIT;
	set_sema(&pr->p_iowait, 0);
	queue_msg(pr->p_port, sm);
	p_sema_v_lock(&pr->p_iowait, PRIHI, &pr->p_lock);
	p_lock(&pr->p_lock, SPL0);
	if ((pr->p_port == 0) || (sm->m_arg < 0)) {
		error = 1;
		goto out;
	}

	/*
	 * We only support raw/DMA devices for swap, and rea-only
	 * for mapped files.
	 */
	ASSERT(sm->m_nseg == 0, "qio_msg_send: got back seg");

	/*
	 * Clean up and handle any iodone portion
	 */
out:
	v_lock(&pr->p_lock, SPL0);
	v_sema(&pr->p_svwait);
	v_sema(&pr->p_sema);
	if (sm) {
		free(sm);
	}

	/*
	 * If q_iodone isn't null, put result in q_cnt and call
	 * the function.
	 */
	if (q->q_iodone) {
		q->q_cnt = error;
		(*(q->q_iodone))(q);
	}

	/*
	 * Release qio element
	 */
	free_qio(q);
}

/*
 * run_qio()
 *	Syscall which the dedicated QIO threads remain within
 */
run_qio(void)
{
	struct qio *q;

	if (!isroot()) {
		return(-1);
	}

	/*
	 * Get a new qio structure, add to the pool
	 */
	q = malloc(sizeof(struct qio));
	p_lock(&qio_lock, SPL0);
	q->q_next = qios;
	qios = q;
	v_lock(&qio_lock, SPL0);

	/*
	 * Bump the count allowed through the qio semaphore
	 */
	v_sema(&qio_sema);

	/*
	 * Endless loop
	 */
	for (;;) {
		/*
		 * Wait for work
		 */
		p_sema(&qio_gate, PRIHI);

		/*
		 * Extract one unit of work
		 */
		p_lock(&qio_lock, SPL0);
		q = qio_hd;
		ASSERT_DEBUG(q, "run_qio: gate mismatch with run");
		qio_hd = q->q_next;
#ifdef DEBUG
		if (qio_hd == 0) {
			qio_tl = 0;
		}
#endif
		v_lock(&qio_lock, SPL0);

		/*
		 * Do the work
		 */
		qio_msg_send(q);
	}
}

/*
 * alloc_qio()
 *	Allocate a queued I/O structure
 *
 * We throttle the number of queued operations with this routine.  It
 * may sleep, so it may not be called with locks held.
 */
struct qio *
alloc_qio(void)
{
	struct qio *q;

	p_sema(&qio_sema, PRIHI);
	p_lock(&qio_lock, SPL0);
	q = qios;
	ASSERT_DEBUG(q, "qio_alloc: bad sema count");
	qios = q->q_next;
	v_lock(&qio_lock, SPL0);
	return(q);
}

/*
 * free_qio()
 *	Free qio element back to pool when finished
 */
static void
free_qio(struct qio *q)
{
	p_lock(&qio_lock, SPL0);
	q->q_next = qios;
	qios = q;
	v_lock(&qio_lock, SPL0);
	v_sema(&qio_sema);
}

/*
 * init_qio()
 *	Called once to initialize the queued I/O system
 */
void
init_qio(void)
{
	init_sema(&qio_sema); set_sema(&qio_sema, 0);
	init_sema(&qio_gate); set_sema(&qio_gate, 0);
	qios = qio_hd = qio_tl = 0;
	init_lock(&qio_lock);
}
