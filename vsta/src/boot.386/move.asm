;
; move.asm
;	Move a block of memory and jump to it
;
; Tricky, since this code will get tromped unless it is moved
; up out of the way.  This then requires some care since the code
; isn't actually running at the address for which it was assembled.
;
; Once running, this code pulls what it needs from the rest of the
; DOS boot loader, then disables interrupt and bulk-copies the
; stuff down to a base address of 0 (actually 4K, since 32-bit tasks
; are generated with an invalid addr 0).  Finally, it launches the
; 32-bit code through the boot 32-bit task gate.
;
; This isn't easy, but that's why we get the big bucks, right? :-)
;

;
; Prologue
;
	DOSSEG
	.MODEL  large
	.386p

;
; Constants
;
SEGMUL=8		; Shift for segment index (multiply)
GDT_KDATA=(1 * SEGMUL)	;  ...kernel data segment
GDT_BOOT32=(3 * SEGMUL)	;  ...32-bit task segment
GDT_TMPTSS=(4 * SEGMUL)	;  ...temptask segment
CR_PROT=1		; CR0 bit to turn on protected mode
CR_PG=80000000h		;  ...to turn on paging

;
; Data
;
	.DATA
	EXTRN	_gdtdesc : FWORD
	EXTRN	_cr3 : DWORD
	EXTRN	_stackseg : WORD
	EXTRN	_basemem : DWORD
	EXTRN	_pbase : DWORD

;
; Our routine.  NB, it's not running where it was assembled.
;
	.CODE
	PUBLIC  _move_jump
_move_jump	PROC

	;
	; Can point GDT to our buffer now, since it isn't used until
	; we go into protected mode.
	;
	mov	ax,SEG _gdtdesc
	mov	ds,ax
	lgdt	_gdtdesc

	;
	; Also take care of CR3 now
	;
	mov	ax,SEG _cr3
	mov	ds,ax
	mov	eax,_cr3
	mov	cr3,eax

	;
	; We're not ready for interrupts yet, so mask them.
	;
	cli

	;
	; Move to our high stack
	;
	mov	ax,SEG _stackseg
	mov	ds,ax
	mov	ax,_stackseg
	mov	ss,ax
	mov	sp,1000h

	;
	; Load what we'll need into registers
	;	edx - address loaded at
	;	ecx - length in bytes
	;
	mov	ax,SEG _basemem
	mov	ds,ax
	mov	edx,_basemem
	mov	ax,SEG _pbase
	mov	ds,ax
	mov	ecx,_pbase
	sub	ecx,edx

	;
	; Skip first page, which is intended for the null-dereference
	; catching page.
	;
	sub	ecx,1000h
	add	edx,1000h

	;
	; Copy memory image down to 0 base.  Screwball because we're
	; copying more than 64K, so we do paragraphs at a time and
	; advance the segment value.  Sigh.
	;
	shr	edx,4		; Convert pointer to segment
	mov	es,dx
	mov	ax,100h		; Page 0 + one page (null deref)
	mov	ds,ax
	xor	bx,bx		; Constant 0 pointer
l5:	mov	eax,es:0(bx)	; Move a paragraph
	mov	ds:0(bx),eax
	mov	eax,es:4(bx)
	mov	ds:4(bx),eax
	mov	eax,es:8(bx)
	mov	ds:8(bx),eax
	mov	eax,es:12(bx)
	mov	ds:12(bx),eax
	sub	ecx,16		; Decrement count by paragraph
	jle	short l6
	mov	ax,ds		; Advance DS and ES
	inc	ax
	mov	ds,ax
	mov	ax,es
	inc	ax
	mov	es,ax
	jmp	short l5
l6:

	;
	; Switch to paging, protected mode.
	;
	mov	eax,cr0
	or	eax,(CR_PROT or CR_PG)
	mov	cr0,eax
	jmp	short l1
l1:	nop			; Now in protected mode

	;
	; Set "current task"
	;
	mov	ax,GDT_TMPTSS
	nop
	ltr	ax
	jmp	short l4
l4:	nop

	;
	; Jump into our 32-bit task
	;
	db	0EAh		; 16-bit long jump, off 0, seg BOOT32
	dw	0
	dw	GDT_BOOT32

	;
	; No return
	;
l2:	jmp	l2

	;
	; Record size of this routine
	PUBLIC	_move_jump_len
_move_jump_len=$-_move_jump

_move_jump	ENDP

;
; This is needed because Borland C is too hoity-toity to let
; me cast a pointer to a function pointer without bitching.
;
	.DATA
	EXTRN	_move_jump_hi : DWORD
	.CODE
	PUBLIC  _run_move_jump
_run_move_jump	PROC
	jmp	[_move_jump_hi]
_run_move_jump	ENDP

	END
