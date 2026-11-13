# Unconditional jumps.
#
# jal and jalr always redirect, so each costs a flush. jal also writes a return
# address: verify in the trace that the link register receives the address
# after the jump, not the jump target.

    .section ".text.init"
    .globl _start
_start:
    li   t0, 0
    nop
    nop
    jal  ra, routine         # ra = the address of the next instruction
    nop
    nop
    nop

    li   t1, 2
    nop
    nop
    j    onward              # jal zero, ...: a jump that writes nothing
    nop
    nop
    nop
onward:
    li   t2, 3
    nop
    nop
    nop

# --- exit with status 0 ----------------------------------------------------
    li   a0, 1
    nop
    nop
    lui  t5, %hi(tohost)
    nop
    nop
    sw   a0, %lo(tohost)(t5)
    nop
1:  j    1b

routine:
    addi t3, zero, 7
    nop
    nop
    jalr zero, 0(ra)         # return: ra was written well before this
    nop
    nop
    nop

# The host interface. Both symbols must exist, aligned as below: a store to
# `tohost` ends the program, and Spike, against which the reference results are
# checked, detects it only when `fromhost` is also defined. With `tohost` alone,
# Spike runs until it is killed.
    .section ".tohost","aw",@progbits
    .align 6
    .globl tohost
tohost:   .dword 0
    .align 6
    .globl fromhost
fromhost: .dword 0
