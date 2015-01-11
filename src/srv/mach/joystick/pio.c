/*
 * pio.c
 *    A simple portable I/O library.
 *
 * Copyright (C) 1993 by G.T.Nicol, all rights reserved.
 *
 * This library provides functionality similar to that of XDR from Sun.
 * One major additional feature is support for compressed buffers. The
 * compression scheme used is a dynamic huffman type encoding based on
 * splay trees. The original idea comes from Doug Jones, and this 
 * implementation is based on his pascal code. The algorithm is reasonably
 * fast, and provides a reasonable amount of compression.
 */
#include <stdio.h>
#include <stdlib.h>
#ifndef __MSDOS__
#include <stat.h>
#endif
#include "pio.h"
#include <fcntl.h>

#ifndef FALSE
#define FALSE  0
#endif
#ifndef TRUE
#define TRUE   1
#endif

/*
 * If this is undefined, all bytes accessed by the library
 * will have their bit-order reversed
 */
#define PIO_MSB_FIRST

/*
 * Memory allocation block size
 */
#define PIO_BLOCK_SIZE 512

/*
 * Constants for compression algorithm
 */
#define PIO_CHAR_RANGE   (255)
#define PIO_EOF_CODE     (PIO_CHAR_RANGE + 1)
#define PIO_MAXCHAR      (PIO_EOF_CODE + 1)
#define PIO_SUCCMAX      (PIO_MAXCHAR + 1)
#define PIO_TWICEMAX     (2 * PIO_MAXCHAR + 1)
#define PIO_ROOT         1
#define PIO_MAXSTATE     64
#define PIO_BIT_LIMIT    16

/*
 * Compressed stream magic numbers
 */
static unsigned char pio_magic_1 = 'P';
static unsigned char pio_magic_2 = 'Z';
static unsigned char pio_magic_3 = '0';
static unsigned char pio_magic_4 = '1';

/*
 * Tree type for the compressor
 */
typedef struct {
   int up[PIO_TWICEMAX + 1];
   int left[PIO_MAXCHAR + 1];
   int right[PIO_MAXCHAR + 1];
} pio_compress_tree;

/*
 * Our basic trees, and the compressor state.
 */
static pio_compress_tree *pio_compress_trees[PIO_MAXSTATE];
static int pio_compress_state;


/*
 * Compressor bit buffers
 */
static int pio_compress_input_buffer;
static int pio_compress_input_left;
static int pio_compress_input_garbage;
static int pio_compress_output_buffer;
static int pio_compress_output_left;
static int pio_compress_error = FALSE;

/*
 * We use this to reverse bits when the bit on machines where
 * the MSB is last.
 */
#ifndef PIO_MSB_FIRST
static unsigned char pio_reverse_table[] = {
   0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
   0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
   0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
   0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
   0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
   0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
   0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
   0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
   0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
   0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
   0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
   0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
   0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
   0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
   0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
   0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
   0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
   0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
   0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
   0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
   0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
   0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
   0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
   0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
   0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
   0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
   0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
   0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
   0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
   0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
   0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
   0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};
#endif

/*
 * pio_malloc()
 *    A slightly safer malloc.
 */
static void *
pio_malloc(unsigned size)
{
   void *temp = NULL;

   if (size == 0) {
      fprintf(stderr, "pio: Trying to allocate 0 bytes. Changed to 512");
      size = 512;
   }
   temp = malloc(size);
   if (temp == NULL) {
      fprintf(stderr, "pio: Out of memory\n");
      exit(1);
   }
   memset(temp, 0, size);
   return (temp);
}

/*
 * pio_realloc()
 *    A slightly safer malloc.
 */
static void *
pio_realloc(void *ptr, unsigned size)
{
   void *temp = NULL;

   if (size == 0) {
      fprintf(stderr, "pio: Trying to reallocate to 0 bytes.\n");
      return (ptr);
   }
   temp = realloc(ptr, size);
   if (temp == NULL) {
      fprintf(stderr, "pio: Unable to reallocate memory\n");
      exit(1);
   }
   return (temp);
}

