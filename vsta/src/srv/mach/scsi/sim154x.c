/*
 * sim154x.c - Adaptec 1542C SIM driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <mach/param.h>
#include "cam.h"
#include "scsi.h"

/*
 * I/O port definitions.
 */
/*
 * Port address base + 0 write definitions.
 */
#define	SIM154X_HRST		(1 << 7)	/* hard reset */
#define	SIM154X_SRST		(1 << 6)	/* soft reset */
#define	SIM154X_IRST		(1 << 5)	/* interrupt reset */
#define	SIM154X_SCRST		(1 << 4)	/* SCSI bus reset */
/*
 * Port address base + 0 read definitions.
 */
#define	SIM154X_STST		(1 << 7)	/* self test in progress */
#define	SIM154X_DIAGF		(1 << 6)	/* internal diag. failure */
#define	SIM154X_MBIR		(1 << 5)	/* mailbox init. required */
#define	SIM154X_IDLE		(1 << 4)	/* SCSI host adaptor idle */
#define	SIM154X_CDF		(1 << 3)	/* cmd./data out port full */
#define	SIM154X_DF		(1 << 2)	/* data in port full */
#define	SIM154X_INVDCMD		(1 << 0)	/* invalid host adaptor cmd. */
/*
 * Port address base + 2 write definitions.
 */
#define	SIM154X_ANYINTR		(1 << 7)	/* any interrupt */
#define	SIM154X_SCRD		(1 << 3)	/* SCSI reset detected */
#define	SIM154X_HACC		(1 << 2)	/* host adaptor cmd. cmplt. */
#define	SIM154X_MBOA		(1 << 1)	/* MBO available */
#define	SIM154X_MBIF		(1 << 0)	/* MBI full */

/*
 * I/O ports.
 */
struct	sim154x_ports {
	uint8	csr;				/* control/status */
	uint8	cdr;				/* command/data */
	uint8	intr;				/* interrupt flags */
	uint8	id;				/* identification register */
};

/*
 * Register offsets.
 */
#define	SIM154X_CSR_OFF		0
#define	SIM154X_CDR_OFF		1
#define	SIM154X_INTR_OFF	2
#define	SIM154X_ID_OFF		3		/* 1542c only */

/*
 * Adaptor command operation codes.
 */
#define	SIM154X_NOP		0		/* no operation */
#define	SIM154X_MBXINIT		1		/* mailbox initialization */
#define	SIM154X_SCSI_START	2		/* start SCSI command */
#define	SIM154X_BIOS_START	3		/* start BIOS command */
#define	SIM154X_ADAPTOR_INQ	4		/* adaptor inquiry */
#define	SIM154X_ENBL_MBOA	5		/* enable MBOA interrupt */
#define	SIM154X_SELECT_TMO	6		/* set selection timeout */
#define	SIM154X_BUS_ON_TIME	7		/* set bus on time */
#define	SIM154X_BUS_OFF_TIME	8		/* set bus off time */
#define	SIM154X_XFER_SPEED	9		/* set transfer speed */
#define	SIM154X_INST_DEVS	0xa		/* get installed devices */
#define	SIM154X_CONF_DATA	0xb		/* get configuration data */
#define	SIM154X_TGT_MODE	0xc		/* enable target mode */
#define	SIM154X_SETUP_DATA	0xd		/* get setup data */
#define	SIM154X_WC2_FIFO	0x1a		/* write channel 2 FIFO */
#define	SIM154X_RC2_FIFO	0x1b		/* read channel 2 FIFO */
#define	SIM154X_WC1_FIFO	0x1c		/* write channel 1 FIFO */
#define	SIM154X_RC1_FIFO	0x1d		/* read channel 1 FIFO */
#define	SIM154X_ECHO_CMD	0x1f		/* echo command data */
#define	SIM154X_ADAPTOR_DIAG	0x20		/* adaptor diagnostic */
#define	SIM154X_HA_OPT		0x21		/* set host adaptor options */
#define	SIM154X_SET_EEPROM	0x22		/* set EEPROM */
#define	SIM154X_GET_EEPROM	0x23		/* get EEPROM */
#define	SIM154X_ENBL_SHRAM	0x24		/* enable shadow RAM for r/w */
#define	SIM154X_INIT_BIOS_MB	0x25		/* initialize BIOS mailbox */
#define	SIM154X_BIOS_BANK1	0x26		/* set BIOS bank 1 */
#define	SIM154X_BIOS_BANK2	0x27		/* set BIOS bank 2 */
#define	SIM154X_EXT_BIOS	0x28		/* get extended BIOS info. */
#define	SIM154X_ENBL_MBIF	0x29		/* set mailbox if. enable */
#define	SIM154X_BIOS_SCSI_ST	0x82		/* start BIOS SCSI command */

/*
 * Adaptec Command Control Block operation codes and flags.
 */
#define	SIM154X_INIR_ACCB	0x00		/* initiator ACCB */
#define	SIM154X_TARG_ACCB	0x01		/* target ACCB */
#define	SIM154X_INIR_SG_ACCB	0x02		/* initiator S/G ACCB */
#define	SIM154X_INIR_RDL_ACCB	0x03		/* initiator RDLR ACCB */
#define	SIM154X_INIR_BOTH_ACCB	0x04		/* initiator S/G + RDLR ACCB */
#define	SIM154X_RETRY_ACCB	0x20		/* Retry flag */
#define	SIM154X_ND_ACCB		0x40		/* No Disconnect flag */
#define	SIM154X_BDR_ACCB	0x80		/* Bus Device Reset flag */

