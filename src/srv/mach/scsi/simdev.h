/*
 * simdev.h - SIM device specific definitions.
 */

#ifndef	__SIMDEV_H__
#define	__SIMDEV_H__
/*
 * Adaptor parameters. The exact meaning of these fields is driver specific.
 */
struct	sim_hba_params {
	void	*ioaddr;			/* base I/O address */
	int	irq;				/* interrupt level */
	int	ivect;				/* interrupt vector */
	int	dmarq;				/* DMA level */
};

/*
 * Per driver initialization structure.
 */
struct	sim_driver {
	void	(*dvrinit)();			/* initialization function */
	int	max_adaptors;			/* MAX adaptors, 0=autosize */
	struct	sim_hba_params *hba_params;	/* HW parameter overrides */
};

extern	struct sim_driver sim_dvrtbl[];
extern	int sim_ndrivers;

#endif	/*__SIMDEV_H__*/