/*
 * pio_create_buffer()
 *    Create a pio buffer.
 *
 * If the "buffer" argument is non-NULL, and the "buffer_size" argument is
 * non-zero, this routine create an INPUT buffer; ie. a buffer you can read
 * from, otherwise, it will ignore the value of "buffer", and create a new
 * OUTPUT buffer; ie. a buffer to write to.
 */
pio_buffer_t *
pio_create_buffer(unsigned char *buffer, long buffer_size)
{
   pio_buffer_t *temp = pio_malloc(sizeof(pio_buffer_t));

   if (buffer == NULL || buffer_size == 0) {
      if (buffer_size == 0)
         buffer_size = PIO_BLOCK_SIZE;
      temp->buffer = pio_malloc(buffer_size);
      temp->buffer_type = PIO_OUTPUT;
   } else {
      temp->buffer = pio_malloc(buffer_size);
      memcpy(temp->buffer, buffer, buffer_size);
      temp->buffer_type = PIO_INPUT;
   }
   temp->buffer_size = buffer_size;
   temp->buffer_end = 0;
   temp->buffer_pos = 0;
   return (temp);
}

/*
 * pio_destroy_buffer()
 *    Free a buffer, and any associated data.
 */
void
pio_destroy_buffer(pio_buffer_t * buffer)
{
   if (buffer == NULL)
      return;

   if (buffer->buffer != NULL)
      free(buffer->buffer);
   free(buffer);
}

/*
 * pio_reset_buffer()
 *    Simply reset the buffer's position marker to 0
 */
void
pio_reset_buffer(pio_buffer_t * buffer)
{
   if (buffer == NULL)
      return;

   buffer->buffer_pos = 0;
}

/*
 * pio_check_size()
 *    Verify there are enough bytes to write some output, and grow if not.
 */
static void
pio_check_size(pio_buffer_t * buff, unsigned bytes_needed)
{
   int bytes_left = buff->buffer_size - buff->buffer_pos;

   if (bytes_left < 0) {
      fprintf(stderr, "pio: Buffer overrun in pio_check_size()\n");
   }
   if (bytes_left < bytes_needed) {
      buff->buffer_size +=
         bytes_needed < PIO_BLOCK_SIZE ? PIO_BLOCK_SIZE : bytes_needed;
      buff->buffer = pio_realloc(buff->buffer, buff->buffer_size);
   }
}

/*
 * pio_u_char()
 *    Input/Output an unsigned character from/to a buffer.
 */
int
pio_u_char(pio_buffer_t * temp, unsigned char *ptr)
{
   if (temp == NULL || ptr == NULL)
      return (-1);

   if (temp->buffer_type == PIO_OUTPUT) {
      pio_check_size(temp, 1);
#ifdef PIO_MSB_FIRST
      temp->buffer[temp->buffer_pos++] = *ptr;
#else
      temp->buffer[temp->buffer_pos++] = pio_reverse_table[*ptr];
#endif
      temp->buffer_end = temp->buffer_pos - 1;
   } else {
      if (temp->buffer_pos > temp->buffer_end) {
	 temp->buffer_pos = temp->buffer_end + 1;
         return (-1);
      }
#ifdef PIO_MSB_FIRST
      *ptr = temp->buffer[temp->buffer_pos++];
#else
      *ptr = pio_reverse_table[temp->buffer[temp->buffer_pos++]];
#endif
   }
   return (0);
}

/*
 * pio_char()
 *    Input/Output a character from/to a buffer.
 */
int
pio_char(pio_buffer_t * temp, char *ptr)
{
   return (pio_u_char(temp, (unsigned char *) ptr));
}

/*
 * pio_u_short()
 *    Input/Output an unsigned short from/to a buffer.
 */
