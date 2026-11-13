# REFERENCE SOLUTION -- not distributed to students.
#
# int fact(int n)   -- recursive, so this one genuinely needs a stack frame.
#
# Two things must survive the recursive call: the return address in ra, which
# the call itself overwrites, and our copy of n, which we need again after the
# call returns. s0 is used for n precisely because it is callee-saved -- but
# that means we must save the caller's s0 before using it.

.section .data
	.align 2
n:	.word 6
answer:	.word 0

.section .text

	.globl fact
fact:
	addi sp, sp, -16		# frame: ra, s0, and keep 16-byte alignment
	sw   ra, 12(sp)
	sw   s0, 8(sp)

	li   t0, 2
	blt  a0, t0, base		# fact(0) = fact(1) = 1

	mv   s0, a0			# remember n across the call
	addi a0, a0, -1
	call fact			# a0 = fact(n-1)
	mul  a0, a0, s0			# a0 = n * fact(n-1)
	j    done

base:
	li   a0, 1

done:
	lw   s0, 8(sp)			# put the caller's s0 back
	lw   ra, 12(sp)
	addi sp, sp, 16
	ret

	.globl main
main:
	addi sp, sp, -16
	sw   ra, 12(sp)

	lw   a0, n
	call fact
	la   t0, answer
	sw   a0, 0(t0)

	lw   ra, 12(sp)
	addi sp, sp, 16
	li   a0, 0
	ret
