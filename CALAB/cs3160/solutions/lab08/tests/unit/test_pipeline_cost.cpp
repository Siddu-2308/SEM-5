// The benefit of forwarding, measured by running both pipelines.
//
// The strongest check is that the two pipelines retire the *same instructions
// with the same results* and differ only in cycle count. A forwarding path that
// delivers a wrong value makes the result wrong, which the first half of each
// test detects; a path never taken makes the program slow, which the second
// half detects.

#include "check.h"

#include "bench.h"

TEST(forwarding_gets_the_same_answer_as_stalling) {
    const u32 prog[] = {ADDI(10, 0, 1),
                        ADDI(10, 10, 1),
                        ADDI(10, 10, 1),
                        ADD (11, 10, 10),
                        NOP(), NOP(), HALT()};

    Bench a;
    a.load(prog, (int)(sizeof(prog) / 4));
    run_pipelined(a.cpu);

    Bench b;
    b.load(prog, (int)(sizeof(prog) / 4));
    run_forwarding(b.cpu);

    CHECK_EQ(a.reg(10), b.reg(10));
    CHECK_EQ(a.reg(11), b.reg(11));
    CHECK_EQ(a.st.instructions, b.st.instructions);
}

TEST(and_takes_fewer_cycles_doing_it) {
    const u32 prog[] = {ADDI(10, 0, 1),
                        ADDI(10, 10, 1),
                        ADDI(10, 10, 1),
                        ADD (11, 10, 10),
                        NOP(), NOP(), HALT()};

    Bench a;
    a.load(prog, (int)(sizeof(prog) / 4));
    run_pipelined(a.cpu);

    Bench b;
    b.load(prog, (int)(sizeof(prog) / 4));
    run_forwarding(b.cpu);

    CHECK(b.st.cycles < a.st.cycles);
    CHECK_EQ(b.st.stall_cycles, 0L);
    CHECK_EQ(b.st.cycles, b.st.instructions + 4);
}

TEST(a_back_to_back_chain_now_costs_nothing) {
    Bench b;
    LOAD(b, ADDI(10, 0, 1),
            ADDI(10, 10, 1),
            ADDI(10, 10, 1),
            ADDI(10, 10, 1),
            NOP(), NOP(), HALT());
    run_forwarding(b.cpu);

    CHECK_EQ(b.reg(10), 4u);
    CHECK_EQ(b.st.stall_cycles, 0L);
}

TEST(a_load_use_dependency_costs_exactly_one_cycle) {
    Bench b;
    b.cpu.regs[11] = DATA;
    b.cpu.regs[12] = 0x99u;
    LOAD(b, SW(12, 11, 0), NOP(), NOP(),
            LW(13, 11, 0),
            ADD(14, 13, 13),
            NOP(), NOP(), HALT());
    run_forwarding(b.cpu);

    CHECK_EQ(b.reg(14), 0x132u);
    CHECK_EQ(b.st.stall_cycles, 1L);   // two, without forwarding
}

TEST(a_store_writes_the_forwarded_value_not_the_stale_one) {
    // The store takes its data from v_rs2, so the forwarded value must travel
    // down the pipeline, not be applied only to a local for the ALU.
    Bench b;
    b.cpu.regs[11] = DATA;
    LOAD(b, ADDI(12, 0, 0x77),
            SW(12, 11, 0),            // stores x12, computed one cycle ago
            NOP(), NOP(), HALT());
    run_forwarding(b.cpu);

    CHECK_EQ(ram_read(b.ram, DATA, 4), 0x77u);
}

TEST(a_taken_branch_now_costs_one_instruction_instead_of_three) {
    // Resolved in ID rather than MEM: only one instruction has been fetched
    // after the branch when it is decided.
    const u32 prog[] = {BEQ(0, 0, 16),
                        ADDI(10, 0, 99),
                        ADDI(10, 0, 99),
                        ADDI(10, 0, 99),
                        ADDI(10, 0, 5),
                        HALT()};
    Bench a;
    a.load(prog, (int)(sizeof(prog) / 4));
    run_pipelined(a.cpu);

    Bench b;
    b.load(prog, (int)(sizeof(prog) / 4));
    run_forwarding(b.cpu);

    CHECK_EQ(a.st.flushed, 3L);
    CHECK_EQ(b.st.flushed, 1L);
    CHECK_EQ(a.reg(10), 5u);
    CHECK_EQ(b.reg(10), 5u);
}

TEST(a_call_reads_a_link_register_that_was_forwarded) {
    // jal writes ra and its target reads ra, so ra must be forwarded, and the
    // forwarded value must be the return address, not the jump target.
    Bench b;
    LOAD(b, JAL(1, 8),                // over the next instruction
            ADDI(10, 0, 99),          // wrong path
            ADD(11, 1, 0),            // reads ra immediately
            NOP(), NOP(), HALT());
    run_forwarding(b.cpu);

    CHECK_EQ(b.reg(11), BASE + 4u);
    CHECK_EQ(b.reg(10), 0u);
}

TEST(the_cycle_identity_still_balances) {
    Bench b;
    LOAD(b, ADDI(10, 0, 1),
            ADDI(10, 10, 1),
            BEQ(10, 10, 12),
            ADDI(11, 0, 99),
            ADDI(11, 0, 99),
            ADDI(11, 0, 3),
            NOP(), NOP(), HALT());
    run_forwarding(b.cpu);

    CHECK_EQ(b.st.cycles,
             b.st.instructions + 4 + b.st.stall_cycles + b.st.flushed);
}
