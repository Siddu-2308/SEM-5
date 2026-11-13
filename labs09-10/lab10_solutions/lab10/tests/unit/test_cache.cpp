// The write path and the hierarchy, one function at a time.
//
// A whole-program run tests these only as totals. Here each is tested on two or
// three addresses, with a RAM small enough to inspect afterwards.

#include "check.h"

#include "cache.h"
#include "ram.h"

static const u32 BASE = 0x80000000u;
static const u32 SIZE = 0x10000u;

static const int CACHE_LATENCY = 2;
static const int RAM_LATENCY   = 100;

static CacheConfig cfg_of(const char *name, int size, int block_size, int assoc,
                          Mapping mapping, Replacement repl,
                          WritePolicy wp = WP_WRITEBACK,
                          WriteMiss wm = WM_ALLOCATE) {
    CacheConfig c;
    c.valid        = true;
    c.name         = name;
    c.latency      = CACHE_LATENCY;
    c.size         = size;
    c.block_size   = block_size;
    c.assoc        = assoc;
    c.mapping      = mapping;
    c.replacement  = repl;
    c.write_policy = wp;
    c.write_miss   = wm;
    return c;
}

// --- Writing into a cache --------------------------------------------------

TEST(a_write_miss_with_allocate_brings_the_block_in_first) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    for (u32 i = 0; i < 16; i++) {
        ram_write_byte(ram, BASE + i, 0xFF);
    }

    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.write(BASE + 4, 0xAAAAAAAAu, 4, &latency);
    CHECK_EQ(c.stats.writes, 1);
    CHECK_EQ(c.stats.write_misses, 1);
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);  // the block was fetched

    // Writing four bytes must preserve the other twelve, which hold what the
    // RAM had.
    CHECK_EQ(c.read(BASE, 4, &latency), 0xFFFFFFFFu);
    CHECK_EQ(c.stats.read_hits, 1);
    CHECK_EQ(c.read(BASE + 4, 4, &latency), 0xAAAAAAAAu);
}

TEST(a_write_to_a_block_that_is_here_is_a_hit) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.read(BASE, 4, &latency);                    // brings the block in
    c.write(BASE + 8, 0x12345678u, 4, &latency);

    CHECK_EQ(c.stats.write_hits, 1);
    CHECK_EQ(c.stats.write_misses, 0);
    CHECK_EQ(latency, CACHE_LATENCY);             // the level below is not accessed
    CHECK_EQ(c.read(BASE + 8, 4, &latency), 0x12345678u);
}

TEST(a_sub_word_write_keeps_the_rest_of_the_block) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    for (u32 i = 0; i < 16; i++) {
        ram_write_byte(ram, BASE + i, 0xFF);
    }

    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.write(BASE + 1, 0x00u, 1, &latency);
    CHECK_EQ(c.read(BASE, 4, &latency), 0xFFFF00FFu);

    c.write(BASE + 2, 0x1234u, 2, &latency);      // little-endian, two bytes
    CHECK_EQ(c.read(BASE, 4, &latency), 0x123400FFu);
    CHECK_EQ(c.stats.write_hits, 1);              // the second one hit
}

// --- The two write policies ------------------------------------------------

TEST(write_back_defers_the_traffic_until_the_block_is_displaced) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO, WP_WRITEBACK), &ram);

    int latency = 0;
    c.write(BASE, 0xAAAAAAAAu, 4, &latency);
    CHECK_EQ(ram.stats.writes, 0);                 // nothing has gone down yet
    CHECK_EQ(ram_read(ram, BASE, 4), 0u);

    // Further writes to the block hit and are not passed down.
    c.write(BASE + 4, 0xBBBBBBBBu, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY);
    CHECK_EQ(c.stats.write_hits, 1);
    CHECK_EQ(ram.stats.writes, 0);

    // Displacing the dirty block writes it back, both words, since a block is
    // transferred whole.
    c.read(BASE + 0x40, 4, &latency);
    CHECK_EQ(c.stats.writebacks, 1);
    CHECK_EQ(c.stats.evictions, 1);
    CHECK_EQ(ram.stats.writes, 1);
    CHECK_EQ(ram_read(ram, BASE, 4), 0xAAAAAAAAu);
    CHECK_EQ(ram_read(ram, BASE + 4, 4), 0xBBBBBBBBu);
    // The miss cost the writeback as well as the fetch.
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY + RAM_LATENCY);
}

