# Straight-line arithmetic, free of hazards.
#
# No branches and no memory accesses: the smallest program that exercises
# every stage from IF to WB, and drains the pipeline at the end.
#
# Every result is used at least three instructions after it is produced.
# Without forwarding, an instruction waits in ID until its operands have been
# written back, so a producer one or two instructions ahead would cause a
# stall. A producer three ahead is in WB, which writes the register file in the
# first half of the cycle while ID reads it in the second, so it costs
# nothing.

    .section ".text.init"
    .globl _start
_start:
    li   t0, 5
    li   t1, 7
    li   t2, 11
    li   t3, 3
    nop

    add  t4, t0, t1          # t1 produced 4 back
    sub  t5, t2, t3          # t3 produced 3 back
    xor  t6, t0, t1
    and  a1, t2, t3
    or   a2, t4, t5          # t5 produced 3 back
    sll  a3, t6, t3
    srl  a4, a1, t3
    add  a6, t0, t2
    add  a5, a2, a3
    mul  s2, t0, t1
    nop
    slt  a7, a4, a6          # a6 produced 4 back
    nop
    nop
    add  s3, a5, a7          # a7 produced 3 back
    nop
    nop
    add  s4, s2, s3

# --- exit with status 0 ----------------------------------------------------
    li   a0, 1               # (status << 1) | 1, with status 0
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
