#include "alu.h"

#include "decode.h"
#include "log.h"

// The most negative 32-bit number. Negating it overflows, the one case signed
// division must handle explicitly.
static const u32 INT32_MIN_BITS = 0x80000000u;

AluInputs alu_inputs(const Decoded &d, u32 v_rs1, u32 v_rs2) {
    AluInputs in;
    in.a = 0;
    in.b = 0;

    switch (d.op) {
        // Register-register: both operands come from the register file.
        case OP_ADD: case OP_SUB: case OP_SLL: case OP_SLT: case OP_SLTU:
        case OP_XOR: case OP_SRL: case OP_SRA: case OP_OR: case OP_AND:
        case OP_MUL: case OP_MULH: case OP_MULHSU: case OP_MULHU:
        case OP_DIV: case OP_DIVU: case OP_REM: case OP_REMU:
        // Branches compare two registers.
        case OP_BEQ: case OP_BNE: case OP_BLT:
        case OP_BGE: case OP_BLTU: case OP_BGEU:
            in.a = v_rs1;
            in.b = v_rs2;
            break;

        // Register-immediate, and the address calculations, which are the same
        // addition: base register plus a signed offset.
        case OP_ADDI: case OP_SLTI: case OP_SLTIU: case OP_XORI:
        case OP_ORI: case OP_ANDI: case OP_SLLI: case OP_SRLI: case OP_SRAI:
        case OP_LB: case OP_LH: case OP_LW: case OP_LBU: case OP_LHU:
        case OP_SB: case OP_SH: case OP_SW:
        case OP_JALR:
            in.a = v_rs1;
            in.b = d.imm;
            break;

        // lui produces its immediate and nothing else, so it adds it to zero.
        case OP_LUI:
            in.a = 0;
            in.b = d.imm;
            break;

        // auipc and jal are both "this instruction's address plus a constant".
        case OP_AUIPC:
        case OP_JAL:
            in.a = d.pc;
            in.b = d.imm;
            break;

        // No operands: the CSR instructions access registers not modelled
        // here, and the fences and environment calls produce no value.
        case OP_CSRRW: case OP_CSRRS: case OP_CSRRC:
        case OP_CSRRWI: case OP_CSRRSI: case OP_CSRRCI:
        case OP_FENCE: case OP_FENCE_I:
        case OP_ECALL: case OP_EBREAK: case OP_MRET:
        case OP_INVALID: case OP_COUNT:
            break;
    }
    return in;
}

// Shifts use only the low five bits of the amount: a 32-bit value can be
// shifted 0..31 places, and the specification ignores the upper bits.
static int shift_amount(u32 b) { return (int)(b & 0x1Fu); }

u32 alu_apply(Op op, u32 a, u32 b) {
    // TODO(lab 5): the ALU.
    //
    // Return the result of applying `op` to `a` and `b`. The operands were
    // selected by alu_inputs(): two registers for `add`, a register and the
    // immediate for `addi`, a base and an offset for loads, stores and
    // jumps. Arithmetic, address calculation and jump targets therefore all
    // reduce to this one switch.
    //
    // Implement OP_ADD and OP_SUB first and run `make test`; each unit test
    // names the operation it checks. Four cases need explicit handling
    // because RISC-V defines them and C++ does not: division and remainder
    // by zero, and the -2^31 / -1 overflow of each.
    //
    // For the shifts, use shift_amount() above: only the low five bits of
    // the amount are significant.
    (void)op;
    (void)a;
    (void)b;
    (void)shift_amount;
    switch (op) {
        case OP_ADD: case OP_ADDI: 
        case OP_LUI:case OP_AUIPC:case OP_JAL:case OP_JALR:
        case OP_LB: case OP_LH: case OP_LW: case OP_LBU: case OP_LHU:
        case OP_SB: case OP_SH: case OP_SW:
            return a+b;

        case OP_SUB:
        case OP_BEQ: case OP_BNE: case OP_BLT:
        case OP_BGE: case OP_BLTU: case OP_BGEU:
            return a-b;

        case OP_XOR: case OP_XORI: return a ^ b;
        case OP_OR:  case OP_ORI:  return a | b;
        case OP_AND: case OP_ANDI: return a & b;
        case OP_SLT: case OP_SLTI:
            return a < b ? 1u : 0u;
        case OP_SLTU: case OP_SLTIU:
            return a < b ? 1u : 0u;
        
    }
    return 0;
    
}

bool branch_taken(Op op, u32 a, u32 b) {
    // TODO(lab 5): is this branch taken?
    //
    // `a` and `b` are the two compared registers. Return true if control
    // goes to the branch target rather than the next instruction. blt/bge
    // compare signed values (see as_signed() in common.h), bltu/bgeu
    // unsigned values.
    (void)op;
    (void)a;
    (void)b;
    return false;
}
