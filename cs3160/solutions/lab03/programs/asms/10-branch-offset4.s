# A branch whose target is the instruction after it.
#
# `beq zero, zero, .+4` is always taken, *to the fall-through address*. The
# assembler accepts it, but no compiler emits it: it is a no-op that causes a
# pipeline flush.
#
# It is the one encoding that distinguishes the simulator's two counts of
# changes of control flow:
#
#   taken_branches   inferred by stage_writeback() from next_pc != pc + 4,
#                    which this branch defeats, since its next_pc *is* pc + 4
#   pc_redirects     measured by redirects_pc() from the branch outcome, on
#                    which the run loop flushes
#
# Each of the four adds 1 to `branches`, 0 to `taken_branches`, 1 to
# `pc_redirects`, and the flush depth to `flushed_instructions`.
#
# It is also the only program in which a redirect target equals the
# fall-through address, and so the only one showing that the run loop redirects
# on the branch outcome rather than on whether the PC changes.

.section .text
	.globl main
main:
	beq	zero, zero, .+4
	beq	zero, zero, .+4
	beq	zero, zero, .+4
	beq	zero, zero, .+4

	# The not-taken counterpart, on which both counters agree: bne with two
	# zero operands is never taken, so nothing is flushed.
	bne	zero, zero, .+4
	bne	zero, zero, .+4

	li	a0, 0
	ret
