# Branches, taken and not taken.
#
# With branches resolved in MEM, three instructions follow a taken branch into
# the pipeline and must be flushed. flushed_instructions in stats.json is
# therefore three per taken branch and zero per untaken one.
#
# The loop runs four times, so the backward branch is taken three times and
# not taken once.

    .section ".text.init"
    .globl _start
_start:
    li   t0, 0               # counter
    li   t1, 4               # limit
    li   t2, 0               # running total
    nop

loop:
    addi t0, t0, 1           # t0 produced 3 back on the first pass; see below
    nop
    nop
    add  t2, t2, t0
    nop
    nop
    blt  t0, t1, loop        # taken three times, falls through on the fourth
    nop
    nop
    nop

    li   t3, 9
    nop
    nop
    beq  t3, t3, always      # always taken
    nop
    nop
    nop
always:
    li   t4, 1
    nop
    nop
    bne  t4, t4, never       # never taken: no redirect, no flush
    nop
    nop
    nop
never:

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
