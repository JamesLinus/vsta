/* 
 * pio.h
 *    Data and function prototypes for the portable I/O library.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 */
#ifndef __PIO_H__
#define __PIO_H__

#include <stdio.h>

enum {					 /* Buffer types                   */
   PIO_OUTPUT,				 /* Writing to buffer              */
   PIO_INPUT,				 /* reading from buffer            */
};

typedef struct {
   unsigned char *buffer;		 /* the buffer                     */
   unsigned long buffer_size;		 /* size of the buffer             */
   unsigned long buffer_end;		 /* end of the buffer              */
   unsigned long buffer_pos;		 /* current position in the buffer */
   char buffer_type;			 /* PIO_OUTPUT or PIO_INPUT        */
} pio_buffer_t;

/*
 * buffer creation/deletions/etc. 
 */
extern pio_buffer_t  *pio_create_buffer(unsigned char *buffer, long size);
extern void           pio_destroy_buffer(pio_buffer_t * buffer);
extern void           pio_reset_buffer(pio_buffer_t * buffer);
extern int            pio_eof(pio_buffer_t * temp);

/*
 * Buffer I/O.
 */
extern int            pio_write_buffer(pio_buffer_t *temp, FILE *file);
extern pio_buffer_t  *pio_read_buffer(FILE *file);
extern pio_buffer_t  *pio_read_compressed(FILE *file);
extern int            pio_write_compressed(pio_buffer_t *buffer, FILE *file);
extern int            pio_compress_buffer(pio_buffer_t *inbuff, 
					  pio_buffer_t *outbuff);
extern int            pio_decompress_buffer(pio_buffer_t *inbuff, 
					    pio_buffer_t *outbuff);


/*
 * Simple type I/O
 */
extern int pio_u_char(pio_buffer_t * temp, unsigned char *ptr);
extern int pio_u_short(pio_buffer_t * temp, unsigned short *ptr);
extern int pio_u_long(pio_buffer_t * temp, unsigned long *ptr);
extern int pio_u_int(pio_buffer_t * temp, unsigned int *ptr);
extern int pio_char(pio_buffer_t * temp, char *ptr);
extern int pio_short(pio_buffer_t * temp, short *ptr);
extern int pio_long(pio_buffer_t * temp, long *ptr);
extern int pio_int(pio_buffer_t * temp, int *ptr);

/*
 * String, array, and raw data I/O
 */
extern int pio_string(pio_buffer_t * temp, char **ptr,
		      unsigned long max_len);
extern int pio_raw_bytes(pio_buffer_t * temp, unsigned char **ptr,
			 unsigned long *num_bytes, unsigned long max_len);
extern int pio_array(pio_buffer_t * temp, void **ptr,
		     unsigned long datum_size, unsigned long *datum_num,
		     unsigned long max_len, int (*convert_function) ());

#endif					 /* __PIO_H__ */
