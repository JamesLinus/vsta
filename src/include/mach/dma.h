#ifndef _DMA_H
#define _DMA_H
/*
 * Definitions for PC DMA controller
 */
#define DMA_LOW 0x4

#define DMA_ADDR 0x4
#define DMA_CNT 0x5
#define DMA_INIT 0xA
#define DMA_STAT0 0xB
#define DMA_STAT1 0xC
#define DMA_HIADDR 0x81

#define DMA_HIGH 0x81

/*
 * Operation masks for STAT registers
 */
#define DMA_READ 0x46
#define DMA_WRITE 0x4A

#endif /* _DMA_H */
