	.text
	.globl _modf
_modf:
        pushl %ebp
        movl %esp,%ebp
        subl $16,%esp
        fnstcw -12(%ebp)
        movw -12(%ebp),%dx
        orw $3072,%dx
        movw %dx,-16(%ebp)
        fldcw -16(%ebp)
        fldl 8(%ebp)
        frndint
        fstpl -8(%ebp)
        fldcw -12(%ebp)
        movl 16(%ebp),%eax
        movl -8(%ebp),%edx
        movl -4(%ebp),%ecx
        movl %edx,(%eax)
        movl %ecx,4(%eax)
        fldl 8(%ebp)
        fsubl -8(%ebp)
        jmp 1f
1:
        leave
        ret