/*
 * Host adaptor status codes.
 */
#define	SIM154X_REQ_CMP		0x00		/* request complete w/o error */
#define	SIM154X_SEL_TIMEOUT	0x11		/* selection timeout */
#define SIM154X_DATA_OVUN_RUN	0x12		/* Data over/under-run error */
#define	SIM154X_UNEXP_BUSFREE	0x13		/* unexpected bus free */
#define	SIM154X_TARG_SEQ_FAIL	0x14		/* target phase seq. error */
#define	SIM154X_INVAL_ACCB_OP	0x16		/* invalid ACCB opcode */
#define	SIM154X_INVAL_LINK	0x17		/* linked ACCB LUN mismatch */
#define	SIM154X_INVAL_TGT_DIR	0x18		/* invalid target direction */
#define	SIM154X_DUP_ACCB	0x19		/* dup. target mode ACCB */
#define	SIM154X_INVAL_ACCB_PRM	0x1a		/* invalid ACCB parameter */

/*
 * Mailbox out commands.
 */
#define	SIM154X_MBO_FREE	0x00		/* mailbox out is free */
#define	SIM154X_MBO_START	0x01		/* start command */
#define	SIM154X_MBO_ABORT	0x02		/* abort command */

/*
 * ACCB direction bits.
 */
#define	SIM154X_DIR_OUT		0x10		/* outbound transfer */
#define	SIM154X_DIR_IN		0x08		/* inbound transfer */

#define	SIM154X_ACCB_ADC(_targ, _lun, _dir)				\
		(((_targ) << 5) | (_lun) | (_dir))
#define	SIM154X_ACCB_TARG(_accb)	(((_accb)->addr_dir >> 5) & 7)

/*
 * Mailbox initialization structure.
 */
struct	sim154x_mbxinit {
	uint8	mbx_count;			/* mailbox count */
	uint8	mbx_addr[3];			/* mailbox address */
};

/*
 * Get configuration data structure.
 */
struct	sim154x_config {
	uint8	dma_chan;			/* DMA arbitration priority */
	uint8	intr_chan;			/* interrupt channel */
	uint8	scsi_id;			/* host SCSI ID */
};

/*
 * 24-bit big endian to/from integer conversion.
 */
#define	SIM154X_GET_INT24(_ptr)					\
			(((_ptr)[0] << 16) | ((_ptr)[1] << 8) |		\
			  (_ptr)[2])

#define	SIM154X_SET_INT24(_ptr, _data) do {				\
			_ptr[0] = (_data) >> 16;			\
			_ptr[1] = (_data) >> 8;				\
			_ptr[2] = (_data);				\
		} while(0)

/*
 * Adaptec Command Control Block (ACCB) format.
 */
#define	SIM154X_ACCB_DATA_SIZE	 (sizeof(union scsi_cdb) + 14)

struct	sim154x_accb {
	uint8	opcode;				/* ACCB operation code */
	uint8	addr_dir;			/* address/direction cntl. */
	uint8	scsi_cmdlen;			/* SCSI command length */
	uint8	rqsns_len;			/* request sense length */
	uint8	data_len[3];			/* data length */
	uint8	data_ptr[3];			/* data pointer */
	uint8	link_ptr[3];			/* link pointer */
	uint8	cmd_link_id;			/* command linking ID */
	uint8	hastat;				/* host adaptor status */
	uint8	tarstat;			/* target device status */
	uint8	reserved[2];
	uint8	data[SIM154X_ACCB_DATA_SIZE];	/* cdb + sense data */
};

static	unsigned short sim154x_addrs[] = {
	0x330, 0x334, 0x230, 0x234, 0x130, 0x134
};
#define	SIM154X_NADDRS	(sizeof(sim154x_addrs) / sizeof(sim154x_addrs[0]))

#define	SIM154X_LOW		0x130		/* lowest port address */
#define	SIM154X_HIGH		0x334		/* highest port address */
#define	SIM154X_NMBX		4		/* number of mailbox's */
#define	SIM154X_NSGPACCB	32		/* no. of S/G per ACCB */
#define	SIM154X_MAX_ADAPTORS	(NBPG / sizeof(struct sim154x_adaptor))

#define	SIM154X_CANT_DMA(adaptor, pa)	((((long)pa) & ~0xffffff) != 0)
/* Test
#define	SIM154X_CANT_DMA(adaptor, pa)	1
 */

/*
 * Mailbox structure.
 */
struct	sim154x_mbx {
	uint8	cmdsts;				/* mailbox command/status */
	uint8	addr[3];			/* accb physical addr. */
};

/*
 * Host adaptor scatter/gather element structure.
 */
struct	sim154x_sg_elem {
	uint8	length[3];
	uint8	address[3];
};

/*
 * Bounce buffer structure.
 */
struct	sim154x_bounce_info {
	void	*uva;				/* user virtual address */
	void	*bva;				/* bounce virtual address */
	int	len;				/* buffer size */
};

/*
 * Per host adaptor structure.
 */
