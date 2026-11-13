# REFERENCE SOLUTION -- not distributed to students.
#
# int sum(int *arr, int n)
#
# A leaf function: it calls nothing, so it needs no stack frame at all. Using
# only t- and a-registers means there is nothing to save or restore.

.section .data
	.align 2
arr:	.word 4, 8, 15, 16, 23, 42
	.fill 10, 4, 0
n:	.word 6
answer:	.word 0

.section .text

	.globl sum
sum:
	li   t0, 0			# running total
1:	beqz a1, 2f			# no elements left?
	lw   t1, 0(a0)
	add  t0, t0, t1
	addi a0, a0, 4			# next element is 4 bytes along
	addi a1, a1, -1
	j    1b
2:	mv   a0, t0			# return value goes in a0
	ret

# A main of your own, so `make run` does something useful while you develop.
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
