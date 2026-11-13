# data_vars.s -- a worked example: read it, build it and run it.
#
# Declares variables and an array in the .data section, and loads their
# addresses and values into registers.
#
# 1. Registers. x5, x6 and x7 hold addresses and x11..x14 values; all may be
#    overwritten. x1, x2, x3 and x4 may NOT: they hold the return address, the
#    stack pointer and two pointers set by the start-up code. If x1 is
#    overwritten, `ret` jumps to that value instead of returning. See
#    docs/registers.md.
#
# 2. Termination. main ends with `ret`, as every program in this course must.
#    Returning passes control to the start-up code, which halts the machine. A
#    program that loops forever never halts, and the tests cannot read its
#    results.

.section .data
	.align 2
myvar1:
	.word 0xc001

myvar2:
	.word 0xc0de

myarr:
	.word 0xfeed
	.word 0xdeed
	.word 0xdeaf
	.word 0xd00d

.section .text
	.globl main
main:
	la   x5, myvar1		# x5 = address of myvar1
	lw   x11, 0(x5)		# x11 = contents of myvar1

	la   x6, myvar2
	lw   x12, 0(x6)

	la   x7, myarr		# x7 = address of the first element
	lw   x13, 0(x7)		# x13 = myarr[0]
	lw   x14, 4(x7)		# x14 = myarr[1]  (words are 4 bytes apart)

	li   x10, 0		# exit status 0
	ret
