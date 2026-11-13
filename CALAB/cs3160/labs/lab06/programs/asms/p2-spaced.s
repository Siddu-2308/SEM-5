# A chain of dependent instructions, spaced by hand.
#
# Each instruction depends on the one three before it, the closest a
# dependency can be without stalling a pipeline that has no forwarding. The
# nops are what a compiler would have to insert for this machine when it has no
# useful instruction to place there.
#
# Hazard detection makes the nops unnecessary, and forwarding removes most of
# the cost of omitting them. This program, which needs neither, takes the same
# number of cycles in the stalling and the forwarding pipeline.

    .section ".text.init"
    .globl _start
_start:
    li   t0, 1

    nop
    nop
    addi t1, t0, 1           # t0 produced 3 back
    nop
    nop
    addi t2, t1, 1
    nop
    nop
    addi t3, t2, 1
    nop
    nop
    addi t4, t3, 1
    nop
    nop
    addi t5, t4, 1
    nop
    nop
    add  t6, t5, t0

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
