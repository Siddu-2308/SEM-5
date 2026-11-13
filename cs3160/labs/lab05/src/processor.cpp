#include "processor.h"

#include <cstring>

#include "alu.h"
#include "disasm.h"
#include "log.h"

void processor_init(Processor &cpu, const Config &cfg, Stats &st,
                    MemoryLevel &imem, MemoryLevel &dmem) {
    cpu.cfg   = &cfg;
    cpu.stats = &st;
    cpu.imem  = &imem;
    cpu.dmem  = &dmem;
    cpu.pc    = cfg.start;
    memset(cpu.regs, 0, sizeof(cpu.regs));
    cpu.halted      = false;
    cpu.exit_status = 0;
}

// A model that is not yet built reports an error rather than running another
// model in its place.
static void dispatch_pipelined(Processor &cpu) {
    (void)cpu;
    fatal("--proc=pipelined: this simulator has no pipeline yet. "
          "Use --proc=single.");
}

static void dispatch_forwarding(Processor &cpu) {
    (void)cpu;
    fatal("--proc=forwarding: this simulator has no forwarding pipeline "
          "yet. Use --proc=single or --proc=pipelined.");
}

void processor_run(Processor &cpu) {
    switch (cpu.cfg->proc) {
        case PROC_SINGLE:     run_single(cpu); break;
        case PROC_PIPELINED:  dispatch_pipelined(cpu); break;
        case PROC_FORWARDING: dispatch_forwarding(cpu); break;
    }

    Stats &st = *cpu.stats;
    st.exited      = cpu.halted;
    st.exit_status = cpu.exit_status;

    if (cpu.halted) {
        log_info("program exited with status %d after %ld instructions, %ld cycles",
                 cpu.exit_status, st.instructions, st.cycles);
    } else {
        log_error("instruction budget of %ld exhausted without the program "
                  "reaching _exit; the trace is incomplete",
                  cpu.cfg->num_insts);
    }
}

// --- Register file ---------------------------------------------------------

u32 reg_read(const Processor &cpu, int n) {
    if (n == 0) {
        return 0;
    }
    return cpu.regs[n];
}

void reg_write(Processor &cpu, int n, u32 value) {
    if (n == 0) {
        return;  // x0 is hardwired to zero
    }
    cpu.regs[n] = value;
}

// --- Stages ----------------------------------------------------------------

Fetched stage_fetch(Processor &cpu, u32 pc) {
    Fetched f;
    f.pc      = pc;
    f.latency = 0;
    f.raw     = cpu.imem->read(pc, 4, &f.latency);
    cpu.stats->fetches++;

    if (log_debug_enabled()) {
        log_debug("IF  0x%08x: %08x  %s", pc, f.raw, disasm(f.raw, pc).c_str());
    }
    return f;
}

void stage_operands(const Processor &cpu, const Decoded &d, u32 *v_rs1, u32 *v_rs2) {
    *v_rs1 = reads_rs1(d.op) ? reg_read(cpu, d.rs1) : 0;
    *v_rs2 = reads_rs2(d.op) ? reg_read(cpu, d.rs2) : 0;
}

Executed stage_execute(const Decoded &d, u32 v_rs1, u32 v_rs2) {
    // TODO(lab 5): the execute stage.
    //
    // Return an Executed: `result` is the ALU result, and `taken` whether a
    // branch goes to its target (false for all other instructions).
    // alu_inputs() selects the operands, alu_apply() combines them, and
    // branch_taken() decides `taken`; all three are in alu.h.
    (void)d;
    (void)v_rs1;
    (void)v_rs2;
    Executed e;
    e.result = 0;
    e.taken  = false;
    return e;
}

u32 next_pc_of(const Decoded &d, const Executed &e) {
    // TODO(lab 5): where does control go next?
    //
    // In order of precedence: a jump goes to the ALU result, `e.result`
    // (jal and jalr targets are additions); a taken branch goes to its pc
    // plus its immediate; any other instruction goes to pc + 4. is_jump()
    // and is_branch() are in decode.h.
    (void)e;
    return d.pc + 4;
}

bool redirects_pc(const Decoded &d, const Executed &e) {
    if (is_jump(d.op)) {
        return true;  // jumps are unconditional
    }
    return is_branch(d.op) && e.taken;
}

MemAccess stage_memory(Processor &cpu, const Decoded &d, u32 addr, u32 v_rs2) {
    // TODO(lab 5): the memory stage.
    //
    // Only loads and stores access memory. For any other instruction,
    // return a MemAccess with both flags false. `addr` is the ALU result.
    //
    //   - Width: mem_size() gives 1, 2 or 4 bytes, the size argument of
    //     cpu.dmem->read() and ->write(), which also set the latency.
    //   - Sign: a sub-word load arrives in the low bits; lb/lh sign-extend
    //     and lbu/lhu zero-extend. mem_signed() says which; sign_extend()
    //     performs it.
    //   - Termination: crt.S ends with a store to cpu.cfg->tohost. On that
    //     store, set cpu.halted and cpu.exit_status (see crt.S for the
    //     encoding of the status). Without this the simulator never halts.
    //
    // Count each load and store in cpu.stats->data_accesses.
    (void)cpu;
    (void)d;
    (void)v_rs2;
    MemAccess m;
    m.did_read  = false;
    m.did_write = false;
    m.addr      = addr;
    m.value     = 0;
    m.latency   = 0;
    return m;
}

