/* Software options */
#define	SERVERS		1	/* Include TCP servers */
#define	TRACE		1	/* Include packet tracing code */
#define	NSESSIONS	10	/* Number of interactive clients */
#undef	SCREEN		1	/* trace screen on the Atari-ST */
#define	TYPE		1	/* Include type command */
#define	FLOW		1	/* Enable local tty flow control */
#undef  TNC2		1       /* TNC2 MBBIOS emulator */
#define MODEM_CALL	1	/* Include modem dialing for SLIP */

/* Hardware configuration */
#undef	PC_EC		0	/* 3-Com 3C501 Ethernet controller */
#define	SLIP		1	/* Serial line IP */
#undef	SLFP		0	/* MIT Serial line framing protocol */
#define	KISS		0	/* KISS TNC code */
#undef	HAPN		0	/* Hamilton Area Packet Network driver code */
#undef	EAGLE		0	/* Eagle card driver */
#undef	PACKET		0	/* FTP Software's Packet Driver interface */
#undef	PC100		0	/* PAC-COM PC-100 driver code */
#undef	APPLETALK	0	/* Appletalk interface (Macintosh) */
#undef	PLUS		0	/* HP's Portable Plus is the platform */
#define GENERIC_ETH	1	/* generic Ethernet support */

/* software options */
#define _FINGER		1	/* add finger command code */ 
#define	MULPORT		1	/* include GRAPES multiport digipeater code */
#define	NRS  		1	/* NET/ROM async interface */
#define	NETROM		1	/* NET/ROM network support */

#if !defined(SLIP)
#undef MODEM_CALL	 	/* Don't Include modem dialing for SLIP */
#endif

#if	defined(NRS)
#undef	NETROM
#define	NETROM		1	/* NRS implies NETROM */
#endif

#if	(defined(NETROM) || defined(KISS) || defined(HAPN) || defined(EAGLE) || defined(PC100))
#define	AX25		1		/* AX.25 subnet code */
#endif

/* KISS TNC, SLIP, NRS or PACKET implies ASY */
#if (defined(KISS) || defined(PACKET) || defined(NRS) || defined(SLIP) || defined(SLFP))
#undef	ASY
#define	ASY		1	/* Asynch driver code */
#endif

#ifdef GENERIC_ETH
#define	ETHER	1		/* Generic Ethernet code */
#endif
