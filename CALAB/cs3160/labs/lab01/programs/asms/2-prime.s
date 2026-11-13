# Problem 2 -- primality test.
#
# Decide whether the value in `a` is a prime number.
# Store 1 in `result` if it is prime, and -1 if it is not.
#
# No value below 2 is prime, so a = 1, a = 0 and negative values all give -1.
#
# DO NOT rename or reorder the globals below. The tests replace the contents
# of `a` with their own value and then read `result`.

.section .data
	.align 2
a:	.word 10
result:	.word 0

.section .text
	.globl main
main:
	# your code here

	li   x10, 0		# exit status 0
	ret			# main must return -- do not loop forever