struct	sim154x_adaptor {
	struct	sim154x_mbx mbos[SIM154X_NMBX];	/* mailbox out area */
	struct	sim154x_mbx mbis[SIM154X_NMBX];	/* mailbox in area */
/*
 * Adaptec ACCB and scatter/gather areas.
 */
	struct	sim154x_accb accbs[SIM154X_NMBX];
	struct	sim154x_sg_elem sg_list[SIM154X_NMBX][SIM154X_NSGPACCB];
/**
 ** The following elements don't need to be wired.
 **/
/*
 * Scatter/gather address mapping, bounce buffer, and count information.
 */
	struct	sim154x_sg_info {
		int	unsigned addr_map[SIM154X_NSGPACCB];
		struct	sim154x_bounce_info bounce_info[SIM154X_NSGPACCB];
		int	sg_count;
	} sg_info[SIM154X_NMBX];
	struct	sim_bus bus_info;		/* generic bus information */
	short	unsigned ioport;		/* I/O port address */
	char	unsigned intr_level;		/* interrupt level */
	char	unsigned dma_level;		/* DMA level */
	char	unsigned brdid;			/* board ID */
	char	unsigned next_mbo;		/* next MBO index */
};

/*
 * Adaptor Inquiry structure.
 */
struct	sim154x_adaptor_inq {			/* adaptor inquiry */
	uint8	brdid;				/* board ID */
	uint8	special;			/* special options */
	uint8	fwrev[2];			/* firmware revision  */
};

/*
 * Board ID's.
 */
#define	SIM154X_ID_154XC	0x44
#define	SIM154X_ID_154XCF	0x45

/*
 * Return Extended BIOS Information structure.
 */
struct	sim154x_ext_bios {			/* extended BIOS info. */
	uint8	enabled;			/* bit 3 = xlation enabled */
	uint8	lock_code;			/* MBX lock code */
};

/*
 * Set Mailbox Interface Enable structure.
 */
struct	sim154x_enbl_mbif {
	uint8	disabled;			/* 1 = MBX init. disabled */
	uint8	lock_code;			/* MBX lock code */
};

/*
 * Array of all Adaptec 154x host adaptor structures, its physical address,
 * and the number of active adaptors.
 */
static	struct sim154x_adaptor *sim154x_adaptors = NULL;
static	struct sim154x_adaptor *sim154x_adaptors_pa = NULL;
static	int sim154x_adaptors_handle;
static	int sim154x_nadaptors = 0;

/*
 * Parameters.
 */
extern	struct cam_params cam_params;

/*
 * Local function prototypes.
 */
#ifdef	__STDC__
static	struct sim154x_adaptor *sim154x_get_adaptor(long path_id);
static	long sim154x_exec_cmd(short unsigned ioport, char unsigned cmd,
	                       char unsigned *input, int ninput,
	                       char unsigned *output, int noutput);
static	long sim154x_get_config(short unsigned ioport, int *brdid,
	                        int *intr_level, int *dma_level);
static	long sim154x_mk_sg_list(CCB *ccb, struct sim154x_adaptor *adaptor,
	                        int index);
static	long sim154x_rm_sg_list(CCB *ccb, struct sim154x_adaptor *adaptor,
	                        int index);
static	int sim154x_probe(short unsigned ioport);
long	sim154x_start(CCB *ccb);
long	sim154x_init(long path_id);
long	sim154x_action(CCB *ccb);
#endif

/*
 * External funciton prototypes.
 */
#ifdef	__STDC__
char	unsigned inportb(int portid);
void	outportb(int portid, unsigned char value);
#endif

/*
 * sim154x_get_adaptor - get the adaptor pointer associated w/ 'path_id'.
 */
static	struct sim154x_adaptor *sim154x_get_adaptor(path_id)
long	path_id;
{
	struct	sim154x_adaptor *adaptor = sim154x_adaptors;
	int	i;

	for(i = 0; i < sim154x_nadaptors; i++, adaptor++)
		if(adaptor->bus_info.path_id == path_id)
			break;
	if(i >= sim154x_nadaptors)
		return(NULL);
	return(adaptor);
}

/*
 * sim154x_mk_sg_list - build a 154X style scatter/gather list. Allocate
 * bounce buffers for pages with physical addresses greater than 16MB.
 */