int
pio_u_short(pio_buffer_t * temp, unsigned short *ptr)
{
   if (temp == NULL || ptr == NULL)
      return (-1);

   if (temp->buffer_type == PIO_OUTPUT) {
      pio_check_size(temp, 2);
#ifdef PIO_MSB_FIRST
      temp->buffer[temp->buffer_pos++] = *ptr & 0xff;
      temp->buffer[temp->buffer_pos++] = (*ptr >> 8) & 0xff;
#else
      temp->buffer[temp->buffer_pos++] =
         pio_reverse_table[*ptr & 0xff];
      temp->buffer[temp->buffer_pos++] =
         pio_reverse_table[(*ptr >> 8) & 0xff];
#endif
      temp->buffer_end = temp->buffer_pos - 1;
   } else {
      if (temp->buffer_pos >= temp->buffer_end) {
	 temp->buffer_pos = temp->buffer_end + 1;
         return (-1);
      }
#ifdef PIO_MSB_FIRST
      *ptr = temp->buffer[temp->buffer_pos++];
      *ptr |= (temp->buffer[temp->buffer_pos++] << 8);
#else
      *ptr = pio_reverse_table[temp->buffer[temp->buffer_pos++]];
      *ptr |= (pio_reverse_table[temp->buffer[temp->buffer_pos++]] << 8);
#endif
   }
   return (0);
}

/*
 * pio_short()
 *    Input/Output a short from/to a buffer.
 */
int
pio_short(pio_buffer_t * temp, short *ptr)
{
   return (pio_u_short(temp, (unsigned short *) ptr));
}

/*
 * pio_u_long()
 *    Input/Output an unsigned long from/to a buffer.
 */
int
pio_u_long(pio_buffer_t * temp, unsigned long *ptr)
{
   if (temp == NULL || ptr == NULL)
      return (-1);

   if (temp->buffer_type == PIO_OUTPUT) {
      pio_check_size(temp, 4);
#ifdef PIO_MSB_FIRST
      temp->buffer[temp->buffer_pos++] = *ptr & 0xff;
      temp->buffer[temp->buffer_pos++] = (*ptr >> 8) & 0xff;
      temp->buffer[temp->buffer_pos++] = (*ptr >> 16) & 0xff;
      temp->buffer[temp->buffer_pos++] = (*ptr >> 24) & 0xff;
#else
      temp->buffer[temp->buffer_pos++] = 
	 pio_reverse_table[*ptr & 0xff];
      temp->buffer[temp->buffer_pos++] = 
	 pio_reverse_table[(*ptr >> 8) & 0xff];
      temp->buffer[temp->buffer_pos++] = 
	 pio_reverse_table[(*ptr >> 16) & 0xff];
      temp->buffer[temp->buffer_pos++] =
         pio_reverse_table[(*ptr >> 24) & 0xff];
#endif
      temp->buffer_end = temp->buffer_pos - 1;
   } else {
      *ptr = 0;
      if (temp->buffer_pos >= temp->buffer_end - 2) {
	 temp->buffer_pos = temp->buffer_end + 1;
         return (-1);
      }
#ifdef PIO_MSB_FIRST
      *ptr |= temp->buffer[temp->buffer_pos++];
      *ptr |= (temp->buffer[temp->buffer_pos++] << 8);
      *ptr |= (temp->buffer[temp->buffer_pos++] << 16);
      *ptr |= (temp->buffer[temp->buffer_pos++] << 24);
#else
      *ptr |= pio_reverse_table[temp->buffer[temp->buffer_pos++]];
      *ptr |= (pio_reverse_table[temp->buffer[temp->buffer_pos++]] << 8);
      *ptr |= (pio_reverse_table[temp->buffer[temp->buffer_pos++]] << 16);
      *ptr |= (pio_reverse_table[temp->buffer[temp->buffer_pos++]] << 24);
#endif
   }
   return (0);
}

/*
 * pio_long()
 *    Input/Output a long from/to a buffer.
 */
int
pio_long(pio_buffer_t * temp, long *ptr)
{
   return (pio_u_long(temp, (unsigned long *) ptr));
}

/*
 * pio_u_int()
 *    Input/Output an unsigned integer from/to a buffer.
 *
 * Integers are read or written as longs.
 */
int
pio_u_int(pio_buffer_t * temp, unsigned int *ptr)
{
   int ret;
   unsigned long long_val = *ptr;
   if (temp == NULL || ptr == NULL)
      return (-1);

   ret = pio_u_long(temp, &long_val);
   if (temp->buffer_type == PIO_INPUT) {
      *ptr = (unsigned int) long_val;
   }
   return (ret);
}

/*
 * pio_int()
 *    Input/Output a integer from/to a buffer.
 *
 * Integers are read or written as longs.
 */
