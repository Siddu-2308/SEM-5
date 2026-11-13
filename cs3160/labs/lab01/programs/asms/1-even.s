# Problem 1 -- count the non-negative even numbers.
#
# Count how many of the first `n` entries of the array `l` are both
# non-negative (>= 0) and even. Store that count in `result`.
#
# Note that 0 is non-negative and even.
#
# DO NOT rename, reorder or resize the globals below. The tests replace the
# contents of `n` and `l` with their own data and then read `result`, so `l`
# must keep room for all 16 words.

.section .data
	.align 2
n:	.word 5
l:	.word 2, -1, 7, 5, 3
	.fill 11, 4, 0		# l holds 16 words in total
result:	.word 0

.section .text
	.globl main
main:
	# your code here

	li   x10, 0		# exit status 0
	ret			# main must return -- do not loop forever
