// The five-stage pipeline, with no forwarding: every hazard is a stall.
//
// Each iteration of the loop is one clock cycle, in which all five stages run.
// Each stage reads the latch in front of it (its state at the start of the
// cycle) and writes a *next* latch; at the end of the cycle the four next
// latches replace the current ones. This models pipeline registers, and makes
// the result independent of the order in which the stages appear.
//
// One exception is deliberate: WB writes the register file in the first half
// of the cycle and ID reads it in the second, so an instruction in ID sees the
// value being written by the instruction in WB. A back-to-back dependency
// therefore costs two stall cycles rather than three, as in the textbook.
//
// This model differs from the forwarding model in two respects: it has no
// forwarding paths (hazard_in_id() is told so), and it resolves branches in
// the stage the configuration names.

#include "hazard.h"
#include "log.h"
#include "processor.h"

void run_pipelined(Processor &cpu) {
    const Config &cfg     = *cpu.cfg;
    Stats        &st      = *cpu.stats;
    const ResolveStage resolve = config_resolve_stage(cfg, PROC_PIPELINED);

    Latch if_id  = latch_bubble();
    Latch id_ex  = latch_bubble();
    Latch ex_mem = latch_bubble();
    Latch mem_wb = latch_bubble();

    bool done = false;  // the store to tohost has retired; the program is over

    // A pipeline that retires nothing is making no progress, the usual state of
    // an incomplete implementation. Without this check the loop would run to
    // the instruction budget, indistinguishable from a hang. The limit far
    // exceeds the longest legitimate gap between retirements: four cycles to
    // fill the pipeline, plus a stall, plus a flush.
    const long IDLE_LIMIT = 256;
    long idle = 0, retired_before = -1;

    while (!done && st.instructions < cfg.num_insts) {
        if (st.instructions == retired_before) {
            if (++idle >= IDLE_LIMIT) {
                log_error("the pipeline has run %ld cycles without retiring an "
                          "instruction, so it is not moving. Look at what fills "
                          "IF/ID and what copies each next_* latch over the one "
                          "in front of it.", IDLE_LIMIT);
                break;
            }
        } else {
            idle           = 0;
            retired_before = st.instructions;
        }

        int fetch_latency = 0;
        int mem_latency   = 0;

        Latch next_if_id  = latch_bubble();
        Latch next_id_ex  = latch_bubble();
        Latch next_ex_mem = latch_bubble();
        Latch next_mem_wb = latch_bubble();

        pipeline_dump(st.cycles, if_id, id_ex, ex_mem, mem_wb);

        // TODO(lab 6): the writeback stage.
        //
        // If MEM/WB holds an instruction, retire it with stage_writeback(); the
        // latch holds every argument it needs. Set `done` when the retiring
        // instruction is the store to cfg.tohost: the program has ended.
        //
        // Report an instruction that does not decode (processor_illegal()) here
        // and only here. An invalid word fetched on a wrong path is flushed
        // before reaching WB; reporting it earlier would fail correct programs.

        // TODO(lab 6): the memory stage.
        //
        // Copy EX/MEM into a local, set its `m` with stage_memory(), and make it
        // the next MEM/WB. Record the access latency in mem_latency, which the
        // cycle cost at the end of the loop requires.
        //
        // Every stage has this form: read the current latch in front, add this
        // stage's result, write the *next* latch. Never write a current latch,
        // which another stage is still reading.
        (void)mem_latency;

        // TODO(lab 6): the execute stage.
        //
        // As above: take ID/EX, set `e` with stage_execute() and `next_pc` with
        // next_pc_of(), and make it the next EX/MEM.

        // --- ID: decode, check for hazards, read the registers ---
        Hazard hz;
        hz.stall   = false;
        hz.fwd_rs1 = FWD_NONE;
        hz.fwd_rs2 = FWD_NONE;

        // TODO(lab 6): the decode stage.
        //
        // If IF/ID holds an instruction, decode it (IF/ID holds bits, not a
        // Decoded), then call hazard_in_id() to decide whether it may proceed,
        // with forwarding=false and resolves_in_id = (resolve == RESOLVE_ID).
        // Until hazard detection is implemented it always reports no hazard.
        //
        // If it may proceed, read its operands with stage_operands() and build
        // the next ID/EX with latch_from_id().
        //
        // If it must stall, do nothing: next_id_ex remains a bubble, and the IF
        // stage below holds the stalled instruction in IF/ID.
        //
        // Only when resolve == RESOLVE_ID: compute the branch outcome into the
        // latch, which the redirect below reads. The default configuration
        // resolves in MEM, so this case can be left until last.

        // TODO(lab 6): the fetch stage, and what a stall does to it.
        //
        // Three cases, in this order:
        //
        //   stalled    IF/ID keeps its instruction and the PC does not advance.
        //              Nothing is fetched, so a stall costs no memory access.
        //              Count the cycle in st.stall_cycles.
        //   halted     stop fetching and let the pipeline drain: do nothing.
        //   otherwise  fetch at cpu.pc into the next IF/ID with
        //              latch_from_fetch(), record the latency in fetch_latency,
        //              and advance cpu.pc by 4.
        //
        // Fetching pc + 4 is correct even after a branch: its outcome is not
        // known for several more stages, and the flush below corrects the path.
        (void)hz;
        (void)fetch_latency;

        // TODO(lab 6): control hazards.
        //
        // The deciding instruction is the one leaving the resolving stage;
        // `resolved` (provided) points to its next_* latch. If that latch
        // redirects the PC (redirects_pc(), in hazard.cpp):
        //
        //   * set cpu.pc to its next_pc and count it in st.pc_redirects;
        //   * flush the instructions fetched after it: the younger latches
        //     next_if_id, next_id_ex, next_ex_mem, in that order. The number
        //     to flush is wrong_path_instructions(); flush a latch by
        //     replacing it with latch_bubble().
        //
        // Count in st.flushed only latches that held an instruction. A stall
        // inserts a bubble, which may be among these; flushing it costs nothing.
        const Latch *resolved = (resolve == RESOLVE_MEM)  ? &next_mem_wb
                                : (resolve == RESOLVE_EX) ? &next_ex_mem
                                                          : &next_id_ex;
        (void)resolved;

        // TODO(lab 6): end the cycle.
        //
        // Copy each next_* latch over the corresponding current latch. This is
        // the clock edge of the pipeline registers; doing it here, rather than
        // as each stage finishes, makes the result independent of stage order.
        //
        // Then charge the cycle: its cost is cycle_cost(cpu, fetch_latency,
        // mem_latency), of which every cycle beyond the first is memory stall;
        // add both to st. A slow memory stalls the whole pipeline, so the cost
        // belongs to the cycle rather than to any stage.
        (void)next_if_id;
        (void)next_id_ex;
        (void)next_ex_mem;
        (void)next_mem_wb;
    }
    processor_dump(cpu);
}
