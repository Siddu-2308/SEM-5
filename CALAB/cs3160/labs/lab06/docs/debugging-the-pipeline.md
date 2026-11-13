# Debugging a pipeline

In a single-cycle machine an incorrect result comes from one faulty stage, and
the trace identifies it. In a pipeline the instruction producing the incorrect
result is usually not at fault: it read a register too early, or should not
have executed at all.

Examine the *cycle*, therefore, not only the instruction.

## The per-cycle dump

    printf '[Logging]\nlog_level = DEBUG\n' > debug.ini
    build/sim --proc=pipelined --config=debug.ini tests/images/p4-branch.r5ob

At `DEBUG` level the simulator prints one line per cycle giving the contents of
each pipeline register:

    cycle 6      IF/ID 80000018  ID/EX 80000014  EX/MEM 80000010  MEM/WB 8000000c
    cycle 7      IF/ID 8000001c  ID/EX 80000018  EX/MEM 80000014  MEM/WB 80000010
    cycle 8      IF/ID 8000001c  ID/EX --------  EX/MEM 80000018  MEM/WB 80000014

Each column holds an address, or `--------` for an empty stage. Read downwards:
an instruction should move one column to the right on each line.

| Observation | Meaning |
| --- | --- |
| the same address in IF/ID on two lines, and a `--------` moving right | a stall: the instruction is held in ID and a bubble proceeds |
| an address that disappears before reaching MEM/WB | a flush: an instruction ahead of it redirected the PC |
| every column `--------` after the first few cycles | nothing is fetched, or the latches are not installed |
| an address appearing twice in the same column | the PC did not advance |
| addresses advancing by 4 beyond the end of the program | the store to `tohost` was not detected |

## Interpreting a trace mismatch

The tests compare the `[OUT]` trace with the reference and report the first
difference. In a pipeline, examine the few lines preceding it:

    trace differs at retired instruction 41
          yours: [OUT] 800020a4 | next_pc = 800020a8 | x15 = 00000002 | mem[?] = ...
          ours:  [OUT] 800020a4 | next_pc = 800020a8 | x15 = 00000003 | mem[?] = ...

The address and `next_pc` agree; the value differs. The instruction followed
the correct path but computed an incorrect value, which in a pipeline almost
always indicates a stale register read: a dependency that should have stalled
and did not.

If the *addresses* diverge, an instruction that should have been flushed was
retired, or the converse. Examine the preceding branch and count the
instructions between it and the divergence.

## Three errors with plausible output

**Writing the current latch instead of the next.** Several stages of one
instruction then execute in a single cycle: the program is *correct* but far
too fast. A cycle count close to the instruction count indicates this error.

**Counting a flushed bubble as a flushed instruction.** `flushed` then exceeds
the number of instructions fetched. Count only latches that held an
instruction.

**Fetching during a stall.** The instruction count is unaffected, since the
duplicate is flushed, but `fetches` increases and, once a cache is present,
every cycle count is slightly too high.

## The cycle identity

With an ideal memory:

    cycles = instructions + 4 + stall_cycles + flushed_instructions

The `+ 4` is the time to fill the pipeline. If the identity does not hold, one
of its terms is counted in the wrong place, which is far easier to locate than
a trace mismatch.
