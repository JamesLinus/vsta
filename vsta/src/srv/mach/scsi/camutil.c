/*
 * camutil.c - non-server specific CAM utility functions.
 */
#include <stdio.h>
#include <std.h>
#include "cam.h"

/*
 * cam_get_sglen()
 *	Return the sum of the scatter/gather element transfer lengths.
 */
long	unsigned cam_get_sglen(sg_list, sg_count)
CAM_SG_ELEM *sg_list;
int	sg_count;
{
	long	unsigned xfer_len;
	int	i;

	for(i = 0, xfer_len = 0L; i < sg_count; i++)
		xfer_len += sg_list[i].sg_length;
	return(xfer_len);
}

/*
 * cam_si32tohi32()
 *	SCSI CDB integer to host 32-bit integer conversion.
 */
void	cam_si32tohi32(register unsigned char *si, uint32 *hi)
{
	*hi = (si[0] << 24) | (si[1] << 16) | (si[2] << 8) | si[3];
}

/*
 * cam_si24tohi32()
 *	SCSI CDB 3 byte integer to host 32-bit integer conversion.
 */
void	cam_si24tohi32(unsigned char *si, uint32 *hi)
{
	*hi = (si[0] << 16) | (si[1] << 8) | si[2];
}

/*
 * cam_hi32tosi24
 *	Host 32-bit integer to SCSI CDB 3 byte integer conversion.
 */
void	cam_hi32tosi24(uint32 hi, unsigned char *si)
{
	si[0] = (hi >> 16) & 0xff;
	si[1] = (hi >> 8) & 0xff;
	si[2] = hi & 0xff;
}

