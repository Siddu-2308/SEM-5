// A complete machine, small enough to build inside a test.
//
// A run loop cannot be tested one stage at a time, since its function is the
// sequencing of the stages; a test must run it. It is therefore given a program
// of a few instructions and a halt reachable in one, so that an incorrect count
// can be traced by hand.

#ifndef CS3160_TEST_BENCH_H
#define CS3160_TEST_BENCH_H

#include "encode.h"
#include "processor.h"
#include "ram.h"

static const u32 BASE   = 0x80000000u;
static const u32 SIZE   = 0x10000u;
static const u32 TOHOST = 0x80001000u;
static const u32 DATA   = 0x80002000u;

// x5 holds the tohost address before every run, so that a program can stop in
// one instruction (HALT, below) rather than first building the address.
static const int TOHOST_REG = 5;

struct Bench {
    Config    cfg;
    Stats     st;
    Ram       ram;
    Processor cpu;

    Bench() {
        config_defaults(cfg);
        cfg.tohost = TOHOST;
        stats_init(st);
        ram_init(ram, BASE, SIZE, 0);
        processor_init(cpu, cfg, st, ram, ram);
        cpu.regs[TOHOST_REG] = TOHOST;
    }

    // Place a program at the reset address. Its last word should be HALT, or
    // execution continues into the zeroes that follow.
    void load(const u32 *words, int n) {
        for (int i = 0; i < n; i++) {
            ram_write(ram, BASE + (u32)(4 * i), words[i], 4);
        }
    }

    u32 reg(int n) const { return reg_read(cpu, n); }
};

// A store to tohost, which ends a program.
static inline u32 HALT(void) { return SW(0, TOHOST_REG, 0); }

#define LOAD(bench, ...)                                     \
    do {                                                     \
        const u32 prog__[] = {__VA_ARGS__};                  \
        (bench).load(prog__, (int)(sizeof(prog__) / 4));     \
    } while (0)

#endif  // CS3160_TEST_BENCH_H
