OBJS= main.obj ptes.obj seg.obj load.obj gdt.obj move.obj
CC= bcc
AS= tasm
CFLAGS= -v -g1 -ml
ASFLAGS= -mx

.c.obj:
	$(CC) $(CFLAGS) -c $*.c
.asm.obj:
	$(AS) $(ASFLAGS) $*.asm

boot: $(OBJS)
	$(CC) $(CFLAGS) -eboot.exe $(OBJS)

move.asm: move.s
	copy move.s move.asm

clean:
	rm -f *.obj boot
