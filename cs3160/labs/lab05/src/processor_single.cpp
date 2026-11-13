// The single-cycle model: each iteration executes one instruction completely.
//
// Cycles: one per instruction, plus memory stall time. The fetch and the data
// access occur in sequence within the same cycle, so their latencies add. A
// latency of n means the access occupies n cycles, including the cycle the
// instruction takes in any case.

#include "processor.h"

#include "log.h"

void run_single(Processor &cpu) {
    // TODO(lab 5): the run loop.
    //
    // Each iteration executes one instruction through the stages declared
    // in processor.h, in order:
    //
    //   fetch -> decode -> operands -> execute -> next_pc -> memory ->
    //   writeback
    //
    // then sets the PC to the value from next_pc_of(). Stop when cpu.halted
    // is set, and bound the loop by cfg.num_insts so that a program that
    // never terminates stops after a million instructions.
    //
    //   - An instruction that does not decode is an error in the program:
    //     call processor_illegal().
    //   - Cycles: one per instruction plus memory stall time, given by
    //     cycle_cost_serial(fetch latency, data latency); every cycle beyond
    //     the first is memory stall.
    //   - Redirects: count in cpu.stats->pc_redirects each instruction for
    //     which redirects_pc() (in processor.cpp) is true. This differs
    //     from taken_branches; the handout explains why.
    //
    // Finally, call processor_dump(), which logs the final register state at
    // DEBUG level.
    const Config &cfg = *cpu.cfg;
    (void)cfg;
    processor_dump(cpu);
}
