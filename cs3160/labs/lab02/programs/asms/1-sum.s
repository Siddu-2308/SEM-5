# Problem 1 -- int sum(int *arr, int n)
#
# Write a function `sum` that returns the sum of the first n entries of arr.
#
#   argument 1 (a0) : the address of the array
#   argument 2 (a1) : how many entries to add
#   returns    (a0) : the total; 0 if n is 0
#
# `sum` calls no function, so it is a LEAF function: using only t- and
# a-registers, it needs no stack frame. Any of s0-s11 used must be saved and
# restored.
#
# The tests call `sum` directly with their own arguments and ignore the main
# below, which exists so that `make run` can be used during development.

.section .data
	.align 2
arr:	.word 4, 8, 15, 16, 23, 42
	.fill 10, 4, 0
n:	.word 6
answer:	.word 0

.section .text

	.globl sum
sum:
	# your code here

	ret

	.globl main
main:
	addi sp, sp, -16
	sw   ra, 12(sp)

	la   a0, arr
	lw   a1, n
	call sum
	la   t0, answer
	sw   a0, 0(t0)

	lw   ra, 12(sp)
	addi sp, sp, 16
	li   a0, 0
	ret