TEST(write_through_sends_every_write_down_and_leaves_nothing_dirty) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO, WP_WRITETHROUGH),
               &ram);

    int latency = 0;
    c.write(BASE, 0xAAAAAAAAu, 4, &latency);
    CHECK_EQ(ram_read(ram, BASE, 4), 0xAAAAAAAAu);   // already there

    // A write hit is still passed down, at its cost.
    c.write(BASE + 4, 0xBBBBBBBBu, 4, &latency);
    CHECK_EQ(c.stats.write_hits, 1);
    CHECK_EQ(ram_read(ram, BASE + 4, 4), 0xBBBBBBBBu);
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);

    // The block remains present and readable but is not dirty, so displacing it
    // costs nothing extra.
    CHECK_EQ(c.read(BASE, 4, &latency), 0xAAAAAAAAu);
    CHECK_EQ(c.stats.read_hits, 1);
    c.read(BASE + 0x40, 4, &latency);
    CHECK_EQ(c.stats.evictions, 1);
    CHECK_EQ(c.stats.writebacks, 0);
}

TEST(no_write_allocate_leaves_a_block_that_is_not_here_alone) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO, WP_WRITEBACK,
                         WM_NOALLOCATE), &ram);

    int latency = 0;
    c.write(BASE, 0xAAAAAAAAu, 4, &latency);
    CHECK_EQ(c.stats.write_misses, 1);
    CHECK_EQ(ram_read(ram, BASE, 4), 0xAAAAAAAAu);   // written past the cache
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);

    // The block is absent: the next read of it misses.
    c.read(BASE, 4, &latency);
    CHECK_EQ(c.stats.read_misses, 1);

    // Once the block is present, a write to it hits and is not passed down.
    c.write(BASE + 4, 0xCCCCCCCCu, 4, &latency);
    CHECK_EQ(c.stats.write_hits, 1);
    CHECK_EQ(ram_read(ram, BASE + 4, 4), 0u);
}

// --- Writing back a dirty block --------------------------------------------

TEST(a_clean_block_is_never_written_down) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.read(BASE, 4, &latency);                    // clean: a copy of the level below
    CacheAddr a = cache_split(c, BASE);
    int way = cache_find(c, a.set, a.tag);
    CHECK(way >= 0);

    long writes_before = ram.stats.writes;
    int  cost = 0;
    CHECK(!cache_writeback_if_dirty(c, a.set, way, &cost));
    CHECK_EQ(cost, 0);
    CHECK_EQ(ram.stats.writes, writes_before);

    // A dirty block is written back, and the function reports it.
    c.write(BASE, 0x1234u, 4, &latency);
    CHECK(cache_writeback_if_dirty(c, a.set, way, &cost));
    CHECK_EQ(cost, RAM_LATENCY);
    CHECK_EQ(ram_read(ram, BASE, 4), 0x1234u);
}

TEST(a_block_written_down_is_clean_and_is_not_written_down_twice) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.write(BASE, 0xDEADBEEFu, 4, &latency);
    CacheAddr a = cache_split(c, BASE);
    int way = cache_find(c, a.set, a.tag);
    CHECK(way >= 0);

    int cost = 0;
    CHECK(cache_writeback_if_dirty(c, a.set, way, &cost));
    CHECK_EQ(ram.stats.writes, 1);

    // The copy below is now current, so a second call writes nothing.
    CHECK(!cache_writeback_if_dirty(c, a.set, way, &cost));
    CHECK_EQ(ram.stats.writes, 1);
    // The block remains present.
    CHECK_EQ(c.read(BASE, 4, &latency), 0xDEADBEEFu);
    CHECK_EQ(c.stats.read_hits, 1);
}

// --- Flushing at the end of a run ------------------------------------------

TEST(flush_makes_the_memory_below_current) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.write(BASE, 0x11111111u, 4, &latency);
    c.write(BASE + 0x10, 0x22222222u, 4, &latency);   // a different set
    CHECK_EQ(ram_read(ram, BASE, 4), 0u);             // neither has gone down

    cache_flush(c);
    CHECK_EQ(ram_read(ram, BASE, 4), 0x11111111u);
    CHECK_EQ(ram_read(ram, BASE + 0x10, 4), 0x22222222u);
}