int
pio_int(pio_buffer_t * temp, int *ptr)
{
   int ret;
   long long_val = *ptr;
   if (temp == NULL || ptr == NULL)
      return (-1);

   ret = pio_long(temp, &long_val);
   if (temp->buffer_type == PIO_INPUT) {
      *ptr = (int) long_val;
   }
   return (ret);
}

/*
 * pio_string()
 *    Input/Output a C string.
 *
 * A pio string is represented as an unsigned long showing the length,
 * followed by length bytes. This routine allows the user to specify
 * the maximum length of the data via the max_len parameter. Upon input,
 * if *ptr == NULL, we allocate a new string, otherwise, we assume that
 * the user knows what he's doing (a dangerous assumption...)
 */
int
pio_string(pio_buffer_t * temp, char **ptr, unsigned long max_len)
{
   char inch, *cptr;
   unsigned long length, loop;

   if (temp == NULL || ptr == NULL)
      return (-1);

   if (temp->buffer_type == PIO_OUTPUT) {
      if (*ptr == NULL)
         return (-1);
      cptr = *ptr;
      length = strlen(cptr);
      if (max_len < length)
         length = max_len;
      pio_u_long(temp, &length);
      while (length--) {
         pio_char(temp, cptr++);
      }
   } else {
      if (pio_u_long(temp, &length) == -1) {
         fprintf(stderr, "pio: Bad string length\n");
         return (-1);
      }
      if (*ptr == NULL) {
         *ptr = pio_malloc(max_len + 1);
      }
      cptr = *ptr;
      for (loop = 0; loop < length; loop++) {
         if (pio_char(temp, &inch) == -1) {
            *cptr = '\0';
            return (-1);
         }
         if (loop < max_len)
            *cptr++ = inch;
      }
      *cptr = '\0';
   }
   return (0);
}

/*
 * pio_raw_bytes()
 *    Input/output some raw byte values.
 */
int
pio_raw_bytes(pio_buffer_t * temp, unsigned char **ptr,
              unsigned long *num_bytes, unsigned long max_len)
{
   unsigned char *cptr, inch;
   unsigned long length, loop = 0;

   if (temp == NULL || ptr == NULL)
      return (-1);

   if (temp->buffer_type == PIO_OUTPUT) {
      if (*ptr == NULL)
         return (-1);
      cptr = *ptr;
      pio_u_long(temp, &max_len);
      length = max_len;
      while (length--) {
         if (pio_u_char(temp, cptr++) == -1) {
            *num_bytes = (max_len - length);
         }
         loop++;
      }
   } else {
      if (pio_u_long(temp, &length) == -1) {
         fprintf(stderr, "pio: Bad raw bytes length\n");
         return (-1);
      }
      if (*ptr == NULL) {
         *ptr = pio_malloc(max_len);
      }
      cptr = *ptr;
      for (loop = 0; loop < length; loop++) {
         if (pio_u_char(temp, &inch) == -1) {
            *num_bytes = loop > max_len ? max_len : loop;
            return (-1);
         }
         if (loop < max_len)
            *cptr++ = inch;
      }
   }
   *num_bytes = loop > max_len ? max_len : loop;
   return (0);
}


/*
 * pio_array()
 *    Input/Output an array of something.
 */
int
pio_array(pio_buffer_t * temp, void **ptr, unsigned long datum_size,
          unsigned long *datum_num, unsigned long max_len,
          int (convert_function) ())
{
   unsigned char *cptr;
   unsigned long length, loop;

   if (temp == NULL || ptr == NULL)
      return (-1);

   if (temp->buffer_type == PIO_OUTPUT) {
      if (*ptr == NULL)
         return (-1);
      cptr = *ptr;
      pio_u_long(temp, &max_len);
      length = max_len;
      while (length--) {
         if ((*convert_function) (temp, cptr) == -1) {
            *datum_num = (max_len - length);
         }
         cptr += datum_size;
      }
   } else {
      if (pio_u_long(temp, &length) == -1) {
         fprintf(stderr, "Bad array length\n");
         return (-1);
      }
      if (*ptr == NULL) {
         *ptr = pio_malloc(max_len * datum_size);
      }
      cptr = *ptr;
      for (loop = 0; loop < length; loop++) {
         if ((*convert_function) (temp, cptr) == -1) {
            *datum_num = loop > max_len ? max_len : loop;
            return (-1);
         }
         if (loop < max_len) {
            cptr += datum_size;
         }
      }
      *datum_num = loop > max_len ? max_len : loop;
   }
   return (0);
}

