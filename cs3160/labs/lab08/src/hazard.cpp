#include "hazard.h"

#include "log.h"

// Whether the instruction in `l` writes register `n`.
//
// Two conditions that every comparison needs are included. An empty latch
// writes nothing, so a bubble is never a producer. Register 0 never counts: it
// is hardwired to zero, and treating it as a dependency would stall on every
// nop (`addi zero, zero, 0`).
static bool writes(const Latch &l, int n) {
    return l.valid && n != 0 && writes_rd(l.d.op) && l.d.rd == n;
}

// The registers the instruction reads, or 0. An instruction whose format has no
// rs2 still has bits in that position, belonging to the immediate; consulting
// the format rather than the encoding prevents false dependencies, such as a
// jal depending on a register number formed by its immediate bits.
static void sources(const Decoded &d, int *rs1, int *rs2) {
    *rs1 = reads_rs1(d.op) ? d.rs1 : 0;
    *rs2 = reads_rs2(d.op) ? d.rs2 : 0;
}

// --- Without forwarding: the only remedy is to stall -----------------------

Hazard hazard_stalling(const Decoded &id, const Latch &ex, const Latch &mem) {
    Hazard h;
    h.stall   = false;
    h.fwd_rs1 = FWD_NONE;
    h.fwd_rs2 = FWD_NONE;

    int rs1, rs2;
    sources(id, &rs1, &rs2);

    // An instruction in EX or MEM has not yet written the register file, so a
    // dependent instruction must wait.
    if (writes(ex, rs1) || writes(ex, rs2) ||
        writes(mem, rs1) || writes(mem, rs2)) {
        h.stall = true;
    }
    return h;
}

// --- With forwarding: the two dependencies it cannot resolve --------------

Hazard hazard_forwarding(const Decoded &id, const Latch &ex, const Latch &mem,
                         bool resolves_in_id) {
    Hazard h;
    h.stall   = false;
    h.fwd_rs1 = FWD_NONE;
    h.fwd_rs2 = FWD_NONE;

    // TODO(lab 8): what forwarding cannot fix in time.
    //
    // Most dependencies are satisfied in EX by forward_in_ex(), below. Two
    // cases remain, each costing one stall cycle.
    //
    // Load-use, present in every five-stage forwarding pipeline: a loaded
    // value does not exist until the memory access completes, so the
    // instruction immediately after a load that reads its result must
    // stall. Use is_load() and the `ex` latch.
    //
    // Branches resolved in ID, which reduces the cost of a taken branch from
    // three flushed instructions to one. A branch needs its operands one
    // stage earlier than other instructions. Guard this case with
    // `resolves_in_id` and apply it only to branches and jumps:
    //
    //   * producer in EX: no result yet                       -> stall
    //   * load in MEM: data arrives at the end of this cycle  -> stall
    //   * any other producer in MEM                           -> forward,
    //     by setting h.fwd_rs1 / h.fwd_rs2 to FWD_EX_MEM
    //
    // The run loop applies these two fields through forwarded_value().
    (void)id;
    (void)ex;
    (void)mem;
    (void)resolves_in_id;
    (void)writes;
    (void)sources;
    return h;
}

// The decision ID makes, by whichever of the two functions above belongs to the
// model being run.
Hazard hazard_in_id(const Decoded &id, bool id_valid, const Latch &ex,
                    const Latch &mem, const Latch &wb,
                    bool forwarding, bool resolves_in_id) {
    Hazard h;
    h.stall   = false;
    h.fwd_rs1 = FWD_NONE;
    h.fwd_rs2 = FWD_NONE;

    if (!id_valid) {
        return h;
    }

    // WB writes the register file in the first half of the cycle and ID reads
    // it in the second, so a dependency on the instruction in WB needs neither
    // a stall nor forwarding; hence neither function below takes it.
    (void)wb;

    return forwarding ? hazard_forwarding(id, ex, mem, resolves_in_id)
                      : hazard_stalling(id, ex, mem);
}

Forwarding forward_in_ex(const Latch &ex, const Latch &mem, const Latch &wb) {
    Forwarding f;
    f.rs1 = FWD_NONE;
    f.rs2 = FWD_NONE;

    if (!ex.valid) {
        return f;
    }

    // TODO(lab 8): where should this instruction's operands come from?
    //
    // The instruction in `ex` is about to use the ALU. For each register it
    // reads, determine whether an older instruction still in the pipeline
    // writes that register, in which case the value read from the register
    // file is stale. Use sources() and writes(), above.
    //
    // Set f.rs1 / f.rs2 to FWD_EX_MEM to take the value of the instruction
    // in MEM, to FWD_MEM_WB for the instruction in WB, or leave FWD_NONE
    // when the register file value is current.
    //
    // If both write the register, the more recent instruction takes
    // precedence.
    //
    // This function never stalls: the load-use dependency, which forwarding
    // cannot satisfy, is detected a stage earlier by hazard_forwarding().
    (void)mem;
    (void)wb;
    return f;
}

u32 forwarded_value(FwdSrc src, u32 from_regfile, const Latch &mem, const Latch &wb) {
    // TODO(lab 8): fetch the value forward_in_ex() decided to take.
    //
    // For FWD_NONE, return from_regfile. Otherwise return the value the
    // producer will write to the register file, as computed by
    // writeback_value(), not mem.e.result: for jal, the ALU result is the
    // jump target, whereas rd receives the return address.
    //
    // An instruction in MEM has not yet accessed memory, so a load there has
    // no value to forward. The load-use stall in hazard_forwarding()
    // prevents this case; should it occur, call fatal() rather than forward
    // an incorrect value.
    (void)src;
    (void)mem;
    (void)wb;
    return from_regfile;
}

bool redirects_pc(const Latch &l) {
    return l.valid && redirects_pc(l.d, l.e);
}

int wrong_path_instructions(ResolveStage stage) {
    switch (stage) {
        case RESOLVE_ID:  return 1;
        case RESOLVE_EX:  return 2;
        case RESOLVE_MEM: return 3;
    }
    fatal("unknown branch resolution stage");
}