static	long sim154x_mk_sg_list(ccb, adaptor, index)
CCB	*ccb;
struct	sim154x_adaptor *adaptor;
int	index;
{
	CAM_SG_ELEM cam_sg_elem, *cam_sg_list;
	struct	sim154x_sg_elem *adp_sg_list;
	struct	sim154x_bounce_info *bounce_info;
	int	unsigned *addr_map;
	void	*va;
	long	length;
	uint32	pa;
	int	cam_sg_count, i, adp_sg_length;
	struct	sim154x_sg_info *adp_sg_info;
	static	char *myname = "sim154x_mk_sg_list";

	if(ccb->header.cam_flags & CAM_SG_VALID) {
		cam_sg_list = ccb->scsiio.sg_list;
		cam_sg_count = ccb->scsiio.sg_count;
	} else {
		cam_sg_list = &cam_sg_elem;
		cam_sg_list->sg_address = (void *)ccb->scsiio.sg_list;
		cam_sg_list->sg_length = ccb->scsiio.xfer_len;
		cam_sg_count = 1;
	}

	adp_sg_list = adaptor->sg_list[index];
	addr_map = adaptor->sg_info[index].addr_map;
	bounce_info = adaptor->sg_info[index].bounce_info;
	adp_sg_info = &adaptor->sg_info[index];
	adp_sg_info->sg_count = 0;

	for(i = 0; i < cam_sg_count; i++, cam_sg_list++) {
	    bounce_info->uva = va = cam_sg_list->sg_address;
	    length = cam_sg_list->sg_length;
	    while(length > 0) {
		if(cam_page_wire(va, (void **)&pa, (int *)addr_map)
				!= CAM_SUCCESS) {
			cam_error(0, myname, "can't wire I/O buffer");
			(void)sim154x_rm_sg_list(ccb, adaptor, index);
			return(CAM_ENOMEM);
		}
/*
 * ISA bus can't handle addresses greater than 16MB. If the physical
 * address is greater than 16MB, unwire the page and use a bounce buffer
 * instead.
 */
		if(SIM154X_CANT_DMA(adaptor, pa)) {
			if(cam_page_release(*addr_map) != CAM_SUCCESS) {
				cam_error(0, myname, "can't unwire I/O buffer");
				(void)sim154x_rm_sg_list(ccb, adaptor, index);
				return(CAM_ENOMEM);
			}
			if((va = cam_alloc_mem(NBPG, NULL, NBPG)) == NULL) {
				cam_error(0, myname, "memory allocation error");
				(void)sim154x_rm_sg_list(ccb, adaptor, index);
				return(CAM_ENOMEM);
			}
			if(cam_page_wire(va, (void **)&pa, (int *)addr_map)
					!= CAM_SUCCESS) {
				cam_error(0, myname, "can't wire I/O buffer");
				(void)sim154x_rm_sg_list(ccb, adaptor, index);
				return(CAM_ENOMEM);
			}
			bounce_info->bva = va;
		} else
			bounce_info->bva = NULL;
/*
 * Determine the transfer length for this segment.
 * Fill in the segment's length and physical address.
 */
		adp_sg_length = NBPG - ((unsigned int)va & (NBPG - 1));
		if(adp_sg_length > length)
			adp_sg_length = length;
		SIM154X_SET_INT24(adp_sg_list->length, adp_sg_length);
		SIM154X_SET_INT24(adp_sg_list->address, pa);
/*
 * For bounce buffer writes, do the copy now.
 */
		if((ccb->header.cam_flags & CAM_DIR_OUT) &&
		   (bounce_info->bva != NULL))
			bcopy(bounce_info->uva, bounce_info->bva,
			      adp_sg_length);
/*
 * Update pointers, counters.
 */
		va += adp_sg_length;
		length -= adp_sg_length;
		bounce_info->len = adp_sg_length;
		adp_sg_info->sg_count++;
		adp_sg_list++;
		addr_map++;
		bounce_info++;
	    }
	}

	return(CAM_SUCCESS);
}

/*
 * sim154x_rm_sg_list - unwire pages setup in sim154x_rm_sg_list().
 * For reads, copy bounce buffer data. Free bounce buffers.
 */
static	long sim154x_rm_sg_list(ccb, adaptor, index)
CCB	*ccb;
struct	sim154x_adaptor *adaptor;
int	index;
{
	struct	sim154x_bounce_info *bounce_info;
	int	unsigned *addr_map;
	int	adp_sg_count, i;
	static	char *myname = "sim154x_rm_sg_list";

	addr_map = adaptor->sg_info[index].addr_map;
	bounce_info = adaptor->sg_info[index].bounce_info;
	adp_sg_count = adaptor->sg_info[index].sg_count;

	for(i = 0; i < adp_sg_count; i++, addr_map++) {
/*
 * For bounce buffer reads, copy the data to the user buffer.
 */
		if((ccb != NULL) && (ccb->header.cam_flags & CAM_DIR_IN) &&
		   (bounce_info->bva != NULL))
			bcopy(bounce_info->bva, bounce_info->uva,
			      bounce_info->len);
/*
 * Unwire the current page.
 */
		if(cam_page_release(*addr_map) != CAM_SUCCESS)
			cam_error(0, myname, "address map release error");
/*
 * Free the bounce buffer, if necessary.
 */
		if(bounce_info->bva != NULL)
			cam_free_mem(bounce_info->bva, NBPG);

		bounce_info++;
	}

	adaptor->sg_info[index].sg_count = 0;
	return(CAM_SUCCESS);
}

/*
 * sim154x_adaptor_reset()
 *	Perform a hard or a reset on the input adaptor.
 *
 * RETURNS:
 *	CAM_SUCCESS			success.
 *	CAM_EIO				operation timed out.
 */
long	sim154x_adaptor_reset(adaptor, reset_type)
struct	sim154x_adaptor *adaptor;
int	reset_type;
{
	int	count;
	short	unsigned csrport = adaptor->ioport + SIM154X_CSR_OFF;
	static	char *myname = "sim154x_adaptor_reset";

	if(reset_type == SIM154X_HRST) {
		outportb(csrport, SIM154X_HRST);
		for(count = 0; count < 100000; count++) {
			if(inportb(csrport) & SIM154X_STST)
				break;
		}
		if(count >= 100000) {
			cam_error(0, myname, "hard reset timed out");
			return(CAM_EIO);
		}
	} else {
		outportb(csrport, SIM154X_SRST);
		for(count = 0; count < 100000; count++) {
			if(inportb(csrport) & (SIM154X_MBIR | SIM154X_IDLE))
				break;
		}
		if(count >= 100000) {
			cam_error(0, myname, "soft reset timed out");
			return(CAM_EIO);
		}
	}
	return(CAM_SUCCESS);
}

/*
 * sim154x_exec_cmd - execute an adaptor command.
 */