TEST(flush_moves_no_figure_a_student_reports) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    c.write(BASE, 0x11111111u, 4, &latency);
    long writebacks_before = c.stats.writebacks;
    long evictions_before  = c.stats.evictions;

    cache_flush(c);
    CHECK_EQ(ram_read(ram, BASE, 4), 0x11111111u);
    // The flush makes the final state visible; it is not program traffic.
    CHECK_EQ(c.stats.writebacks, writebacks_before);
    CHECK_EQ(c.stats.evictions, evictions_before);

    // A second flush writes nothing: the blocks are now clean.
    long ram_writes = ram.stats.writes;
    cache_flush(c);
    CHECK_EQ(ram.stats.writes, ram_writes);
}

// --- One cache below another -----------------------------------------------

TEST(a_read_block_is_answered_from_this_level_when_it_can_be) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    for (u32 i = 0; i < 16; i++) {
        ram_write_byte(ram, BASE + i, (u8)(0x40 + i));
    }
    Cache c;
    cache_init(c, cfg_of("L2", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    u8  block[16];
    int latency = 0;
    c.read_block(BASE, block, 16, &latency);
    CHECK_EQ(c.stats.read_misses, 1);
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);
    for (int i = 0; i < 16; i++) {
        CHECK_EQ(block[i], 0x40 + i);
    }

    // A second request is served by this level, which kept the block.
    block[0] = 0;
    c.read_block(BASE, block, 16, &latency);
    CHECK_EQ(c.stats.read_hits, 1);
    CHECK_EQ(latency, CACHE_LATENCY);
    CHECK_EQ(ram.stats.reads, 1);
    CHECK_EQ(block[0], 0x40);
}

TEST(a_write_block_does_not_read_what_it_is_about_to_overwrite) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("L2", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    u8 block[16];
    for (int i = 0; i < 16; i++) {
        block[i] = (u8)(0x90 + i);
    }

    int latency = 0;
    c.write_block(BASE, block, 16, &latency);
    CHECK_EQ(c.stats.write_misses, 1);
    // The whole block is written, so there is nothing to preserve or fetch: the
    // level below is not accessed.
    CHECK_EQ(ram.stats.reads, 0);
    CHECK_EQ(latency, CACHE_LATENCY);

    // The block is present and dirty, and reads back as written.
    CHECK_EQ(c.read(BASE + 4, 4, &latency), 0x97969594u);
    CHECK_EQ(c.stats.read_hits, 1);
    CHECK_EQ(ram_read(ram, BASE, 4), 0u);   // still not written down
}

TEST(a_hierarchy_charges_each_level_an_access_passes_through) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    Cache l2;
    cache_init(l2, cfg_of("L2", 1024, 64, 4, MAP_SET, REPL_LRU), &ram);
    Cache l1;
    cache_init(l1, cfg_of("L1", 256, 64, 1, MAP_DIRECT, REPL_FIFO), &l2);

    int latency = 0;
    l1.read(BASE, 4, &latency);
    // A miss in both: L1 lookup, L2 lookup, then the RAM fetch.
    CHECK_EQ(latency, CACHE_LATENCY + CACHE_LATENCY + RAM_LATENCY);
    CHECK_EQ(l1.stats.read_misses, 1);
    CHECK_EQ(l2.stats.read_misses, 1);
    CHECK_EQ(ram.stats.reads, 1);

    // An L1 hit costs L1's latency alone; the levels below are not accessed.
    l1.read(BASE + 4, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY);
    CHECK_EQ(l2.stats.reads, 1);

    // Evict the block from L1. L2 still holds it, so the refill stops there.
    l1.read(BASE + 0x100, 4, &latency);   // same L1 set, different tag
    l1.read(BASE, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY + CACHE_LATENCY);
    CHECK_EQ(l2.stats.read_hits, 1);
    CHECK_EQ(ram.stats.reads, 2);         // only the two distinct blocks
}

