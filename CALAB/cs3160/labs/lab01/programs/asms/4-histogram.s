# Problem 4 -- histogram of marks.
#
# Each of the first `n` entries of `marks` is a mark from 0 to 10 inclusive.
# For every m in 0..10, count[m] must hold the number of marks equal to m.
#
# `count` starts out as all zeros. Entries of `marks` beyond the first `n`
# must not be counted.
#
# Hint: count[m] is at (address of count) + 4*m, since each entry is a 4-byte
# word; no comparison per bucket is needed.
#
# DO NOT rename, reorder or resize the globals below. The tests replace the
# contents of `n`, `marks` and `count` with their own data and then read
# `count` back, so `marks` must keep room for all 32 words.

.section .data
	.align 2
count:	.fill 11, 4, 0		# one bucket per mark 0..10
marks:	.word 2, 3, 0, 5, 10, 7, 1, 10, 10, 8
	.word 8, 9, 6, 7, 8, 2, 4, 5, 0, 9
	.fill 12, 4, 0		# marks holds 32 words in total
n:	.word 20

.section .text
	.globl main
main:
	# your code here

	li   x10, 0		# exit status 0
	ret			# main must return -- do not loop forever