static long
sim154x_exec_cmd(short unsigned ioport, char unsigned cmd,
                 char unsigned *input, int ninput,
                 char unsigned *output, int noutput)
{
	char	unsigned *p;
	int	i;
	long	count;
	short	unsigned csrport = ioport + SIM154X_CSR_OFF;
	short	unsigned cdrport = ioport + SIM154X_CDR_OFF;
	short	unsigned intrport = ioport + SIM154X_INTR_OFF;
	static	char *myname = "sim154x_exec_cmd";

	if(!(inportb(csrport) & SIM154X_IDLE) &&
	   (cmd != SIM154X_SCSI_START) && (cmd != SIM154X_BIOS_SCSI_ST)) {
		CAM_DEBUG_FCN_EXIT(myname, CAM_EBUSY);
		return(CAM_EBUSY);
	}
/*
 * Write the input data.
 */
	for(p = input, i = 0; i < (ninput + 1); i++) {
		count = 0;
		while(inportb(csrport) & SIM154X_CDF) {
			if(count++ > 100000) {
				cam_error(0, myname,
				          "write failed on iteration %d", i);
				return(CAM_EIO);
			}
		}
		if(i == 0)
			outportb(cdrport, cmd);
		else
			outportb(cdrport, *p++);
	}
/*
 * Read the output data.
 */
	for(p = output, i = count = 0; i < noutput; i++) {
		while(!(inportb(csrport) & SIM154X_DF)) {
			if(count++ > 100000) {
				cam_error(0, myname,
				          "read failed on iteration %d", i);
				return(CAM_EIO);
			}
		}
		*p++ = inportb(cdrport);
	}
/*
 * Wait for the command to complete.
 */
	if((cmd != SIM154X_SCSI_START) && (cmd != SIM154X_BIOS_SCSI_ST)) {
		count = 0;
		while(!(inportb(intrport) & SIM154X_HACC)) {
			if(count++ > 100000) {
				cam_error(0, myname, "HACC wait failed");
				outportb(csrport, SIM154X_IRST);
				return(CAM_EIO);
			}
		}
		outportb(csrport, SIM154X_IRST);
	}

	return(CAM_SUCCESS);
}

/*
 * sim154x_get_config - get configuration data from the adaptor. Convert
 * the adaptor bit field configuration information to their corresponding
 * integer values.
 */
static	long sim154x_get_config(short unsigned ioport, int *brdid,
	                        int *intr_level, int *dma_level)
{
	int	i, j;
	long	status;
	struct	sim154x_config config;
	struct	sim154x_adaptor_inq adaptor_inq;
	static	char unsigned ilevels[8] = { 9, 10, 11, 12, 255, 13, 14, 255 };
	static	char *myname = "sim154x_get_config";
/*
 * Try to do an adaptor inquiry. Don't fail if it can't be done -
 * some older boards may not handle it.
 */
	if(sim154x_exec_cmd(ioport, SIM154X_ADAPTOR_INQ, NULL, 0,
	                (unsigned char *)&adaptor_inq,
	                sizeof(struct sim154x_adaptor_inq)) == CAM_SUCCESS)
		*brdid = adaptor_inq.brdid;
	else
		*brdid = 0;
/*
 * Get the configuration data from the adaptor.
 */
	status = sim154x_exec_cmd(ioport, SIM154X_CONF_DATA,
	                          NULL, 0, (unsigned char *)&config,
	                          sizeof(struct sim154x_config));
	if(status != CAM_SUCCESS)
		return(status);
/*
 * Convert the bit-encoded interrupt channel.
 */
	*intr_level = 255;
	for(i = 0, j = 1; i < 8; i++, j <<= 1) {
		if(j & config.intr_chan) {
			*intr_level = ilevels[i];
			break;
		}
	}
	if(*intr_level == 255) {
		cam_error(0, myname,
		         "interrupt level for ioport 0x%x not found",
		          ioport);
		return(CAM_EIO);
	}
/*
 * Convert the bit-encoded DMA channel.
 */
	for(i = 0, j = 1; i < 8; i++, j <<= 1) {
		if(j & config.dma_chan) {
			*dma_level = i;
			break;
		}
	}
	if(i >= 8) {
		cam_error(0, myname, "DMA channel for ioport 0x%x not found",
		          ioport);
		return(CAM_EIO);
	}

	return(CAM_SUCCESS);
}

/*
 * sim154x_dma_chan_init - DMA channel initialization.
 */
static	long sim154x_dma_chan_init(dma_level)
int	dma_level;
{
	static	struct {
		int	chan;
		short	unsigned port1;
		short	unsigned data1;
		short	unsigned port2;
		short	unsigned data2;
	} dma_init[] = {
		{ 0, 0x0b, 0xc0, 0x0a, 0x00 },
		{ 5, 0xd6, 0xc1, 0xd4, 0x01 },
		{ 6, 0xd6, 0xc2, 0xd4, 0x02 },
		{ 7, 0xd6, 0xc3, 0xd4, 0x03 },
	};
	static	ndma_init = sizeof(dma_init) / sizeof(dma_init[0]);
	int	i;

	for(i = 0; i < ndma_init; i++) {
		if(dma_level == dma_init[i].chan) {
			outportb(dma_init[i].port1, dma_init[i].data1);
			outportb(dma_init[i].port2, dma_init[i].data2);
			break;
		}
	}

	return(i < ndma_init ? CAM_SUCCESS : CAM_EIO);
}

/*
 * sim154x_probe - is there an Adaptec 154x host adaptor at the
 * input ioport?
 */
