# Problem 3 -- int matmul(int *A, int *B, int *C)
#
# Write a function `matmul` that multiplies two 3x3 matrices: C = A * B.
#
#   argument 1 (a0) : address of A
#   argument 2 (a1) : address of B
#   argument 3 (a2) : address of C, where the result goes
#   returns    (a0) : 1 if every element of C is non-zero, otherwise 0
#
# Matrices are stored row by row, so element [i][j] of a 3x3 matrix is the
# word at  base + 4*(3*i + j).
#
# Write every element of C; do not assume that it is initially zero.
#
# Three pointers and three loop counters exceed the convenient number of
# t-registers, so s-registers will be needed; they must be restored before
# returning.
#
# The tests call `matmul` directly; the main below is for development only.

.section .data
	.align 2
A:	.word 1, 2, 3,  4, 5, 6,  7, 8, 9
B:	.word 1, 0, 0,  0, 1, 0,  0, 0, 1
C:	.fill 9, 4, 0
answer:	.word 0

.section .text

	.globl matmul
matmul:
	# your code here

	ret

	.globl main
main:
	addi sp, sp, -16
	sw   ra, 12(sp)

	la   a0, A
	la   a1, B
	la   a2, C
	call matmul
	la   t0, answer
	sw   a0, 0(t0)

	lw   ra, 12(sp)
	addi sp, sp, 16
	li   a0, 0
	ret