TEST(a_dirty_block_leaving_l1_lands_in_l2_and_not_in_the_ram) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    Cache l2;
    cache_init(l2, cfg_of("L2", 1024, 64, 4, MAP_SET, REPL_LRU), &ram);
    Cache l1;
    cache_init(l1, cfg_of("L1", 256, 64, 1, MAP_DIRECT, REPL_FIFO), &l2);

    int latency = 0;
    l1.write(BASE, 0xDEADBEEFu, 4, &latency);
    long ram_writes = ram.stats.writes;

    l1.read(BASE + 0x100, 4, &latency);   // displaces the dirty block
    CHECK_EQ(l1.stats.writebacks, 1);
    CHECK_EQ(l2.stats.writes, 1);
    CHECK_EQ(ram.stats.writes, ram_writes);   // L2 is a memory too

    // ... and reading it back finds L2's copy, holding the written value.
    CHECK_EQ(l1.read(BASE, 4, &latency), 0xDEADBEEFu);
    CHECK_EQ(l2.stats.read_hits, 1);
}

TEST(a_flush_of_a_hierarchy_has_to_go_from_the_top_down) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    Cache l2;
    cache_init(l2, cfg_of("L2", 1024, 64, 4, MAP_SET, REPL_LRU), &ram);
    Cache l1;
    cache_init(l1, cfg_of("L1", 256, 64, 1, MAP_DIRECT, REPL_FIFO), &l2);

    int latency = 0;
    l1.write(BASE, 0x5A5A5A5Au, 4, &latency);

    // L1 holds the only current copy, so flushing L2 first would write a stale
    // block to the RAM, to be corrected by L1's flush.
    cache_flush(l1);
    cache_flush(l2);
    CHECK_EQ(ram_read(ram, BASE, 4), 0x5A5A5A5Au);
}

// --- Average memory access time --------------------------------------------

TEST(amat_of_one_level_is_its_hit_time_plus_what_its_misses_cost) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    // No accesses yet, so no misses and no penalty. The RAM cannot miss, so its
    // AMAT is its latency.
    CHECK_NEAR(level_amat(c.stats), (double)CACHE_LATENCY, 1e-9);
    CHECK_NEAR(level_amat(ram.stats), (double)RAM_LATENCY, 1e-9);

    // Four accesses to one block: one miss, three hits.
    int latency = 0;
    for (int i = 0; i < 16; i += 4) {
        c.read(BASE + (u32)i, 4, &latency);
    }
    CHECK_EQ(c.stats.read_misses, 1);
    CHECK_NEAR(level_amat(c.stats), CACHE_LATENCY + 0.25 * RAM_LATENCY, 1e-9);
}

TEST(amat_asks_the_same_question_of_the_level_below) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache l2;
    cache_init(l2, cfg_of("L2", 1024, 64, 4, MAP_SET, REPL_LRU), &ram);
    Cache l1;
    cache_init(l1, cfg_of("L1", 256, 64, 1, MAP_DIRECT, REPL_FIFO), &l2);

    // Two accesses to one block: L1 misses once and hits once, and its miss
    // also misses in L2. L1 misses 1 of 2, L2 1 of 1.
    int latency = 0;
    l1.read(BASE, 4, &latency);
    l1.read(BASE + 4, 4, &latency);

    double l2_amat = CACHE_LATENCY + 1.0 * RAM_LATENCY;
    CHECK_NEAR(level_amat(l2.stats), l2_amat, 1e-9);
    CHECK_NEAR(level_amat(l1.stats), CACHE_LATENCY + 0.5 * l2_amat, 1e-9);

    // That is 53. The two accesses cost 2 + 2 + 100 and 2, a mean of 53, so
    // here the derived and measured figures agree. They do not always; see
    // level_amat() in stats.h.
    CHECK_NEAR(level_amat(l1.stats), 53.0, 1e-9);
}

TEST(amat_of_a_write_through_cache_is_the_figure_that_lies) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 256, 16, 1, MAP_DIRECT, REPL_FIFO, WP_WRITETHROUGH),
               &ram);

    // One miss to fetch the block, then three write hits.
    int latency = 0;
    c.read(BASE, 4, &latency);
    for (int i = 0; i < 3; i++) {
        c.write(BASE + 4 * (u32)i, 0x11u, 4, &latency);
    }
    CHECK_EQ(c.stats.write_hits, 3);
    CHECK_EQ(c.stats.write_misses, 0);

    // The formula sees one miss in four accesses and gives 27 cycles.
    CHECK_NEAR(level_amat(c.stats), CACHE_LATENCY + 0.25 * RAM_LATENCY, 1e-9);

    // The actual cost is 102 for the miss and 102 for each write passed
    // through, a mean of 102: a write-through hit also pays the level below,
    // which "hit time + miss rate x miss penalty" does not capture.
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);
}