static int
sim154x_probe(short unsigned ioport)
{
	int x;
/*
 * Quick check for no card at this address.
 */
	if(inportb(ioport + SIM154X_CSR_OFF) == 0xff)
		return(0);
/*
 * Tell card to reset, wait up to 1 second for it to
 * complete.  Otherwise assume card does not exist.
 */
	if(cam_params.nobootbrst)
		outportb(ioport + SIM154X_CSR_OFF, SIM154X_SRST | SIM154X_IRST);
	else
		outportb(ioport + SIM154X_CSR_OFF, SIM154X_HRST);
	for (x = 0; x < 10; ++x) {
		if((unsigned short)inportb(ioport + SIM154X_CSR_OFF) ==
		   (SIM154X_IDLE | SIM154X_MBIR))
			break;
		cam_msleep(100);
	}
	if(x >= 10)
		return(0);

	return(1);
}

/*
 * sim154x_start
 *	Start I/O on the input CCB.
 */
long	sim154x_start(ccb)
CCB	*ccb;
{
	struct	sim154x_adaptor *adaptor;
	struct	sim154x_accb *accb;
	struct	sim154x_mbx *mbo;
	uint32	physadr;
	long	status = CAM_SUCCESS;
	int	dir, mbidx, length, adn, count;
	static	char *myname = "sim154x_start";

	CAM_DEBUG_FCN_ENTRY(myname);

	if((adaptor = sim154x_get_adaptor(ccb->header.path_id)) == NULL) {
		CAM_DEBUG_FCN_EXIT(myname, CAM_ENOENT);
		return(CAM_ENOENT);
	}
/*
 * Get an Adaptec Command Control Block and an output mailbox.
 * Use MBO entries in round-robin order - the order in which
 * the HBA searches for them.
 */
	mbidx = adaptor->next_mbo;
	for(count = 0; count < SIM154X_NMBX; count++, mbidx++) {
		if(mbidx >= SIM154X_NMBX)
			mbidx = 0;
		mbo = &adaptor->mbos[mbidx];
		if(mbo->cmdsts == 0) {
			adaptor->next_mbo = mbidx + 1;
			accb = &adaptor->accbs[mbidx];
			if(accb->scsi_cmdlen == 0)
				break;
		}
	}
	if(count >= SIM154X_NMBX) {
		cam_error(0, myname, "mailbox out not found");
		CAM_DEBUG_FCN_EXIT(myname, CAM_EIO);
		return(CAM_EIO);
	}
/*
 * Initialize the ACCB.
 */
	accb = &adaptor->accbs[mbidx];
	mbo = &adaptor->mbos[mbidx];

	switch(ccb->header.fcn_code) {
	case XPT_SCSI_IO:
/*
 * Wire down the I/O buffer and make an adaptor scatter/gather list.
 */
		if(sim154x_mk_sg_list(ccb, adaptor, mbidx) != CAM_SUCCESS) {
			status = CAM_ENOMEM;
			break;
		}
/*
 * Fill in the Adaptec Command Control Block.
 */
		length = adaptor->sg_info[mbidx].sg_count *
		         sizeof(struct sim154x_sg_elem);
		SIM154X_SET_INT24(accb->data_len, length);
/*
 * For commands that don't involve any data transfer (eg, TUR), the
 * host adaptor doesn't want an SG opcode.
 */
		if(length > 0)
			accb->opcode = SIM154X_INIR_BOTH_ACCB;
		else
			accb->opcode = SIM154X_INIR_ACCB;
		dir = 0;
		if(ccb->header.cam_flags & CAM_DIR_IN)
			dir |= SIM154X_DIR_IN;
		if(ccb->header.cam_flags & CAM_DIR_OUT)
			dir |= SIM154X_DIR_OUT;
		accb->addr_dir = SIM154X_ACCB_ADC(ccb->header.target,
		                                  ccb->header.lun, dir);
		accb->scsi_cmdlen = ccb->scsiio.cdb_len;
/*		accb->rqsns_len = 0; */
		accb->rqsns_len = 1;		/* disable autosense */

		adn = adaptor - sim154x_adaptors;
		physadr = (uint32)sim154x_adaptors_pa[adn].sg_list[mbidx];
		SIM154X_SET_INT24(accb->data_ptr, physadr);

		SIM154X_SET_INT24(accb->link_ptr, 0);
		accb->cmd_link_id = 0;
/*
 * Fill in the CDB.
 */
		bcopy(ccb->scsiio.cdb, accb->data, accb->scsi_cmdlen);
		CAM_DEBUG(CAM_DBG_MSG, myname, "copying CDB opcode %d",
		          accb->data[0]);
/*
 * Start the SCSI I/O operation.
 */
		CAM_DEBUG(CAM_DBG_MSG, myname, "starting I/O on adaptor %d", 0);
		mbo->cmdsts = SIM154X_MBO_START;
		status = sim154x_exec_cmd(adaptor->ioport, SIM154X_SCSI_START,
		                          NULL, 0, NULL, 0);
		break;
	default:
		cam_error(0, myname, "invalid function code (%d)",
		          ccb->header.fcn_code);
		status = CAM_EINVAL;
	}

	CAM_DEBUG_FCN_EXIT(myname, status);
	return(status);
}

/*
 * Sim_154x_intr - handle adaptor interrupts.
 */
