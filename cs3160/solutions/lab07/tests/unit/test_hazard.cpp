// Read-after-write detection, without forwarding paths.
//
// A pipeline state is three structures, so each test asks hazard_stalling()
// directly: given this instruction in ID and these two ahead of it, must the
// pipeline stall?
//
// The tests follow the order of the cases in the handout.

#include "check.h"

#include "hazard.h"

// A decoded instruction with the given register numbers, built field by field
// rather than decoded, since these tests concern the hazard rules alone.
static Decoded op(Op o, int rd, int rs1, int rs2) {
    Decoded d = decode_none();
    d.op  = o;
    d.pc  = 0x80002000u;
    d.rd  = rd;
    d.rs1 = rs1;
    d.rs2 = rs2;
    return d;
}

// That instruction in a pipeline latch.
static Latch stage(const Decoded &d) { return latch_from_id(d, 0, 0); }

static const Latch EMPTY = latch_bubble();

// --- the basic rule --------------------------------------------------------

TEST(stall_when_the_producer_is_in_ex) {
    // add a0, a1, a2   in EX
    // add a3, a0, a0   in ID: a0 is not yet in the register file
    Decoded use      = op(OP_ADD, 13, 10, 10);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_stalling(use, producer, EMPTY).stall);
}

TEST(stall_when_the_producer_is_in_mem) {
    Decoded use      = op(OP_ADD, 13, 10, 10);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_stalling(use, EMPTY, producer).stall);
}

TEST(no_stall_when_the_registers_do_not_overlap) {
    // No instruction in flight writes a register this instruction reads.
    Decoded use       = op(OP_ADD, 13, 10, 10);
    Latch   unrelated = stage(op(OP_ADD, 20, 11, 12));
    Latch   producer  = stage(op(OP_ADD, 10, 11, 12));

    CHECK(!hazard_stalling(use, unrelated, EMPTY).stall);
    CHECK(!hazard_stalling(use, EMPTY, unrelated).stall);
    CHECK(!hazard_stalling(use, EMPTY, EMPTY).stall);

    // ...whereas with matching register numbers it stalls, so the answer above
    // is a decision rather than a default.
    CHECK(hazard_stalling(use, producer, EMPTY).stall);
}

TEST(either_source_register_can_collide) {
    Latch producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_stalling(op(OP_ADD, 13, 10, 20), producer, EMPTY).stall);  // rs1
    CHECK(hazard_stalling(op(OP_ADD, 13, 20, 10), producer, EMPTY).stall);  // rs2
}

// --- common errors ---------------------------------------------------------

TEST(a_bubble_is_not_a_producer) {
    // An empty latch has zeroed fields, which read as an instruction appear to
    // write x0. writes() excludes it.
    Decoded use = op(OP_ADD, 13, 10, 10);

    CHECK(!hazard_stalling(use, EMPTY, EMPTY).stall);
    CHECK(hazard_stalling(use, stage(op(OP_ADD, 10, 1, 2)), EMPTY).stall);
}

TEST(x0_is_never_a_dependency) {
    // A nop is `addi zero, zero, 0`. If a write to x0 counted, every nop would
    // stall the two instructions after it.
    Decoded use  = op(OP_ADD, 13, 0, 0);
    Latch   anop = stage(op(OP_ADDI, 0, 0, 0));

    CHECK(!hazard_stalling(use, anop, EMPTY).stall);
    CHECK(!hazard_stalling(use, EMPTY, anop).stall);

    // The same instruction writing a real register is a dependency.
    Decoded reads_x1 = op(OP_ADD, 13, 1, 1);
    Latch   writes_x1 = stage(op(OP_ADDI, 1, 0, 0));
    CHECK(hazard_stalling(reads_x1, writes_x1, EMPTY).stall);
}

TEST(only_the_registers_an_instruction_reads_matter) {
    // jal has neither rs1 nor rs2, but its encoding has immediate bits in both
    // positions. Consulting the format rather than the encoding prevents a
    // false dependency on a register number formed by the offset.
    Decoded jump     = op(OP_JAL, 1, 10, 10);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(!hazard_stalling(jump, producer, EMPTY).stall);

    // lui likewise: rd only.
    Decoded lui = op(OP_LUI, 5, 10, 10);
    CHECK(!hazard_stalling(lui, producer, EMPTY).stall);

    // An instruction that does read x10 stalls, so the answers above follow
    // from the format.
    CHECK(hazard_stalling(op(OP_ADD, 5, 10, 10), producer, EMPTY).stall);
}

TEST(stores_and_branches_read_rs2_as_well) {
    // A store takes its data from rs2, and a branch compares two registers.
    // Neither writes a register, but both read rs2.
    Latch producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_stalling(op(OP_SW, 0, 20, 10), producer, EMPTY).stall);
    CHECK(hazard_stalling(op(OP_BEQ, 0, 20, 10), producer, EMPTY).stall);
}

TEST(an_instruction_that_writes_nothing_is_not_a_producer) {
    // A store in flight writes no register, so no later instruction depends on
    // it through the register file.
    Decoded use   = op(OP_ADD, 13, 10, 10);
    Latch   store = stage(op(OP_SW, 0, 11, 10));

    CHECK(!hazard_stalling(use, store, EMPTY).stall);
    // An instruction that does write x10 causes a stall.
    CHECK(hazard_stalling(use, stage(op(OP_ADD, 10, 11, 12)), EMPTY).stall);
}

TEST(the_wb_stage_is_not_consulted) {
    // hazard_stalling() is not given the WB latch: the register file is written
    // in the first half of the cycle and read in the second, so a dependency on
    // the instruction in WB is already satisfied. Hence a back-to-back
    // dependency costs two cycles, not three.
    Decoded use      = op(OP_ADD, 13, 10, 10);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));

    CHECK(hazard_stalling(use, producer, EMPTY).stall);   // in EX:  wait
    CHECK(hazard_stalling(use, EMPTY, producer).stall);   // in MEM: wait
    // and one cycle later it is in WB, and no stall is needed.
}

TEST(a_load_is_a_producer_like_any_other) {
    // Without forwarding a load is not a special case: it writes a register,
    // and any reader of that register waits.
    Decoded use  = op(OP_ADD, 13, 10, 10);
    Latch   load = stage(op(OP_LW, 10, 11, 0));

    CHECK(hazard_stalling(use, load, EMPTY).stall);
    CHECK(hazard_stalling(use, EMPTY, load).stall);
}

TEST(nothing_is_forwarded_this_week) {
    // The forwarding fields are used only by the forwarding model; a model
    // without forwarding must leave them unset.
    Decoded use      = op(OP_ADD, 13, 10, 10);
    Latch   producer = stage(op(OP_ADD, 10, 11, 12));
    Hazard  h        = hazard_stalling(use, producer, EMPTY);

    CHECK(h.stall);                    // it waits...
    CHECK(h.fwd_rs1 == FWD_NONE);      // ...rather than forwarding
    CHECK(h.fwd_rs2 == FWD_NONE);
}
