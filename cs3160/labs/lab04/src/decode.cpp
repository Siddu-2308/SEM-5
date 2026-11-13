#include "decode.h"

#include "log.h"

// Opcodes, in the order they appear in the RISC-V manual's opcode map.
enum {
    OPC_LOAD   = 0x03,
    OPC_FENCE  = 0x0F,
    OPC_OPIMM  = 0x13,
    OPC_AUIPC  = 0x17,
    OPC_STORE  = 0x23,
    OPC_OP     = 0x33,
    OPC_LUI    = 0x37,
    OPC_BRANCH = 0x63,
    OPC_JALR   = 0x67,
    OPC_JAL    = 0x6F,
    OPC_SYSTEM = 0x73,
};

// --- Immediate extraction --------------------------------------------------
//
// One function per format, each gathering the immediate's bits from the
// positions that format uses and sign-extending the result. The layout keeps
// the sign bit at bit 31 and the register fields at fixed positions in every
// format, so that hardware can read the registers before the instruction has
// been fully decoded.

static u32 imm_i(u32 raw) {
    return sign_extend(bits(raw, 31, 20), 12);
}

static u32 imm_s(u32 raw) {
    u32 v = (bits(raw, 31, 25) << 5) | bits(raw, 11, 7);
    return sign_extend(v, 12);
}

static u32 imm_b(u32 raw) {
    u32 v = (bits(raw, 31, 31) << 12) | (bits(raw, 7, 7) << 11) |
            (bits(raw, 30, 25) << 5) | (bits(raw, 11, 8) << 1);
    return sign_extend(v, 13);  // bit 0 is not encoded: offsets are even
}

static u32 imm_u(u32 raw) {
    return raw & 0xFFFFF000u;  // already in place; the low 12 bits are zero
}

static u32 imm_j(u32 raw) {
    u32 v = (bits(raw, 31, 31) << 20) | (bits(raw, 19, 12) << 12) |
            (bits(raw, 20, 20) << 11) | (bits(raw, 30, 21) << 1);
    return sign_extend(v, 21);
}

// --- decode ----------------------------------------------------------------

Decoded decode_none(void) {
    Decoded d;
    d.raw = 0;
    d.pc  = 0;
    d.op  = OP_INVALID;
    d.fmt = FMT_NONE;
    d.rs1 = 0;
    d.rs2 = 0;
    d.rd  = 0;
    d.imm = 0;
    d.csr = 0;
    return d;
}

Decoded decode(u32 raw, u32 pc) {
    // TODO(lab 4): turn 32 bits into a Decoded.
    //
    // The opcode, in bits 6..0, determines the *format* (R, I, S, B, U or J),
    // and the format determines which other fields exist and where the
    // immediate lies. funct3 and funct7 distinguish instructions that share
    // an opcode.
    //
    // The immediate extractors imm_i, imm_s, imm_b, imm_u and imm_j are
    // provided at the top of this file. Each returns the immediate
    // **sign-extended into a u32**.
    //
    // Set d.op, d.fmt, d.rd, d.rs1, d.rs2 and d.imm; leave d.pc unchanged.
    // An unrecognised encoding yields OP_INVALID with FMT_NONE, not a crash,
    // so that the stage that would execute it can report a useful error.
    //
    // Proceed opcode by opcode, running `make test` after each; each unit
    // test is named after the instruction it decodes.
    (void)raw;
    (void)imm_i; (void)imm_s; (void)imm_b; (void)imm_u; (void)imm_j;
    Decoded d = decode_none();
    d.pc = pc;
    return d;
}

// --- Questions about an operation ------------------------------------------

bool is_branch(Op op) {
    switch (op) {
        case OP_BEQ: case OP_BNE: case OP_BLT:
        case OP_BGE: case OP_BLTU: case OP_BGEU:
            return true;
        default:
            return false;
    }
}

bool is_jump(Op op) {
    return op == OP_JAL || op == OP_JALR;
}

bool is_load(Op op) {
    switch (op) {
        case OP_LB: case OP_LH: case OP_LW: case OP_LBU: case OP_LHU:
            return true;
        default:
            return false;
    }
}

bool is_store(Op op) {
    return op == OP_SB || op == OP_SH || op == OP_SW;
}

bool is_csr(Op op) {
    switch (op) {
        case OP_CSRRW: case OP_CSRRS: case OP_CSRRC:
        case OP_CSRRWI: case OP_CSRRSI: case OP_CSRRCI:
            return true;
        default:
            return false;
    }
}

