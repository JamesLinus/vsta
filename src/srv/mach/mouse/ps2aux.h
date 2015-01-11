/* 
 * ps2aux.h
 *    Interrupt driven driver for the ps/2 aux type mouse.
 *
 * Copyright (C) 1994 by David J. Hudson, all rights reserved.
 *
 */
#ifndef __MOUSE_PS2AUX_H__
#define __MOUSE_PS2AUX_H__

/*
 * Aux device controller ports/details
 */
#define PS2AUX_IRQ 12
#define PS2AUX_BUFFER_SIZE	16
#define PS2AUX_MAX_RETRIES	120

#define PS2AUX_INPUT_PORT 0x60
#define PS2AUX_OUTPUT_PORT 0x60
#define PS2AUX_STATUS_PORT 0x64
#define PS2AUX_COMMAND_PORT 0x64

#define PS2AUX_LOW_PORT PS2AUX_INPUT_PORT
#define PS2AUX_HIGH_PORT PS2AUX_COMMAND_PORT

/*
 * Aux controller status details
 */
#define PS2AUX_OUTBUF_FULL 0x21
#define PS2AUX_INBUF_FULL 0x02

/*
 * Aux controller commands
 */
#define PS2AUX_COMMAND_WRITE 0x60
#define PS2AUX_WRITE_MAGIC 0xd4
#define PS2AUX_ENABLE_INTERRUPTS 0x47
#define PS2AUX_DISABLE_INTERRUPTS 0x65
#define PS2AUX_ENABLE_CONTROLLER 0xa7
#define PS2AUX_DISABLE_CONTROLLER 0xa8

/*
 * Device commands/responses
 */
#define PS2AUX_SET_RESOLUTION 0xe8
#define PS2AUX_SET_SCALE1 0xe6
#define PS2AUX_SET_SCALE2 0xe7
#define PS2AUX_GET_SCALE 0xe9
#define PS2AUX_SET_STREAM 0xea
#define PS2AUX_SET_SAMPLE 0xf3
#define PS2AUX_ENABLE_DEVICE 0xf4
#define PS2AUX_DISABLE_DEVICE 0xf5
#define PS2AUX_RESET 0xff
#define PS2AUX_BAT 0xaa
#define PS2AUX_ACK 0xfa

/*
 * Bits and things!
 */
#define PS2AUX_BUTTON_MASK 0x07
#define PS2AUX_Y_OVERFLOW 0x80
#define PS2AUX_X_OVERFLOW 0x40
#define PS2AUX_Y_SIGN 0x20
#define PS2AUX_X_SIGN 0x10

/*
 * Function prototypes
 */
extern int ps2aux_initialise(int argc, char **argv);
extern void ps2aux_interrupt(void),
	ps2aux_coordinates(ushort, ushort),
	ps2aux_bounds(ushort, ushort, ushort, ushort);
 
#endif /* __MOUSE_PS2AUX_H__ */