// One line per retired instruction, in the format the tests parse. Four fields,
// always in this order, so that traces can be compared line by line:
//
//   <pc> | next_pc = <pc> | x<rd> = <value> | mem[<addr>] <op> <value>
//
// A field that does not apply is filled with `?`, so that every line has the
// same shape. `=>` marks a load, `<=` a store.
static void trace_retire(const Processor &cpu, const Decoded &d, u32 next_pc,
                         const MemAccess &m) {
    char reg_field[32];
    if (writes_rd(d.op)) {
        // Read back from the register file, so that a write to x0 shows the
        // zero actually stored.
        snprintf(reg_field, sizeof(reg_field), "x%d = %08x", d.rd,
                 reg_read(cpu, d.rd));
    } else {
        snprintf(reg_field, sizeof(reg_field), "x? = %08x", 0);
    }

    char mem_field[40];
    if (m.did_read) {
        snprintf(mem_field, sizeof(mem_field), "mem[%08x] => %08x", m.addr, m.value);
    } else if (m.did_write) {
        snprintf(mem_field, sizeof(mem_field), "mem[%08x] <= %08x", m.addr, m.value);
    } else {
        snprintf(mem_field, sizeof(mem_field), "mem[?] = %08x", 0);
    }

    log_out("%08x | next_pc = %08x | %s | %s", d.pc, next_pc, reg_field, mem_field);
}

u32 writeback_value(const Decoded &d, u32 result, const MemAccess &m) {
    if (is_load(d.op)) {
        return m.value;  // the loaded data, not the computed address
    }
    if (is_jump(d.op)) {
        // jal and jalr write the return address; the ALU result is the
        // target, which goes to the PC.
        return d.pc + 4;
    }
    return result;
}

void stage_writeback(Processor &cpu, const Decoded &d, u32 result,
                     const MemAccess &m, u32 next_pc) {
    // TODO(lab 5): the writeback stage, and the statistics.
    //
    // First, if the instruction writes a register, write it: writes_rd()
    // says whether it does, writeback_value() selects the ALU result or the
    // loaded value, and reg_write() keeps x0 zero.
    //
    // Second, update the statistics. Every instruction reaching this stage
    // has retired (cpu.stats->instructions); loads, stores, jumps and
    // branches each have a counter. This stage cannot see the branch
    // outcome, so infer `taken_branches` from next_pc != pc + 4. The
    // inference is wrong for exactly one encoding, deliberately (see the
    // handout).
    //
    // Finally, call trace_retire(), which writes the [OUT] line the tests
    // compare.
    (void)result;
    (void)next_pc;
    trace_retire(cpu, d, d.pc + 4, m);
}

// --- Shared bits of the run loops -----------------------------------------

void processor_illegal(const Processor &cpu, const Decoded &d) {
    fatal("illegal instruction 0x%08x at 0x%08x, after %ld instructions. "
          "Either the program jumped somewhere that is not code, or it uses an "
          "instruction this simulator does not implement.",
          d.raw, d.pc, cpu.stats->instructions);
}

int cycle_cost_serial(int fetch_latency, int mem_latency) {
    int cost = fetch_latency + mem_latency;
    return (cost < 1) ? 1 : cost;
}

int cycle_cost(const Processor &cpu, int fetch_latency, int mem_latency) {
    if (cpu.cfg->latency_overlap != OVERLAP_MAX) {
        return cycle_cost_serial(fetch_latency, mem_latency);
    }
    int cost = (fetch_latency > mem_latency) ? fetch_latency : mem_latency;
    return (cost < 1) ? 1 : cost;
}

Latch latch_bubble(void) {
    Latch l;
    memset(&l, 0, sizeof(l));
    l.valid = false;
    l.d     = decode_none();
    return l;
}

Latch latch_from_fetch(const Fetched &f) {
    Latch l = latch_bubble();
    l.valid = true;
    l.d.raw = f.raw;
    l.d.pc  = f.pc;
    return l;
}

Latch latch_from_id(const Decoded &d, u32 v_rs1, u32 v_rs2) {
    Latch l = latch_bubble();
    l.valid = true;
    l.d     = d;
    l.v_rs1 = v_rs1;
    l.v_rs2 = v_rs2;
    return l;
}

// One stage of the pipeline dump: its PC, or dashes when empty.
static void stage_field(char *buf, size_t n, const Latch &l) {
    if (l.valid) {
        snprintf(buf, n, "%08x", l.d.pc);
    } else {
        snprintf(buf, n, "--------");
    }
}

void pipeline_dump(long cycle, const Latch &if_id, const Latch &id_ex,
                   const Latch &ex_mem, const Latch &mem_wb) {
    if (!log_debug_enabled()) {
        return;
    }
    char a[16], b[16], c[16], e[16];
    stage_field(a, sizeof(a), if_id);
    stage_field(b, sizeof(b), id_ex);
    stage_field(c, sizeof(c), ex_mem);
    stage_field(e, sizeof(e), mem_wb);
    log_debug("cycle %-6ld IF/ID %s  ID/EX %s  EX/MEM %s  MEM/WB %s",
              cycle, a, b, c, e);
}

void processor_dump(const Processor &cpu) {
    if (!log_debug_enabled()) {
        return;
    }
    log_debug("pc = 0x%08x", cpu.pc);
    for (int i = 0; i < 32; i += 4) {
        log_debug("  %-4s %08x  %-4s %08x  %-4s %08x  %-4s %08x",
                  reg_name(i), cpu.regs[i], reg_name(i + 1), cpu.regs[i + 1],
                  reg_name(i + 2), cpu.regs[i + 2], reg_name(i + 3), cpu.regs[i + 3]);
    }
}
