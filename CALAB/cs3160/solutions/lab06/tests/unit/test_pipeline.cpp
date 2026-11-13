// The pipeline run loop, and the two control-hazard predicates it uses.
//
// Each test runs a program of a few instructions and checks the result. The
// tests follow the order in which the handout builds the loop; an early failure
// usually explains later ones.
//
// No program here has a dependency closer than three instructions, since this
// pipeline has no hazard detection.

#include "check.h"

#include "bench.h"
#include "hazard.h"

// --- the loop turns over ---------------------------------------------------

TEST(one_instruction_retires_and_writes_its_register) {
    Bench b;
    LOAD(b, ADDI(10, 0, 7), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.st.instructions, 2L);
    CHECK_EQ(b.reg(10), 7u);
    CHECK(b.cpu.halted);
}

TEST(filling_the_pipeline_costs_four_cycles) {
    // The basis of the timing model: with an ideal memory, no stalls and no
    // flushes, a five-stage pipeline takes one cycle per instruction plus four
    // to fill.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), ADDI(11, 0, 2), ADDI(12, 0, 3), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.st.instructions, 4L);
    CHECK_EQ(b.st.stall_cycles, 0L);
    CHECK_EQ(b.st.flushed, 0L);
    CHECK_EQ(b.st.cycles, b.st.instructions + 4);
}

TEST(instructions_retire_in_program_order) {
    // Three spaced writes to the same register. The final value depends only on
    // retirement order, which detects a stage reading a next_* latch instead of
    // the current latch in front of it.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), NOP(), NOP(),
            ADDI(10, 0, 2), NOP(), NOP(),
            ADDI(10, 0, 3), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(10), 3u);
    CHECK_EQ(b.st.instructions, 8L);
}

TEST(every_stage_is_reached) {
    // The result requires that ID read the operand, EX computed it and WB
    // wrote it.
    Bench b;
    b.cpu.regs[11] = 20;
    b.cpu.regs[12] = 8;
    LOAD(b, ADD(10, 11, 12), NOP(), NOP(), SUB(13, 11, 12), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(10), 28u);
    CHECK_EQ(b.reg(13), 12u);
}

// --- the memory stage ------------------------------------------------------

