;
; cputype.asm
;	Routine for determining CPU type
;
; I got this via djgpp, apparently it's originally in the i486
; manual.  I don't have one of those, so I have to trust djgpp
; for the lineage.
;

;
; Prologue
;
	DOSSEG
	.MODEL  large
	.386p

	.CODE
	
	PUBLIC	_cputype	; from Intel 80486 reference manual
_cputype PROC
	pushf
	pop	bx
	and	bx,0fffh
	push	bx
	popf
	pushf
	pop	ax
	and	ax,0f000h
	cmp	ax,0f000h
	jz	bad_cpu
	or	bx,0f000h
	push	bx
	popf
	pushf
	pop	ax
	and	ax,0f000h
	jz	bad_cpu

	smsw	ax
	test	ax,1
	jnz	bad_mode
	mov	ax,0
	ret

bad_mode:
	mov	ax,2
	ret

bad_cpu:
	mov	ax,1
	ret

_cputype ENDP

	END
