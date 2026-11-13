// The RISC-V encodings needed to build a program inside a test.
//
// The pipeline tests need real instruction words, since a hand-built Decoded
// would bypass the decode stage under test. Five formats suffice.
//
// The encodings are written out from the ISA specification rather than taken
// from the simulator: a test whose input came from the code under test would
// agree with it even if both were wrong.

#ifndef CS3160_TEST_ENCODE_H
#define CS3160_TEST_ENCODE_H

#include "common.h"

static inline u32 r_type(u32 f7, int rs2, int rs1, u32 f3, int rd, u32 op) {
    return (f7 << 25) | ((u32)rs2 << 20) | ((u32)rs1 << 15) | (f3 << 12) |
           ((u32)rd << 7) | op;
}

static inline u32 i_type(i32 imm, int rs1, u32 f3, int rd, u32 op) {
    return (((u32)imm & 0xFFFu) << 20) | ((u32)rs1 << 15) | (f3 << 12) |
           ((u32)rd << 7) | op;
}

static inline u32 s_type(i32 imm, int rs2, int rs1, u32 f3, u32 op) {
    u32 u = (u32)imm;
    return (((u >> 5) & 0x7Fu) << 25) | ((u32)rs2 << 20) | ((u32)rs1 << 15) |
           (f3 << 12) | ((u & 0x1Fu) << 7) | op;
}

// B-format: the immediate is in units of 2 bytes, with its bits spread over
// four fields, which makes hand-assembly of a branch error-prone.
static inline u32 b_type(i32 imm, int rs2, int rs1, u32 f3, u32 op) {
    u32 u = (u32)imm;
    return (((u >> 12) & 1u) << 31) | (((u >> 5) & 0x3Fu) << 25) |
           ((u32)rs2 << 20) | ((u32)rs1 << 15) | (f3 << 12) |
           (((u >> 1) & 0xFu) << 8) | (((u >> 11) & 1u) << 7) | op;
}

static inline u32 j_type(i32 imm, int rd, u32 op) {
    u32 u = (u32)imm;
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xFFu) << 12) |
           ((u32)rd << 7) | op;
}

static inline u32 ADDI(int rd, int rs1, i32 imm) { return i_type(imm, rs1, 0, rd, 0x13u); }
static inline u32 ADD (int rd, int rs1, int rs2) { return r_type(0, rs2, rs1, 0, rd, 0x33u); }
static inline u32 SUB (int rd, int rs1, int rs2) { return r_type(0x20u, rs2, rs1, 0, rd, 0x33u); }
static inline u32 LW  (int rd, int rs1, i32 imm) { return i_type(imm, rs1, 2, rd, 0x03u); }
static inline u32 SW  (int rs2, int rs1, i32 imm) { return s_type(imm, rs2, rs1, 2, 0x23u); }
static inline u32 BEQ (int rs1, int rs2, i32 imm) { return b_type(imm, rs2, rs1, 0, 0x63u); }
static inline u32 BNE (int rs1, int rs2, i32 imm) { return b_type(imm, rs2, rs1, 1, 0x63u); }
static inline u32 JAL (int rd, i32 imm)           { return j_type(imm, rd, 0x6Fu); }
static inline u32 NOP (void)                      { return ADDI(0, 0, 0); }

#endif  // CS3160_TEST_ENCODE_H