TEST(a_store_then_a_load_goes_through_memory) {
    Bench b;
    b.cpu.regs[11] = DATA;
    b.cpu.regs[12] = 0xABCDu;
    LOAD(b, SW(12, 11, 0), NOP(), NOP(), LW(13, 11, 0), NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(ram_read(b.ram, DATA, 4), 0xABCDu);
    CHECK_EQ(b.reg(13), 0xABCDu);
    CHECK_EQ(b.st.loads, 1L);
    // Two stores: the one above, and the store to tohost that ends the program.
    CHECK_EQ(b.st.stores, 2L);
}

TEST(the_program_stops_on_the_store_to_tohost) {
    // Nothing after HALT may retire. Otherwise the loop has not detected the
    // store, and would run a real program to the instruction budget.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), HALT(), ADDI(10, 0, 99), ADDI(10, 0, 99));
    run_pipelined(b.cpu);

    CHECK(b.cpu.halted);
    CHECK_EQ(b.reg(10), 1u);
}

TEST(the_pipeline_stops_fetching_once_the_program_halts) {
    // The store to tohost sets `halted` in MEM, by which time two further
    // instructions have been fetched. They never retire; the loop ends with
    // them still in the pipeline.
    //
    // No fetch may follow. The program is followed by zeroes, so a loop that
    // continued fetching would decode one as an illegal instruction and stop
    // with an error instead of an exit status.
    Bench b;
    LOAD(b, ADDI(10, 0, 1), ADDI(11, 0, 2), ADDI(12, 0, 3), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.st.instructions, 4L);
    CHECK_EQ(b.st.fetches, 6L);
    CHECK(b.cpu.halted);
}

// --- control hazards -------------------------------------------------------

TEST(a_taken_branch_redirects_and_flushes_three) {
    // Branches resolve in MEM by default, so three wrong-path instructions
    // follow a taken branch.
    Bench b;
    LOAD(b, BEQ(0, 0, 16),          // always taken, over the next three
            ADDI(10, 0, 99),        // wrong path
            ADDI(10, 0, 99),        // wrong path
            ADDI(10, 0, 99),        // wrong path
            ADDI(10, 0, 5),         // the target
            HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(10), 5u);        // none of the wrong path retired
    CHECK_EQ(b.st.pc_redirects, 1L);
    CHECK_EQ(b.st.flushed, 3L);
    CHECK_EQ(b.st.instructions, 3L);  // the branch, the target, the halt
}

TEST(an_untaken_branch_costs_nothing) {
    // x11 holds 1, so `beq x11, x0` is not taken. Nothing is redirected or
    // flushed, and the cycle count is ideal.
    Bench b;
    b.cpu.regs[11] = 1;
    LOAD(b, BEQ(11, 0, 16),
            ADDI(10, 0, 5),
            NOP(), NOP(), HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(10), 5u);
    CHECK_EQ(b.st.pc_redirects, 0L);
    CHECK_EQ(b.st.flushed, 0L);
    CHECK_EQ(b.st.cycles, b.st.instructions + 4);
}

TEST(a_jump_always_redirects) {
    Bench b;
    LOAD(b, JAL(1, 16),             // over the next three
            ADDI(10, 0, 99),
            ADDI(10, 0, 99),
            ADDI(10, 0, 99),
            ADDI(10, 0, 5),
            HALT());
    run_pipelined(b.cpu);

    CHECK_EQ(b.reg(10), 5u);
    CHECK_EQ(b.st.pc_redirects, 1L);
    CHECK_EQ(b.st.flushed, 3L);
    // jal writes the address of the following instruction, not the target.
    CHECK_EQ(b.reg(1), BASE + 4u);
}

TEST(a_flush_at_the_end_of_a_program_counts_only_real_instructions) {
    // The branch immediately precedes the halt, so at least one of the three
    // latches behind it is empty. Flushing a bubble must not be counted, or
    // flushed_instructions exceeds the number of instructions fetched.
    Bench b;
    LOAD(b, JAL(0, 8),
            ADDI(10, 0, 99),
            HALT());
    run_pipelined(b.cpu);

    CHECK(b.st.flushed <= b.st.fetches);
    CHECK_EQ(b.reg(10), 0u);
}

// --- redirects_pc() for a latch --------------------------------------------

static Latch latch_of(Op op, bool taken) {
    Decoded d = decode_none();
    d.op = op;
    d.pc = BASE;
    Latch l = latch_from_id(d, 0, 0);
    l.e.taken  = taken;
    l.e.result = BASE + 64u;
    return l;
}

TEST(a_latch_holding_a_taken_branch_redirects) {
    CHECK(redirects_pc(latch_of(OP_BEQ, true)));
}

TEST(a_latch_holding_an_untaken_branch_does_not) {
    CHECK(!redirects_pc(latch_of(OP_BEQ, false)));
}

TEST(a_latch_holding_a_jump_always_redirects) {
    CHECK(redirects_pc(latch_of(OP_JAL, false)));
    CHECK(redirects_pc(latch_of(OP_JALR, false)));
}

TEST(an_empty_latch_redirects_nothing) {
    // The essential case: a bubble's fields are zero, and read as an
    // instruction they would cause a flush on an empty stage.
    CHECK(!redirects_pc(latch_bubble()));
}

TEST(an_ordinary_instruction_does_not_redirect) {
    CHECK(!redirects_pc(latch_of(OP_ADDI, false)));
}

// --- wrong_path_instructions() ---------------------------------------------

TEST(flush_depth_follows_from_the_resolution_stage) {
    // One per stage from fetch to the resolving stage, inclusive.
    CHECK_EQ(wrong_path_instructions(RESOLVE_ID), 1);
    CHECK_EQ(wrong_path_instructions(RESOLVE_EX), 2);
    CHECK_EQ(wrong_path_instructions(RESOLVE_MEM), 3);
}
