// Forwarding: the source of each operand, and the two stalls that remain.
//
// Three functions, each answering a different question:
//
//   forward_in_ex()      in EX, when the ALU needs its inputs: does a later
//                        stage hold a more recent value than the register file?
//   forwarded_value()    if so, what is that value?
//   hazard_forwarding()  in ID, before an instruction proceeds: is this one of
//                        the two dependencies forwarding cannot resolve?

#include "check.h"

#include "hazard.h"

static Decoded op(Op o, int rd, int rs1, int rs2) {
    Decoded d = decode_none();
    d.op  = o;
    d.pc  = 0x80002000u;
    d.rd  = rd;
    d.rs1 = rs1;
    d.rs2 = rs2;
    return d;
}

static Latch stage(const Decoded &d) { return latch_from_id(d, 0, 0); }

// A latch that has passed EX and MEM, so that it holds a result to forward.
static Latch produced(const Decoded &d, u32 result) {
    Latch l = stage(d);
    l.e.result  = result;
    l.m.did_read = false;
    l.m.value    = 0;
    return l;
}

static const Latch EMPTY = latch_bubble();

// --- forward_in_ex ---------------------------------------------------------

TEST(an_operand_comes_from_mem_when_the_instruction_there_writes_it) {
    Latch      use      = stage(op(OP_ADD, 13, 10, 20));
    Latch      producer = produced(op(OP_ADD, 10, 11, 12), 99);
    Forwarding f        = forward_in_ex(use, producer, EMPTY);

    CHECK(f.rs1 == FWD_EX_MEM);
    CHECK(f.rs2 == FWD_NONE);
}

TEST(an_operand_comes_from_wb_when_that_is_where_it_is) {
    Latch      use      = stage(op(OP_ADD, 13, 20, 10));
    Latch      producer = produced(op(OP_ADD, 10, 11, 12), 99);
    Forwarding f        = forward_in_ex(use, EMPTY, producer);

    CHECK(f.rs1 == FWD_NONE);
    CHECK(f.rs2 == FWD_MEM_WB);
}

TEST(mem_wins_over_wb_when_both_wrote_the_register) {
    // Both instructions in flight write x10. The one in MEM is more recent, so
    // its value is the one the program intends. The reverse precedence gives a
    // result that is usually, but not always, correct.
    Latch      use   = stage(op(OP_ADD, 13, 10, 10));
    Latch      older = produced(op(OP_ADD, 10, 1, 2), 11);
    Latch      newer = produced(op(OP_ADD, 10, 3, 4), 22);
    Forwarding f     = forward_in_ex(use, newer, older);

    CHECK(f.rs1 == FWD_EX_MEM);
    CHECK(f.rs2 == FWD_EX_MEM);
}

TEST(nothing_is_forwarded_when_nothing_matches) {
    Latch      use       = stage(op(OP_ADD, 13, 10, 10));
    Latch      unrelated = produced(op(OP_ADD, 20, 11, 12), 99);
    Forwarding f         = forward_in_ex(use, unrelated, EMPTY);

    CHECK(f.rs1 == FWD_NONE);
    CHECK(f.rs2 == FWD_NONE);

    // With matching register numbers it forwards, so the answer above is a
    // decision rather than a default.
    Forwarding g = forward_in_ex(use, produced(op(OP_ADD, 10, 11, 12), 99), EMPTY);
    CHECK(g.rs1 == FWD_EX_MEM);
}

TEST(a_bubble_forwards_nothing_and_receives_nothing) {
    Latch      producer = produced(op(OP_ADD, 10, 11, 12), 99);
    Forwarding f        = forward_in_ex(EMPTY, producer, producer);
    CHECK(f.rs1 == FWD_NONE);
    CHECK(f.rs2 == FWD_NONE);

    Latch      use = stage(op(OP_ADD, 13, 10, 10));
    Forwarding g   = forward_in_ex(use, EMPTY, EMPTY);
    CHECK(g.rs1 == FWD_NONE);
    CHECK(g.rs2 == FWD_NONE);

    // An instruction in the same stage is forwarded from.
    Forwarding h = forward_in_ex(use, producer, EMPTY);
    CHECK(h.rs1 == FWD_EX_MEM);
}

TEST(x0_is_never_forwarded) {
    Latch      use  = stage(op(OP_ADD, 13, 0, 0));
    Latch      anop = produced(op(OP_ADDI, 0, 0, 0), 0);
    Forwarding f    = forward_in_ex(use, anop, EMPTY);

    CHECK(f.rs1 == FWD_NONE);
    CHECK(f.rs2 == FWD_NONE);

    // The same instruction writing x1 instead of x0 is forwarded.
    Latch      real_one = produced(op(OP_ADDI, 1, 0, 0), 0);
    Forwarding g = forward_in_ex(stage(op(OP_ADD, 13, 1, 1)), real_one, EMPTY);
    CHECK(g.rs1 == FWD_EX_MEM);
}

// --- forwarded_value -------------------------------------------------------

TEST(forwarding_hands_over_the_value_that_will_be_written_back) {
    Latch producer = produced(op(OP_ADD, 10, 11, 12), 0x1234u);

    CHECK_EQ(forwarded_value(FWD_EX_MEM, 0xDEADu, producer, EMPTY), 0x1234u);
    CHECK_EQ(forwarded_value(FWD_MEM_WB, 0xDEADu, EMPTY, producer), 0x1234u);
}

