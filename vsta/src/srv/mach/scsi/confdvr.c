/*
 * confdvr.c - handles statically linked driver configuration.
 */
#include "sim.h"

/*
 * ccfdv_init - the configuration driver initialization function.
 * Call statically linked driver initialization functions.
 */
void	ccfdv_init(void)
{
	struct	sim_driver *sdp;
	int	i;

	for(sdp = sim_dvrtbl, i = 0; i < sim_ndrivers; i++, sdp++)
		(*sdp->dvrinit)(sdp);
}

