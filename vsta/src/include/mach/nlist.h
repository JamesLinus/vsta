#ifndef _NLIST_H
#define _NLIST_H
/*
 * nlist.h
 *	Name list portion of an a.out
 */
#include <sys/types.h>

#define M_386 (100)	/* Value for N_MACHTYPE */

#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#define N_MACHTYPE(exec) ((enum machine_type)(((exec).a_info >> 16) & 0xff))
#define N_FLAGS(exec) (((exec).a_info >> 24) & 0xff)
#define N_SET_INFO(exec, magic, type, flags) \
        ((exec).a_info = ((magic) & 0xffff) \
         | (((int)(type) & 0xff) << 16) \
         | (((flags) & 0xff) << 24))
#define N_SET_MAGIC(exec, magic) \
        ((exec).a_info = (((exec).a_info & 0xffff0000) | ((magic) & 0xffff)))
#define N_SET_MACHTYPE(exec, machtype) \
        ((exec).a_info = \
         ((exec).a_info&0xff00ffff) | ((((int)(machtype))&0xff) << 16))
#define N_SET_FLAGS(exec, flags) \
        ((exec).a_info = \
         ((exec).a_info&0x00ffffff) | (((flags) & 0xff) << 24))

/* Executable types */
#define OMAGIC 0407	/* Writable text */
#define NMAGIC 0410	/* Pure text */
#define ZMAGIC 0413	/* Pure text, aligned for demand paging */

#define N_BADMAG(x)                                     \
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC          \
  && N_MAGIC(x) != ZMAGIC)
#define _N_BADMAG(x)                                    \
 (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC          \
  && N_MAGIC(x) != ZMAGIC)

#define _N_HDROFF(x) 0

#define N_TXTOFF(x) \
 (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct aout) : \
	 sizeof (struct aout))
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#define N_TXTADDR(x) (sizeof(struct aout)+4096)

/* Data address rounded up to next SEGMENT_SIZE segment after text */
#define SEGMENT_SIZE 0x400000

#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (N_TXTADDR(x)+(x).a_text) \
     : (SEGMENT_SIZE + ((N_TXTADDR(x)+(x).a_text-1) & ~(SEGMENT_SIZE-1))))

/* Address of bss segment in memory after it is loaded.  */
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
struct nlist {
	union {
		char *n_name;
		struct nlist *n_next;
		long n_strx;
	} n_un;
	uchar n_type;
	char n_other;
	short n_desc;
	ulong n_value;
};

/* Types of symbol entries */
#define N_UNDF 0		/* Undefined */
#define N_ABS 2			/* Absolute */
#define N_TEXT 4		/* Symbol in text segment */
#define N_DATA 6		/*  ...in data */
#define N_BSS 8			/*  ...in bss */
#define N_FN 15			/* ??? */
#define N_EXT 1
#define N_TYPE 036
#define N_STAB 0340
#define N_INDR 0xa
#define N_SETA  0x14            /* Absolute set element symbol */
#define N_SETT  0x16            /* Text set element symbol */
#define N_SETD  0x18            /* Data set element symbol */
#define N_SETB  0x1A            /* Bss set element symbol */
#define N_SETV  0x1C            /* Pointer to set vector in data area.  */

struct relocation_info {
	int r_address;
	uint r_symbolnum:24;
	uint r_pcrel:1;
	uint r_length:2;
	uint r_extern:1;
	uint r_pad:4;
};

#endif /* _NLIST_H */
