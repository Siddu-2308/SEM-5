// The five-stage pipeline with forwarding, and control flow resolved early.
//
// The structure is that of processor_pipelined.cpp: one iteration is one
// cycle, and each stage reads the latch in front of it and writes a *next*
// latch. It is deliberately a separate loop rather than the same loop with a
// flag, so that the two models can be compared directly.
//
// Three differences:
//
//   * a result is forwarded from the pipeline register holding it rather than
//     waited for; forward_in_ex() selects the source for the instruction
//     entering the ALU.
//   * branches are resolved in ID, so one instruction, not three, is fetched
//     on the wrong path.
//   * two dependencies remain that forwarding cannot resolve, each costing a
//     one-cycle stall: a load followed immediately by a use of its result, and
//     a branch whose operand is still being computed.

#include "hazard.h"
#include "log.h"
#include "processor.h"

void run_forwarding(Processor &cpu) {
    const Config &cfg          = *cpu.cfg;
    Stats        &st           = *cpu.stats;
    const ResolveStage resolve = config_resolve_stage(cfg, PROC_FORWARDING);

    Latch if_id  = latch_bubble();
    Latch id_ex  = latch_bubble();
    Latch ex_mem = latch_bubble();
    Latch mem_wb = latch_bubble();

    bool done = false;

    // As in processor_pipelined.cpp: report a pipeline that retires nothing,
    // rather than running to the instruction budget.
    const long IDLE_LIMIT = 256;
    long idle = 0, retired_before = -1;

    while (!done && st.instructions < cfg.num_insts) {
        if (st.instructions == retired_before) {
            if (++idle >= IDLE_LIMIT) {
                log_error("the pipeline has run %ld cycles without retiring an "
                          "instruction, so it is not moving.", IDLE_LIMIT);
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

        // --- WB ---
        if (mem_wb.valid) {
            if (mem_wb.d.op == OP_INVALID) {
                processor_illegal(cpu, mem_wb.d);
            }
            stage_writeback(cpu, mem_wb.d, mem_wb.e.result, mem_wb.m, mem_wb.next_pc);
            if (mem_wb.m.did_write && mem_wb.m.addr == cfg.tohost) {
                done = true;
            }
        }

        // --- MEM ---
        if (ex_mem.valid) {
            Latch l     = ex_mem;
            l.m         = stage_memory(cpu, l.d, l.e.result, l.v_rs2);
            mem_latency = l.m.latency;
            next_mem_wb = l;
        }

        // TODO(lab 8): select each operand's source, then compute.
        //
        // The rest of the loop is that of the stalling pipeline. In this stage,
        // determine each operand's source with forward_in_ex() and obtain its
        // value with forwarded_value(), both in hazard.cpp.
        //
        // Write the values into the latch (l.v_rs1, l.v_rs2), not into locals.
        // MEM takes store data from v_rs2; a value forwarded only locally gives
        // a store whose address uses the new value but whose data is stale.
        //
        // Then compute as before: stage_execute() and next_pc_of() into the
        // latch, which becomes the next EX/MEM.
        if (id_ex.valid) {
            Latch l     = id_ex;
            l.e         = stage_execute(l.d, l.v_rs1, l.v_rs2);
            l.next_pc   = next_pc_of(l.d, l.e);
            next_ex_mem = l;
        }

        // --- ID: decode, check hazards, read registers, resolve branches ---
        Hazard hz;
        hz.stall   = false;
        hz.fwd_rs1 = FWD_NONE;
        hz.fwd_rs2 = FWD_NONE;

        // TODO(lab 8): resolve branches in ID.
        //
        // As in the stalling pipeline: decode, call hazard_in_id(), and if there
        // is no stall read the operands and build the next ID/EX. Two changes:
        //
        //   * Pass forwarding=true, so that hazard_in_id() calls
        //     hazard_forwarding(). Besides a stall, the result may now set
        //     hz.fwd_rs1 / hz.fwd_rs2: apply them with forwarded_value() to the
        //     values from stage_operands() before building the latch. For any
        //     instruction other than a branch they are FWD_NONE.
        //   * When resolve == RESOLVE_ID and the instruction is a branch or a
        //     jump, compute its outcome into the latch with stage_execute() and
        //     next_pc_of(). The redirect below reads it there; resolving in ID
        //     reduces the cost of a taken branch from three flushed
        //     instructions to one.
        if (if_id.valid) {
            Decoded d = decode(if_id.d.raw, if_id.d.pc);
            hz = hazard_in_id(d, true, id_ex, ex_mem, mem_wb,
                              /*forwarding=*/true,
                              /*resolves_in_id=*/resolve == RESOLVE_ID);
            (void)d;
        }

        // --- IF ---
        if (hz.stall) {
            next_if_id = if_id;
            st.stall_cycles++;
        } else if (cpu.halted) {
            // The program has stored to tohost: drain the pipeline.
        } else {
            Fetched f     = stage_fetch(cpu, cpu.pc);
            fetch_latency = f.latency;
            next_if_id    = latch_from_fetch(f);
            cpu.pc        = cpu.pc + 4;
        }

        // --- Control flow ---
        const Latch *resolved = (resolve == RESOLVE_MEM)  ? &next_mem_wb
                                : (resolve == RESOLVE_EX) ? &next_ex_mem
                                                          : &next_id_ex;
        if (redirects_pc(*resolved)) {
            cpu.pc = resolved->next_pc;
            st.pc_redirects++;

            Latch *younger[3] = {&next_if_id, &next_id_ex, &next_ex_mem};
            int    depth      = wrong_path_instructions(resolve);
            for (int i = 0; i < depth; i++) {
                if (younger[i]->valid) {
                    st.flushed++;
                }
                *younger[i] = latch_bubble();
            }
            log_debug("0x%08x redirects to 0x%08x: %d wrong-path instruction(s) "
                      "discarded", resolved->d.pc, cpu.pc, depth);
        }

        if_id  = next_if_id;
        id_ex  = next_id_ex;
        ex_mem = next_ex_mem;
        mem_wb = next_mem_wb;

        // A memory access longer than a cycle stalls the whole pipeline, so the
        // extra time is charged to the cycle rather than to a stage.
        int cost = cycle_cost(cpu, fetch_latency, mem_latency);
        st.cycles += cost;
        st.mem_stall_cycles += cost - 1;
    }
    processor_dump(cpu);
}