/*
 * pio_write_buffer()
 *    Write a pio buffer to a file. The file must be opened for writing.
 */
int
pio_write_buffer(pio_buffer_t * temp, FILE * file)
{
   int ret = fwrite(temp->buffer, 1, temp->buffer_end, file);
   fflush(file);
   return (ret);
}

/*
 * pio_read_buffer()
 *    Read a pio buffer from a file into memory.
 */
pio_buffer_t *
pio_read_buffer(FILE * file)
{
   pio_buffer_t *temp = NULL;
   int fd = fileno(file);
   long size = 0;

#if !defined(PIO_MSB_FIRST) || defined(__MSDOS__)
   unsigned char inch;

   temp = pio_create_buffer(NULL, 0);
   while (read(fd, &inch, 1) == 1) {
      size += 1;
      pio_u_char(temp, &inch);
   }
#else
   struct stat statbuff;

   if(fstat(fd,&statbuff) == -1) {
      fprintf(stderr,"pio: Unable to stat file\n");
      return(NULL);
   }
   size = statbuff.st_size;
   temp = pio_create_buffer(NULL, size + 2);
   if(read(fd,temp->buffer,size) != size){
      fprintf(stderr,"pio: Unable to read %ld bytes\n",size);
      pio_destroy_buffer(temp);
      return(NULL);
   }
#endif

   temp->buffer_pos = 0;
   temp->buffer_end = size;
   temp->buffer_type = PIO_INPUT;
   return (temp);
}

/*
 * pio_compress_get_bit()
 *    Input a bit from the compressed stream.
 */
static inline int
pio_compress_get_bit(pio_buffer_t * inbuff)
{
   unsigned char temp;
   int bit;

   if (pio_compress_input_left == 0) {
      if (pio_eof(inbuff)) {
         pio_compress_input_garbage++;
         if (pio_compress_input_garbage > PIO_BIT_LIMIT) {
            fprintf(stderr, "pio: Input buffer exceeds EOF\n");
            pio_compress_error = TRUE;
         }
      } else {
         pio_u_char(inbuff, &temp);
         pio_compress_input_buffer = temp;
      }
      pio_compress_input_left = 8;
   }
   bit = pio_compress_input_buffer & 1;
   pio_compress_input_buffer >>= 1;
   pio_compress_input_left -= 1;

   return (bit);
}

/*
 * pio_compress_put_bit()
 *    Output a bit to the compressed stream.
 */
static inline void
pio_compress_put_bit(int bit, pio_buffer_t * outbuff)
{
   unsigned char temp;

   pio_compress_output_buffer >>= 1;
   if (bit) {
      pio_compress_output_buffer |= 0x80;
   }
   pio_compress_output_left -= 1;
   if (pio_compress_output_left == 0) {
      temp = pio_compress_output_buffer & 0xff;
      pio_u_char(outbuff, &temp);
      pio_compress_output_left = 8;
   }
}

/*
 * pio_compress_splay()
 *    Splay a symbol across the trees
 */
static inline void
pio_compress_splay(int sym)
{
   int a, b, c, d;
   pio_compress_tree *tree_ptr;

   a = sym + PIO_SUCCMAX;
   tree_ptr = pio_compress_trees[pio_compress_state];

   do {
      c = tree_ptr->up[a];
      if (c != PIO_ROOT) {
         d = tree_ptr->up[c];
         b = tree_ptr->left[d];
         if (c == b) {
            b = tree_ptr->right[d];
            tree_ptr->right[d] = a;
         } else {
            tree_ptr->left[d] = a;
         }
         if (a == tree_ptr->left[c]) {
            tree_ptr->left[c] = b;
         } else {
            tree_ptr->right[c] = b;
         }
         tree_ptr->up[a] = d;
         tree_ptr->up[b] = c;
         a = d;
      } else {
         a = c;
      }
   } while (a != PIO_ROOT);

   pio_compress_state = sym % PIO_MAXSTATE;
}

