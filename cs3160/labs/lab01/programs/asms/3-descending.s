# Problem 3 -- sort an array into descending order.
#
# Sort the first `n` entries of `a` from largest to smallest, IN PLACE: the
# result is the array itself. Entries beyond the first `n` must be unchanged.
#
# Any sorting algorithm is acceptable. The tests include n = 0, n = 1, repeated
# values and negative values.
#
# DO NOT rename, reorder or resize the globals below. The tests replace the
# contents of `n` and `a` with their own data and then read `a` back, so `a`
# must keep room for all 16 words.

.section .data
	.align 2
a:	.word 70, 80, 40, 20, 10, 30, 50, 60
	.fill 8, 4, 0		# a holds 16 words in total
n:	.word 8

.section .text
	.globl main
main:
	# your code here

	li   x10, 0		# exit status 0
	ret			# main must return -- do not loop forever