TEST(with_no_forwarding_the_register_file_value_is_used) {
    Latch producer = produced(op(OP_ADD, 10, 11, 12), 0x1234u);

    CHECK_EQ(forwarded_value(FWD_NONE, 0xDEADu, producer, producer), 0xDEADu);
    // ...and each path returns the value of its own stage.
    CHECK_EQ(forwarded_value(FWD_EX_MEM, 0xDEADu, producer, EMPTY), 0x1234u);
}

TEST(a_jump_forwards_its_return_address_not_its_target) {
    // jal's ALU result is the jump target; it *writes* the address of the
    // following instruction. Forwarding e.result instead of writeback_value()
    // is an error visible only at the first instruction of a called function.
    Decoded jump = op(OP_JAL, 1, 0, 0);
    Latch   l    = stage(jump);
    l.e.result   = 0x80005000u;     // where it jumped
    l.next_pc    = 0x80005000u;

    u32 got = forwarded_value(FWD_MEM_WB, 0, EMPTY, l);
    CHECK_EQ(got, jump.pc + 4u);
}

TEST(a_load_forwards_what_it_read) {
    Decoded load = op(OP_LW, 10, 11, 0);
    Latch   l    = stage(load);
    l.e.result   = 0x80002000u;     // the address
    l.m.did_read = true;
    l.m.value    = 0x5555u;         // the data

    // In WB the memory access is complete, so the loaded data is available.
    CHECK_EQ(forwarded_value(FWD_MEM_WB, 0, EMPTY, l), 0x5555u);
}

// --- hazard_forwarding -----------------------------------------------------

TEST(forwarding_removes_the_ordinary_stall) {
    // The dependency that cost two stall cycles without forwarding costs none.
    Decoded use      = op(OP_ADD, 13, 10, 10);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(!hazard_forwarding(use, producer, EMPTY, false).stall);
    CHECK(!hazard_forwarding(use, EMPTY, producer, false).stall);

    // A load in EX is the exception: its value does not yet exist.
    CHECK(hazard_forwarding(use, stage(op(OP_LW, 10, 11, 0)), EMPTY, false).stall);
}

TEST(a_load_use_dependency_still_costs_one_cycle) {
    // The dependency no forwarding path can resolve: the load is in EX, so its
    // memory access has not yet occurred.
    Decoded use  = op(OP_ADD, 13, 10, 10);
    Latch   load = stage(op(OP_LW, 10, 11, 0));

    CHECK(hazard_forwarding(use, load, EMPTY, false).stall);

    // One stage later it is in MEM, and its data arrives in time for EX.
    CHECK(!hazard_forwarding(use, EMPTY, load, false).stall);
}

TEST(a_load_whose_value_nobody_reads_does_not_stall) {
    Latch   load = stage(op(OP_LW, 10, 11, 0));

    CHECK(!hazard_forwarding(op(OP_ADD, 13, 20, 20), load, EMPTY, false).stall);
    // The same load, read by the following instruction, does stall.
    CHECK(hazard_forwarding(op(OP_ADD, 13, 10, 20), load, EMPTY, false).stall);
}

TEST(a_branch_resolved_in_id_needs_its_operands_one_stage_early) {
    // Resolving in ID reduces the cost of a taken branch from three
    // instructions to one, at a price: the branch's producer has one stage less
    // in which to complete.
    Decoded branch   = op(OP_BEQ, 0, 10, 20);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_forwarding(branch, producer, EMPTY, true).stall);

    // An ordinary instruction in the same position does not stall: it obtains
    // its operands a stage later, when the value can be forwarded.
    Decoded use = op(OP_ADD, 13, 10, 20);
    CHECK(!hazard_forwarding(use, producer, EMPTY, true).stall);
}

TEST(a_branch_takes_its_operand_from_mem_when_it_can) {
    // The producer is in MEM, so its result exists and is forwarded into ID
    // without a stall.
    Decoded branch   = op(OP_BEQ, 0, 10, 20);
    Latch   producer = produced(op(OP_ADD, 10, 11, 12), 7);
    Hazard  h        = hazard_forwarding(branch, EMPTY, producer, true);

    CHECK(!h.stall);
    CHECK(h.fwd_rs1 == FWD_EX_MEM);
    CHECK(h.fwd_rs2 == FWD_NONE);
}

TEST(a_branch_behind_a_load_in_mem_still_stalls) {
    // A load in MEM completes at the end of this cycle: too late for the
    // comparison here, in time for the register file in the next cycle.
    Decoded branch = op(OP_BEQ, 0, 10, 20);
    Latch   load   = stage(op(OP_LW, 10, 11, 0));

    CHECK(hazard_forwarding(branch, EMPTY, load, true).stall);
}

TEST(a_jump_through_a_register_needs_its_operand_too) {
    // jalr reads rs1, and it is resolved in ID like a branch.
    Decoded jump     = op(OP_JALR, 1, 10, 0);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_forwarding(jump, producer, EMPTY, true).stall);
}

TEST(nothing_stalls_for_branches_when_they_resolve_later) {
    // With resolves_in_id false, a branch is an ordinary instruction: it obtains
    // its operands in EX, where forwarding supplies them.
    Decoded branch   = op(OP_BEQ, 0, 10, 20);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(!hazard_forwarding(branch, producer, EMPTY, false).stall);
    // The same branch, when resolved in ID, must stall.
    CHECK(hazard_forwarding(branch, producer, EMPTY, true).stall);
}