/*
 * pio_compress_initialise()
 *    Initialise the compressor state, and allocate the trees
 */
static void
pio_compress_initialise(void)
{
   int loop1, loop2, loop3;
   pio_compress_tree *tree_ptr;

   pio_compress_error = FALSE;

   /* setup input buffer */
   pio_compress_input_left = 0;
   pio_compress_input_garbage = 0;

   /* setup output buffer */
   pio_compress_output_buffer = 0;
   pio_compress_output_left = 8;

   for (loop1 = 0; loop1 < PIO_MAXSTATE; loop1++) {
      tree_ptr = (pio_compress_tree *) pio_malloc(sizeof(pio_compress_tree));
      pio_compress_trees[loop1] = tree_ptr;
      for (loop2 = 2; loop2 <= PIO_TWICEMAX; loop2++) {
         tree_ptr->up[loop2] = loop2 / 2;
      }
      for (loop3 = 1; loop3 <= PIO_MAXCHAR; loop3++) {
         tree_ptr->left[loop3] = 2 * loop3;
         tree_ptr->right[loop3] = 2 * loop3 + 1;
      }
   }

   /* initial pio_compress_state */
   pio_compress_state = 0;
}

/*
 * pio_compress_shutdown()
 *    Free up used memory in the reverse order of allocation.
 */
static void
pio_compress_shutdown(void)
{
   int loop;

   for (loop = PIO_MAXSTATE - 1; loop >= 0; loop--) {
      free(pio_compress_trees[loop]);
   }
}


/*
 * pio_compress_compress()
 *    Compress a symbol
 */
static void
pio_compress_compress(int sym, pio_buffer_t * buff)
{
   int sp, a;
   int stack[PIO_MAXCHAR];
   pio_compress_tree *tree_ptr;

   a = sym + PIO_SUCCMAX;
   sp = 0;
   tree_ptr = pio_compress_trees[pio_compress_state];
   do {
      stack[sp] = (tree_ptr->right[tree_ptr->up[a]] == a) ? 1 : 0;
      sp++;
      a = tree_ptr->up[a];
   } while (a != PIO_ROOT);
   do {
      sp--;
      pio_compress_put_bit(stack[sp], buff);
   } while (sp > 0);
   pio_compress_splay(sym);
}

/*
 * pio_compress_decompress()
 *    Decompress a symbol
 */
static int
pio_compress_decompress(pio_buffer_t * inbuff)
{
   int a, sym;
   pio_compress_tree *tree_ptr;
   a = PIO_ROOT;

   tree_ptr = pio_compress_trees[pio_compress_state];
   do {
      if (pio_compress_get_bit(inbuff) == 0) {
         a = tree_ptr->left[a];
      } else {
         a = tree_ptr->right[a];
      }
   } while (a <= PIO_MAXCHAR);
   sym = a - PIO_SUCCMAX;
   pio_compress_splay(sym);
   return (sym);
}

/*
 * pio_compress_buffer()
 *    Compress one buffer into another.
 */
int
pio_compress_buffer(pio_buffer_t * inbuff, pio_buffer_t * outbuff)
{
   unsigned char sym;

   pio_compress_initialise();

   for (;;) {
      if (pio_eof(inbuff))
         break;
      pio_u_char(inbuff, &sym);
      pio_compress_compress(sym, outbuff);
   }
   pio_compress_compress(PIO_EOF_CODE, outbuff);

   /* 
    * send out the remaining bits 
    */
   sym = pio_compress_output_buffer >> pio_compress_output_left;
   pio_u_char(outbuff, &sym); 

   /* 
    * Add a bit of packing so the pio_compress_get_bit() routine
    * works with buffers written to disk.
    */
   sym = 0;
   pio_u_char(outbuff, &sym);

   pio_compress_shutdown();
   return (0);
}

/*
 * pio_decompress_buffer()
 *    Decompress one buffer into another.
 */