void	sim154x_intr(irq, adn)
long	irq, adn;
{
	struct	sim154x_adaptor *adaptor;
	struct	sim154x_accb *accb, *accb_pa;
	struct	sim154x_mbx *mbi;
	CCB	*ccb;
	int	mbidx, accb_idx, target;
	uint8	mbi_status, intr_status, cam_status;
	static	char *myname = "sim154x_intr";

	CAM_DEBUG_FCN_ENTRY(myname);

	adaptor = &sim154x_adaptors[adn];

	intr_status = inportb(adaptor->ioport + SIM154X_INTR_OFF);
	outportb(adaptor->ioport + SIM154X_CSR_OFF, SIM154X_IRST);

	CAM_DEBUG(CAM_DBG_MSG, myname, "intr status = 0x%x", intr_status);

/*
 * Search for the first active input mailbox.
 */
	mbi = adaptor->mbis;
	for(mbidx = 0; mbidx < SIM154X_NMBX; mbidx++, mbi++) {
		if((mbi_status = mbi->cmdsts) != 0) {
			break;
		}
	}
	if(mbidx >= SIM154X_NMBX) {
		cam_error(0, myname, "mailbox in not found");
		return;
	}
/*
 * Convert the ACCB physical address in the mailbox in into the
 * corresponding ACCB virtual address.
 * Get the target.
 */
	accb_pa = (struct sim154x_accb *)SIM154X_GET_INT24(mbi->addr);
	accb_idx = accb_pa - sim154x_adaptors_pa->accbs;
	accb = &adaptor->accbs[accb_idx];
	target = SIM154X_ACCB_TARG(accb);
/*
 * Get the current CAM CCB and release wired memory.
 */
	if((ccb = sim_get_active_ccb(&adaptor->bus_info, target, 0)) == NULL) {
		cam_error(0, myname, "can't get active CCB for %d/%d/%d",
		          adn, target, 0);
		mbi->cmdsts = 0;
		accb->scsi_cmdlen = 0;
		CAM_DEBUG_FCN_EXIT(myname, CAM_EIO);
		return;
	}
	(void)sim154x_rm_sg_list(ccb, adaptor, accb_idx);
/*
 * Map the adaptor status into a CAM status code.
 */
	switch(accb->hastat) {
	case SIM154X_REQ_CMP:
		cam_status = CAM_REQ_CMP;
		break;
	case SIM154X_SEL_TIMEOUT:
		cam_status = CAM_SEL_TIMEOUT;
		break;
	case SIM154X_DATA_OVUN_RUN:
		cam_status = CAM_DATA_OVUN_RUN;
		break;
	case SIM154X_UNEXP_BUSFREE:
		cam_status = CAM_UNEXP_BUSFREE;
		break;
	case SIM154X_TARG_SEQ_FAIL:
		cam_status = CAM_TARG_SEQ_FAIL;
		break;
	default:
		cam_error(0, myname,
		          "adaptor status %d not mapped to CAM status",
		          accb->hastat);
		cam_status = CAM_REQ_CMP_ERR;
	}
/*
 * Fill in the CCB status and residual fields.
 */
	ccb->scsiio.scsi_status = accb->tarstat;
	ccb->header.cam_status = cam_status;
	ccb->scsiio.resid = SIM154X_GET_INT24(accb->data_len);
/*
 *  Call sim_complete() to finish the I/O processing.
 */
	CAM_DEBUG(CAM_DBG_MSG, myname, "target = %d", target);
	sim_complete(&adaptor->bus_info, ccb);
/*
 * Finished with the mailbox and the Adaptec CCB, release them.
 */
	mbi->cmdsts = 0;
	accb->scsi_cmdlen = 0;
	
	CAM_DEBUG_FCN_EXIT(myname, CAM_SUCCESS);
}

/*
 * sim154x_init - Adaptec 154x initialization code.
 */
