# Problem 2 -- int fact(int n)
#
# Write a RECURSIVE function `fact` that returns n factorial.
#
#   argument 1 (a0) : n
#   returns    (a0) : n!   (fact(0) and fact(1) are both 1)
#
# `fact` calls itself, so it is NOT a leaf function. Two values must survive
# the recursive call:
#
#   ra  -- overwritten by `call`. If it is not saved, the outer returns jump
#          to the wrong address and the program crashes.
#   n   -- needed after the call returns, for the multiplication.
#
# Both must therefore be saved in a stack frame. Keep sp a multiple of 16.
#
# The tests call `fact` directly; the main below is for development only.

.section .data
	.align 2
n:	.word 6
answer:	.word 0

.section .text

	.globl fact
fact:
	# your code here

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
