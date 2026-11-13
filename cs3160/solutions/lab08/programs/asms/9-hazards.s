# Hazard-dense code, for exercising the pipeline.
#
# Typical code incidentally places dependent instructions a few apart. This
# program deliberately does the opposite, so that every path through the
# hazard and forwarding logic is exercised: forwarding from MEM and from WB, the
# load-use stall, and a branch depending on the immediately preceding
# instruction.
#
# No value is checked; the program provides coverage. Run it under
# --proc=pipelined and --proc=forwarding and compare the cycle counts.

.section .data
	.align 2
values:
	.word 1, 2, 3, 4, 5, 6, 7, 8
target:
	.word 0
sink:
	.space 64

.section .text
	.globl main
main:
	addi	sp, sp, -16
	sw	ra, 12(sp)

	# --- Back-to-back dependencies: every instruction needs the last one ---
	# Without forwarding each stalls twice; with forwarding, not at all. Most
	# of the difference between the two models arises here.
	li	a0, 1
	addi	a1, a0, 1
	addi	a2, a1, 1
	addi	a3, a2, 1
	addi	a4, a3, 1
	add	a5, a4, a4
	sub	a6, a5, a1
	xor	a7, a6, a5
	or	t1, a7, a6
	and	t2, t1, a7

	# --- A dependency two instructions back, and three back ---
	# Two back still stalls without forwarding; three back is satisfied because
	# the register file is written and read in the same cycle.
	li	t3, 10
	nop
	addi	t4, t3, 1
	li	t5, 20
	nop
	nop
	addi	t6, t5, 1

	# --- Both operands from the same producer ---
	li	a0, 7
	add	a1, a0, a0

	# --- Load-use: the one dependency forwarding cannot remove ---
	la	t0, values
	lw	a2, 0(t0)
	addi	a3, a2, 1		# needs the loaded value immediately
	lw	a4, 4(t0)
	add	a5, a4, a4		# both operands from the load
	lw	a6, 8(t0)
	nop
	addi	a7, a6, 1		# one instruction of slack: no stall

	# --- A store whose value is still in flight ---
	# A store's data comes from rs2, so it must be forwarded to the memory
	# stage, not only to the ALU.
	la	t1, sink
	addi	a0, a0, 5
	sw	a0, 0(t1)		# rs2 produced by the instruction in front
	lw	a1, 0(t1)
	sw	a1, 4(t1)		# rs2 produced by a load in front

	# --- A store whose *address* is still in flight ---
	addi	t2, t1, 8
	sw	a0, 0(t2)

	# --- Branches that depend on the instruction in front of them ---
	# When branches resolve in ID these are costly: the operand is not ready
	# that early, even with forwarding.
	li	a0, 5
	li	a1, 5
	beq	a0, a1, 1f		# both operands one and two instructions back
	nop
1:
	addi	a2, a0, 1
	bne	a2, a0, 2f		# operand produced directly in front
	nop
2:
	lw	a3, 0(t0)
	beq	a3, a3, 3f		# operand from a load directly in front
	nop
3:
	# An untaken branch costs nothing: no flush.
	li	a4, 1
	li	a5, 2
	beq	a4, a5, 4f
	addi	a6, a4, 1		# reached
	beq	a4, a5, 4f
	addi	a7, a4, 2		# reached
4:

	# --- A tight backward loop: a taken branch every three instructions ---
	li	t3, 8
	la	t4, values
	li	t5, 0
loop:
	lw	t6, 0(t4)
	add	t5, t5, t6		# load-use, every iteration
	addi	t4, t4, 4
	addi	t3, t3, -1
	bne	t3, zero, loop		# taken seven times, then not
	la	t0, sink
	sw	t5, 16(t0)		# 36, if the arithmetic worked

	# --- A jump through a register that was just loaded ---
	# jalr needs its operand in ID in the forwarding model, so a loaded
	# target is the worst case: a load-use stall and then a redirect.
	la	t1, after
	la	t2, target
	sw	t1, 0(t2)
	lw	t3, 0(t2)
	jalr	t4, 0(t3)		# target straight out of memory
	li	a0, -1			# skipped
after:
	# --- A jump whose target is known, right after a taken branch ---
	li	a0, 0
	beq	a0, a0, 5f
	li	a0, -1			# skipped
5:
	jal	ra, subroutine
	jal	ra, subroutine		# two calls in a row: return address reused
	jal	ra, empty_subroutine	# returns before the jal has left the pipeline

	lw	ra, 12(sp)
	addi	sp, sp, 16
	li	a0, 0
	ret

	# A subroutine short enough that its return is a control hazard almost
	# immediately after its call.
subroutine:
	addi	a0, a0, 1
	ret

	# One instruction shorter still, which is the case that matters: the ret
	# reads ra in ID while the jal that wrote it is only as far as MEM. A
	# forwarding path that hands over a jump's ALU result rather than its
	# return address sends this back to itself instead of to the caller.
empty_subroutine:
	ret