bool writes_rd(Op op) {
    if (op == OP_INVALID) {
        return false;
    }
    if (is_branch(op) || is_store(op)) {
        return false;
    }
    switch (op) {
        case OP_FENCE: case OP_FENCE_I:
        case OP_ECALL: case OP_EBREAK: case OP_MRET:
            return false;
        default:
            return true;
    }
}

bool reads_rs1(Op op) {
    if (op == OP_INVALID) {
        return false;
    }
    switch (op) {
        // U-format, J-format, the fences, the environment calls and the
        // immediate CSR forms take no register operand; all others do.
        case OP_LUI: case OP_AUIPC: case OP_JAL:
        case OP_FENCE: case OP_FENCE_I:
        case OP_ECALL: case OP_EBREAK: case OP_MRET:
        case OP_CSRRWI: case OP_CSRRSI: case OP_CSRRCI:
            return false;
        default:
            return true;
    }
}

bool reads_rs2(Op op) {
    // Only R-format instructions, stores and branches read rs2.
    if (is_branch(op) || is_store(op)) {
        return true;
    }
    switch (op) {
        case OP_ADD: case OP_SUB: case OP_SLL: case OP_SLT: case OP_SLTU:
        case OP_XOR: case OP_SRL: case OP_SRA: case OP_OR: case OP_AND:
        case OP_MUL: case OP_MULH: case OP_MULHSU: case OP_MULHU:
        case OP_DIV: case OP_DIVU: case OP_REM: case OP_REMU:
            return true;
        default:
            return false;
    }
}

int mem_size(Op op) {
    switch (op) {
        case OP_LB: case OP_LBU: case OP_SB: return 1;
        case OP_LH: case OP_LHU: case OP_SH: return 2;
        case OP_LW: case OP_SW:              return 4;
        default:                             return 0;
    }
}

bool mem_signed(Op op) {
    return op == OP_LB || op == OP_LH;
}

const char *op_name(Op op) {
    switch (op) {
        case OP_INVALID: return "?";
        case OP_ADD:     return "add";
        case OP_SUB:     return "sub";
        case OP_SLL:     return "sll";
        case OP_SLT:     return "slt";
        case OP_SLTU:    return "sltu";
        case OP_XOR:     return "xor";
        case OP_SRL:     return "srl";
        case OP_SRA:     return "sra";
        case OP_OR:      return "or";
        case OP_AND:     return "and";
        case OP_MUL:     return "mul";
        case OP_MULH:    return "mulh";
        case OP_MULHSU:  return "mulhsu";
        case OP_MULHU:   return "mulhu";
        case OP_DIV:     return "div";
        case OP_DIVU:    return "divu";
        case OP_REM:     return "rem";
        case OP_REMU:    return "remu";
        case OP_ADDI:    return "addi";
        case OP_SLTI:    return "slti";
        case OP_SLTIU:   return "sltiu";
        case OP_XORI:    return "xori";
        case OP_ORI:     return "ori";
        case OP_ANDI:    return "andi";
        case OP_SLLI:    return "slli";
        case OP_SRLI:    return "srli";
        case OP_SRAI:    return "srai";
        case OP_LUI:     return "lui";
        case OP_AUIPC:   return "auipc";
        case OP_JAL:     return "jal";
        case OP_JALR:    return "jalr";
        case OP_BEQ:     return "beq";
        case OP_BNE:     return "bne";
        case OP_BLT:     return "blt";
        case OP_BGE:     return "bge";
        case OP_BLTU:    return "bltu";
        case OP_BGEU:    return "bgeu";
        case OP_LB:      return "lb";
        case OP_LH:      return "lh";
        case OP_LW:      return "lw";
        case OP_LBU:     return "lbu";
        case OP_LHU:     return "lhu";
        case OP_SB:      return "sb";
        case OP_SH:      return "sh";
        case OP_SW:      return "sw";
        case OP_FENCE:   return "fence";
        case OP_FENCE_I: return "fence.i";
        case OP_ECALL:   return "ecall";
        case OP_EBREAK:  return "ebreak";
        case OP_MRET:    return "mret";
        case OP_CSRRW:   return "csrrw";
        case OP_CSRRS:   return "csrrs";
        case OP_CSRRC:   return "csrrc";
        case OP_CSRRWI:  return "csrrwi";
        case OP_CSRRSI:  return "csrrsi";
        case OP_CSRRCI:  return "csrrci";
        case OP_COUNT:   break;
    }
    return "?";
}