int
pio_decompress_buffer(pio_buffer_t * inbuff, pio_buffer_t * outbuff)
{
   int sym;
   unsigned char temp;

   pio_compress_initialise();

   for (;;) {
      sym = pio_compress_decompress(inbuff);
      if (sym == PIO_EOF_CODE)
         break;
      temp = sym & 0xff;
      pio_u_char(outbuff, &temp);
   }
   pio_compress_shutdown();
   return (0);
}

/*
 * pio_write_compressed()
 *    Write a compressed buffer to a file.
 */
int
pio_write_compressed(pio_buffer_t * buffer, FILE * file)
{
   int ret = 0, save_type;
   unsigned long save_pos;
   pio_buffer_t *temp = pio_create_buffer(NULL, 0);

   if (temp == NULL) {
      fprintf(stderr, "pio: Unable to create tempory for compression\n");
      return (-1);
   }

   /*
    * Output the magic and the size
    */
   ret += pio_u_char(temp, &pio_magic_1);
   ret += pio_u_char(temp, &pio_magic_2);
   ret += pio_u_char(temp, &pio_magic_3);
   ret += pio_u_char(temp, &pio_magic_4);
   ret += pio_u_long(temp, &buffer->buffer_end);
   if (ret) {
      pio_destroy_buffer(temp);
      fprintf(stderr, "pio: Failed to write compressed header\n");
      return (-1);
   }

   /*
    * Compress the buffer
    */
   save_type = buffer->buffer_type;
   save_pos = buffer->buffer_pos;
   buffer->buffer_pos = 0;
   buffer->buffer_type = PIO_INPUT;
   if (pio_compress_buffer(buffer, temp)) {
      pio_destroy_buffer(temp);
      fprintf(stderr, "pio: Failed compression\n");
      return (-1);
   }
   buffer->buffer_pos = save_pos;
   buffer->buffer_type = save_type;

   /*
    * Write the new buffer to the file
    */
   if (pio_write_buffer(temp, file) != temp->buffer_end) {
      pio_destroy_buffer(temp);
      fprintf(stderr, "pio: Failed to write compressed buffer\n");
      return (-1);
   }
   pio_destroy_buffer(temp);
   return(0);
}

/*
 * pio_read_compressed()
 *    Read a compressed pio buffer into memory.
 */
pio_buffer_t *
pio_read_compressed(FILE * file)
{
   int ret = 0;
   unsigned char m1, m2, m3, m4;
   unsigned long size;
   pio_buffer_t *temp;
   pio_buffer_t *new;

   /*
    * Read in the compressed buffer, and create our temporary
    */
   temp = pio_read_buffer(file);
   new = pio_create_buffer(NULL, 0);

   if (temp == NULL || new == NULL) {
      if (temp != NULL)
         pio_destroy_buffer(temp);
      if (new != NULL)
         pio_destroy_buffer(new);
      fprintf(stderr, "pio: Unable to create buffers for decompression\n");
      return (NULL);
   }

   /*
    * Read in the magic and size
    */
   ret += pio_u_char(temp, &m1);
   ret += pio_u_char(temp, &m2);
   ret += pio_u_char(temp, &m3);
   ret += pio_u_char(temp, &m4);
   ret += pio_u_long(temp, &size);
   if (ret != 0 ||
       m1 != pio_magic_1 ||
       m2 != pio_magic_2 ||
       m3 != pio_magic_3 ||
       m4 != pio_magic_4 ||
       size == 0) {
      pio_destroy_buffer(temp);
      pio_destroy_buffer(new);
      fprintf(stderr, "pio: Bad magic or size\n");
      return (NULL);
   }

   /*
    * Decompress the buffer 
    */
   if (pio_decompress_buffer(temp, new)) {
      pio_destroy_buffer(temp);
      pio_destroy_buffer(new);
      fprintf(stderr, "pio: Failed decompression\n");
      return (NULL);
   }
   pio_destroy_buffer(temp);

   /*
    * Reset the buffer pointer and type
    */
   new->buffer_type = PIO_INPUT;
   new->buffer_pos = 0;
   return (new);
}

/*
 * pio_eof()
 *    Check whether an input buffer is at the end.
 */
int
pio_eof(pio_buffer_t * temp)
{
   return (temp->buffer_pos > temp->buffer_end);
}
