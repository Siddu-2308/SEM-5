# REFERENCE SOLUTION -- not distributed to students.
# Sort the first n entries of a into descending order, in place.
# Bubble sort: simple, and the swap makes the load/store pattern obvious.

.section .data
	.align 2
a:	.word 70, 80, 40, 20, 10, 30, 50, 60
	.fill 8, 4, 0		# a must hold 16 words in total
n:	.word 8

.section .text
	.globl main
main:
	lw   x5, n
	li   x6, 1
	ble  x5, x6, done	# 0 or 1 elements: already sorted
	addi x7, x5, -1		# x7 = passes remaining

outer:
	beqz x7, done
	la   x28, a		# x28 = cursor
	mv   x29, x7		# x29 = comparisons left in this pass

inner:
	beqz x29, outer_next
	lw   x30, 0(x28)
	lw   x31, 4(x28)
	bge  x30, x31, no_swap	# descending: leave x30 >= x31 alone
	sw   x31, 0(x28)
	sw   x30, 4(x28)
no_swap:
	addi x28, x28, 4
	addi x29, x29, -1
	j    inner

outer_next:
	addi x7, x7, -1
	j    outer

done:
	li   x10, 0
	ret
