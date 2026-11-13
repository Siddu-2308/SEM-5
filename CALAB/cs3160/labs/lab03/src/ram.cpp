#include "ram.h"

#include "log.h"

void ram_init(Ram &ram, u32 base, u32 size, int latency) {
    ram.base    = base;
    ram.size    = size;
    ram.latency = latency;
    ram.bytes.assign(size, 0);
    level_stats_init(ram.stats, "RAM", false, latency);
}

bool ram_in_range(const Ram &ram, u32 addr, int size) {
    if (addr < ram.base) {
        return false;
    }
    u32 offset = addr - ram.base;
    // Compare against the size rather than computing offset + size, which
    // could wrap for an address near the top of the address space.
    return offset < ram.size && (u32)size <= ram.size - offset;
}

// The single place an out-of-range access is reported, so that the message is
// the same for every kind of access.
static void check_range(const Ram &ram, u32 addr, int size, const char *what) {
    if (!ram_in_range(ram, addr, size)) {
        fatal("%s of %d byte(s) at 0x%08x is outside RAM [0x%08x, 0x%08x)",
              what, size, addr, ram.base, ram.base + ram.size);
    }
}

u8 ram_read_byte(Ram &ram, u32 addr) {
    check_range(ram, addr, 1, "read");
    return ram.bytes[addr - ram.base];
}

void ram_write_byte(Ram &ram, u32 addr, u8 value) {
    check_range(ram, addr, 1, "write");
    ram.bytes[addr - ram.base] = value;
}

u32 ram_read(Ram &ram, u32 addr, int size) {
    // TODO(lab 3): read `size` bytes (1, 2 or 4) starting at `addr` and
    // return them as one word.
    //
    // Memory is the byte array ram.bytes, and `addr` is a machine address,
    // so the array index is addr - ram.base (as in ram_read_byte() above).
    //
    // RISC-V is **little-endian**: the byte at the lowest address is the
    // least significant. Reading four bytes at 0x1000 therefore gives
    //     bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24
    //
    // A read narrower than a word is zero-extended into the returned u32.
    //
    // check_range() is called first, so that an address outside the RAM is
    // reported by name rather than causing a segmentation fault.
    check_range(ram, addr, size, "read");
    (void)size;
    return 0;
}

void ram_write(Ram &ram, u32 addr, u32 data, int size) {
    // TODO(lab 3): write the low `size` bytes of `data` at `addr`.
    //
    // The inverse of ram_read(): least significant byte at the lowest
    // address. Write only `size` bytes; an `sb` into the middle of a word
    // must leave the other three bytes unchanged (see the sub-word tests in
    // test_ram.cpp).
    check_range(ram, addr, size, "write");
    (void)data;
    (void)size;
}

void ram_dump(Ram &ram, u32 start, u32 end, FILE *out) {
    // TODO(lab 3): print the words from `start` up to (but not including)
    // `end`, one per line:
    //
    //     0x80000000: 00000297
    //     0x80000004: 01028293
    //
    // That is, the address, a colon and a space, then the word as eight hex
    // digits with leading zeros: exactly "0x%08x: %08x\n", lower-case. The
    // tests compare the output character by character.
    //
    // Round `start` down to a word boundary first, so that the addresses
    // are aligned whatever range is requested.
    (void)ram;
    (void)start;
    (void)end;
    (void)out;
}

// --- MemoryLevel interface -------------------------------------------------
//
// RAM is the lowest level of the hierarchy: it always holds the data, so its
// latency is the entire cost of an access.

u32 Ram::read(u32 addr, int size, int *latency) {
    stats.reads++;
    *latency = this->latency;
    return ram_read(*this, addr, size);
}

void Ram::write(u32 addr, u32 data, int size, int *latency) {
    stats.writes++;
    *latency = this->latency;
    ram_write(*this, addr, data, size);
}

void Ram::read_block(u32 addr, u8 *dst, int block_size, int *latency) {
    stats.reads++;
    *latency = this->latency;

    u32 start = addr & ~((u32)block_size - 1);
    check_range(*this, start, block_size, "block read");
    for (int i = 0; i < block_size; i++) {
        dst[i] = bytes[start - base + i];
    }
}

void Ram::write_block(u32 addr, const u8 *src, int block_size, int *latency) {
    stats.writes++;
    *latency = this->latency;

    u32 start = addr & ~((u32)block_size - 1);
    check_range(*this, start, block_size, "block write");
    for (int i = 0; i < block_size; i++) {
        bytes[start - base + i] = src[i];
    }
}
