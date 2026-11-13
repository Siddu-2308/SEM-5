// The cost of stalls, measured by running the pipeline.
//
// test_hazard.cpp checks that dependencies are detected. These tests check the
// consequences: the instruction waits in ID, a bubble proceeds behind it, and
// the cycle count increases by exactly the expected amount.

#include "check.h"

#include "bench.h"

TEST(spaced_code_still_costs_nothing) {
    // No dependency closer than three instructions, as in the programs of the
    // previous lab. Hazard detection must not slow them down.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), NOP(), NOP(),
            ADDI(11, 10, 1), NOP(), NOP(),
            HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(11), 2u);
    CHECK_EQ(b.st.stall_cycles, 0L);
    CHECK_EQ(b.st.cycles, b.st.instructions + 4);

    // Without the spacing the same two instructions stall, so the zero above
    // is measured rather than assumed.
    Bench c;
    LOAD(c, ADDI(10, 0, 1), ADDI(11, 10, 1), NOP(), NOP(), HALT());
    run_pipelined(c.cpu);
    CHECK(c.st.stall_cycles > 0);
}

TEST(a_back_to_back_dependency_costs_two_cycles) {
    // The producer is in EX when the consumer reaches ID, and the consumer
    // waits until the producer is in WB: two cycles.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), ADDI(11, 10, 1), NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(11), 2u);
    CHECK_EQ(b.st.stall_cycles, 2L);
    CHECK_EQ(b.st.cycles, b.st.instructions + 4 + 2);
}

TEST(one_instruction_of_separation_costs_one_cycle) {
    Bench b;
    LOAD(b, ADDI(10, 0, 1), NOP(), ADDI(11, 10, 1), NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(11), 2u);
    CHECK_EQ(b.st.stall_cycles, 1L);
}

TEST(two_instructions_of_separation_cost_nothing) {
    // The producer is in WB when the consumer reads the register file, and WB
    // writes before ID reads. This boundary is why the previous lab's programs
    // use exactly this spacing.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), NOP(), NOP(), ADDI(11, 10, 1), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(11), 2u);
    CHECK_EQ(b.st.stall_cycles, 0L);

    // One nop fewer costs a cycle: two is the boundary.
    Bench c;
    LOAD(c, ADDI(10, 0, 1), NOP(), ADDI(11, 10, 1), NOP(), NOP(), HALT());
    run_pipelined(c.cpu);
    CHECK_EQ(c.st.stall_cycles, 1L);
}

TEST(stalling_produces_the_right_answer_not_just_the_right_count) {
    // A chain without spacing: each step depends on its predecessor and
    // stalls, and the final value is correct only if each waited long enough.
    Bench b;
    LOAD(b, ADDI(10, 0, 1),
            ADDI(10, 10, 1),
            ADDI(10, 10, 1),
            ADDI(10, 10, 1),
            NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(10), 4u);
    CHECK_EQ(b.st.stall_cycles, 6L);   // three dependencies, two cycles each
}

TEST(a_load_use_dependency_stalls_like_any_other) {
    // Without forwarding a load is not a special case: it writes a register,
    // and any reader waits.
    Bench b;
    b.cpu.regs[11] = DATA;
    b.cpu.regs[12] = 0x99u;
    LOAD(b, SW(12, 11, 0), NOP(), NOP(),
            LW(13, 11, 0),
            ADD(14, 13, 13),          // uses the loaded value immediately
            NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(13), 0x99u);
    CHECK_EQ(b.reg(14), 0x132u);
    CHECK_EQ(b.st.stall_cycles, 2L);
}

TEST(a_branch_that_depends_on_the_instruction_in_front_of_it_stalls) {
    // The branch reads a register the preceding instruction is still
    // computing, so it waits in ID.
    Bench b;
    LOAD(b, ADDI(10, 0, 0),
            BEQ(10, 0, 16),           // taken, once x10 is known to be 0
            ADDI(11, 0, 99),
            ADDI(11, 0, 99),
            ADDI(11, 0, 99),
            ADDI(11, 0, 7),
            HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(11), 7u);
    CHECK_EQ(b.st.stall_cycles, 2L);
    CHECK_EQ(b.st.flushed, 3L);
}

TEST(a_bubble_sent_by_a_stall_is_not_counted_as_flushed) {
    // A stall inserts a bubble into ID/EX. If a branch redirects a cycle or two
    // later, that bubble is among the latches flushed; it discards nothing and
    // must not be counted in flushed.
    Bench b;
    LOAD(b, ADDI(10, 0, 0),
            BEQ(10, 0, 8),            // stalls, then redirects over one
            ADDI(11, 0, 99),
            ADDI(11, 0, 7),
            NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK(b.st.stall_cycles > 0);        // the branch did wait
    CHECK(b.st.flushed <= b.st.fetches);
    CHECK_EQ(b.reg(11), 7u);
}

TEST(the_cycle_identity_still_balances) {
    // cycles = instructions + 4 + stalls + flushed, with an ideal memory. A
    // stall counted in the wrong place violates the identity even when each
    // figure appears plausible.
    Bench b;
    LOAD(b, ADDI(10, 0, 1),
            ADDI(10, 10, 1),
            BEQ(10, 10, 12),
            ADDI(11, 0, 99),
            ADDI(11, 0, 99),
            ADDI(11, 0, 3),
            NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    // Both terms must be non-zero, or the identity holds trivially.
    CHECK(b.st.stall_cycles > 0);
    CHECK(b.st.flushed > 0);
    CHECK_EQ(b.st.cycles,
             b.st.instructions + 4 + b.st.stall_cycles + b.st.flushed);
}