long	sim154x_init(path_id)
long	path_id;
{
	struct	sim154x_adaptor *adaptor;
	struct	sim_target *target_info;
	int	i, adn, brdid, intr_level, dma_level, status = CAM_SUCCESS;
	uint32	accb_pa;
	short	unsigned ioport;
	struct	sim154x_mbxinit mbxinit;
	struct	sim154x_ext_bios ext_bios;
	struct	sim154x_enbl_mbif enbl_mbif;
	static	char *myname = "sim154x_init";
	long	sim154x_start();

	CAM_DEBUG_FCN_ENTRY(myname);

	if(sim154x_nadaptors >= SIM154X_MAX_ADAPTORS) {
		cam_error(0, myname, "not enough adaptor structures");
		return(CAM_ENMFILE);
	}

	if(sim154x_adaptors == NULL) {
/*
 * Allocate storage for an adaptor structure.
 */
		sim154x_adaptors = cam_alloc_mem(NBPG, NULL, NBPG);
		if(sim154x_adaptors == NULL) {
			cam_error(0, myname,
			          "adaptor structure allocation error");
			return(CAM_ENOMEM);
		}
/*
 * Wire down and determine the physical address for the adaptors
 * array.
 */
		if(cam_page_wire(sim154x_adaptors,
		                 (void **)&sim154x_adaptors_pa,
		                 &sim154x_adaptors_handle) != CAM_SUCCESS) {
			cam_error(0, myname,
			          "can't wire down adaptors structure");
			return(CAM_ENOMEM);
		}
/*
 * Enable I/O for the needed range.
 */
		if(cam_enable_io(SIM154X_LOW, SIM154X_HIGH) != CAM_SUCCESS) {
			cam_error(0, myname, "enable I/O error");
			status = CAM_EIO;
		}
	}

	if(status == CAM_SUCCESS) {
/*
 * Search for the next available adaptor.
 */
		for(i = 0; i < SIM154X_NADDRS; i++) {
			ioport = sim154x_addrs[i];
/*
 * Search for adaptor port already in use.
 */
			for(adn = 0; adn < sim154x_nadaptors; adn++)
				if(ioport == sim154x_adaptors[adn].ioport)
					break;
			if(adn < sim154x_nadaptors)
				continue;
/*
 * Found an unused port, see if there's an adaptor there.
 */
			if(sim154x_probe(ioport))
				break;
		}
/*
 * Was an adaptor found?
 */
		if(i >= SIM154X_NADDRS)
			status = CAM_ENMFILE;
	}

	if(status == CAM_SUCCESS) {
		adn = sim154x_nadaptors;
		adaptor = &sim154x_adaptors[adn];
		adaptor->ioport = ioport;
		adaptor->bus_info.path_id = path_id;
		adaptor->bus_info.phase = SCSI_BUS_FREE;
		adaptor->bus_info.bus_flags = 0;
		adaptor->bus_info.last_target = CAM_MAX_TARGET;
		adaptor->bus_info.start = sim154x_start;
/*
 * Reset the adaptor.
		(void)sim154x_adaptor_reset(adaptor, SIM154X_SRST);
 */
/*
 * Get the adaptor configuration data.
 */
		status = sim154x_get_config(ioport, &brdid, &intr_level,
		                            &dma_level);
		if(status != CAM_SUCCESS) {
			cam_error(0, myname,
			    "can't get configuration data for adaptor %d", adn);
		} else {
			CAM_DEBUG(CAM_DBG_MSG, myname, "intr. level = %d",
			          intr_level);
			CAM_DEBUG(CAM_DBG_MSG, myname, "DMA level = %d",
			          dma_level);
			CAM_DEBUG(CAM_DBG_MSG, myname, "board ID = %d", brdid);
		}
		adaptor->intr_level = intr_level;
		adaptor->dma_level = dma_level;
		adaptor->brdid = brdid;
/*
 * Disable the extended BIOS for 1542C and 1542CF adaptors.
 */
		if((adaptor->brdid == SIM154X_ID_154XC) ||
		   (adaptor->brdid == SIM154X_ID_154XCF)) {
		    (void)sim154x_exec_cmd(ioport, SIM154X_EXT_BIOS,
		   	                   NULL, 0, (unsigned char *)&ext_bios,
		   	                   sizeof(struct sim154x_ext_bios));
		    if(ext_bios.lock_code != 0) {
			syslog(LOG_INFO,
			       "SCSI/CAM: unlocking mailbox on adaptor %d\n",
			       path_id);
			enbl_mbif.disabled = 0;
			enbl_mbif.lock_code = ext_bios.lock_code;
			(void)sim154x_exec_cmd(ioport, SIM154X_ENBL_MBIF,
			                   (unsigned char *)&enbl_mbif,
			                   sizeof(struct sim154x_enbl_mbif),
			                   NULL, 0);
		    }
		}
/*
 * Initialize the target queues.
 */
		target_info = adaptor->bus_info.target_info;
		for(i = 0; i < CAM_NTARGETS; i++, target_info++) {
			CAM_INITQUE(&target_info->head);
			target_info->nactive = 0;
		}
/*
 * Initialize the mailbox out's.
 */
		accb_pa = (uint32)&sim154x_adaptors_pa[adn].accbs;
		for(i = 0; i < SIM154X_NMBX; i++) {
			adaptor->mbos[i].cmdsts = 0;
			SIM154X_SET_INT24(adaptor->mbos[i].addr, accb_pa);
			accb_pa += sizeof(struct sim154x_accb);
		}
/*
 * Do the adaptor mailbox initialization.
 */
		mbxinit.mbx_count = SIM154X_NMBX;
		SIM154X_SET_INT24(mbxinit.mbx_addr, 
		                   (uint32)sim154x_adaptors_pa[adn].mbos);
		status = sim154x_exec_cmd(ioport, SIM154X_MBXINIT,
		                           (unsigned char *)&mbxinit,
		                           sizeof(mbxinit),
		                           NULL, 0);
		if(status != CAM_SUCCESS) {
			cam_error(0, myname,
			    "mailbox initialization error on adaptor %d", adn);
		}
/*
 * DMA channel initialization.
 */
		if(status == CAM_SUCCESS) {
		    status = sim154x_dma_chan_init(dma_level);
		    if(status != CAM_SUCCESS)
			cam_error(0, myname,
			          "DMA initialization error for adaptor %d",
			          adn);
		}
/*
 * Enable interrupt handling for this adaptor.
 */
		if(status == CAM_SUCCESS) {
		    if((status = cam_enable_isr(intr_level, adn, sim154x_intr))
				!= CAM_SUCCESS)
			cam_error(0, myname,
			          "can't enable interrupts on adaptor %d",
			          adn);
		}
	}

	if(status == CAM_SUCCESS) {
		syslog(LOG_INFO,
		       "%s on bus %d, port 0x%x, IRQ %d, DMAC %d\n",
		       "Adaptec 154x", path_id, ioport, intr_level, dma_level);
		sim154x_nadaptors++;
	}

	CAM_DEBUG_FCN_EXIT(myname, status);
	return(status);
}

long	sim154x_action(ccb)
CCB	*ccb;
{
	struct	sim154x_adaptor *adaptor;
	int	status;
	static	char *myname = "sim154x_action";

	CAM_DEBUG_FCN_ENTRY(myname);

	if((adaptor = sim154x_get_adaptor(ccb->header.path_id)) == NULL)
		return(CAM_EINVAL);

	status = sim_action(ccb, &adaptor->bus_info);

	CAM_DEBUG_FCN_EXIT(myname, status);
	return(status);
}

