// The cache, one function at a time: address decomposition, placement, lookup
// and replacement.
//
// A whole-program run tests all of these at once, as a single hit rate. Here
// each is tested on a few addresses chosen so that the expected result can be
// computed by hand.

#include "check.h"

#include "cache.h"
#include "ram.h"

static const u32 BASE = 0x80000000u;
static const u32 SIZE = 0x10000u;

static const int CACHE_LATENCY = 2;
static const int RAM_LATENCY   = 100;

static CacheConfig cfg_of(const char *name, int size, int block_size, int assoc,
                          Mapping mapping, Replacement repl) {
    CacheConfig c;
    c.valid        = true;
    c.name         = name;
    c.latency      = CACHE_LATENCY;
    c.size         = size;
    c.block_size   = block_size;
    c.assoc        = assoc;
    c.mapping      = mapping;
    c.replacement  = repl;
    c.write_policy = WP_WRITEBACK;
    c.write_miss   = WM_ALLOCATE;
    return c;
}

// The replacement list, read safely. front() and back() of an empty vector are
// undefined, and an unimplemented cache_order_insert() leaves it empty; reading
// past its end could crash the suite.
static int order_front(const Cache &c, u32 set) {
    return c.order[set].empty() ? -1 : c.order[set].front();
}

static int order_back(const Cache &c, u32 set) {
    return c.order[set].empty() ? -1 : c.order[set].back();
}

// --- The geometry ----------------------------------------------------------

TEST(geometry_direct_mapped_is_one_block_per_set) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    // 64 bytes in 16-byte blocks is 4 blocks; direct-mapped, each is a set.
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);
    CHECK_EQ(c.assoc, 1);
    CHECK_EQ(c.n_sets, 4);
    CHECK_EQ(cache_n_blocks(c), 4);
}

TEST(geometry_set_associative_divides_the_blocks_into_sets) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    Cache two_way;
    cache_init(two_way, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);
    CHECK_EQ(two_way.assoc, 2);
    CHECK_EQ(two_way.n_sets, 2);

    // The same storage, four ways per set: one set.
    Cache four_way;
    cache_init(four_way, cfg_of("S", 64, 16, 4, MAP_SET, REPL_LRU), &ram);
    CHECK_EQ(four_way.assoc, 4);
    CHECK_EQ(four_way.n_sets, 1);
    CHECK_EQ(cache_n_blocks(four_way), 4);
}

TEST(geometry_fully_associative_is_one_set_of_everything) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    // `assoc` is ignored: fully associative means every block in one set.
    Cache c;
    cache_init(c, cfg_of("F", 256, 16, 1, MAP_FULL, REPL_LRU), &ram);
    CHECK_EQ(c.n_sets, 1);
    CHECK_EQ(c.assoc, 16);
    CHECK_EQ(cache_n_blocks(c), 16);
}

// --- Splitting an address --------------------------------------------------

TEST(split_cuts_an_address_by_the_geometry) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    // 4 blocks of 16 bytes, direct-mapped: 4 offset bits, 2 set bits, so the
    // set is bits 5:4 and the tag is everything from bit 6 up.
    Cache direct;
    cache_init(direct, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    CacheAddr a = cache_split(direct, BASE + 0x25);
    CHECK_EQ(a.offset, 0x5u);            // 0x25 & 0xf
    CHECK_EQ(a.set, 2u);                 // (0x25 >> 4) & 3
    CHECK_EQ(a.tag, (BASE + 0x25) >> 6);

    // Every byte of one block has the same set and tag and a different offset.
    for (u32 i = 0; i < 16; i++) {
        CacheAddr b = cache_split(direct, BASE + 0x20 + i);
        CHECK_EQ(b.offset, i);
        CHECK_EQ(b.set, 2u);
        CHECK_EQ(b.tag, a.tag);
    }
}

TEST(split_uses_fewer_set_bits_as_the_associativity_rises) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    // Two ways to a set halves the number of sets, so one set bit: bit 4.
    Cache two_way;
    cache_init(two_way, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);
    CHECK_EQ(cache_split(two_way, BASE + 0x25).set, 0u);   // (0x25 >> 4) & 1
    CHECK_EQ(cache_split(two_way, BASE + 0x15).set, 1u);
    CHECK_EQ(cache_split(two_way, BASE + 0x35).set, 1u);

    // The bit removed from the set number is now part of the tag, so addresses
    // that were in different sets now differ in their tags.
    CHECK(cache_split(two_way, BASE + 0x05).tag
          != cache_split(two_way, BASE + 0x25).tag);
}

TEST(split_of_a_fully_associative_cache_has_no_set_bits) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("F", 64, 16, 1, MAP_FULL, REPL_LRU), &ram);

    // One set, so log2(1) = 0 set bits: every address is in set 0 and the tag
    // is the whole block address. Shifting a u32 by 32 is undefined in C++, so
    // a general expression fails in this case.
    for (u32 off = 0; off < 0x100; off += 0x10) {
        CacheAddr a = cache_split(c, BASE + off);
        CHECK_EQ(a.set, 0u);
        CHECK_EQ(a.tag, (BASE + off) >> 4);
        CHECK_EQ(a.offset, 0u);
    }
}

TEST(block_address_is_the_split_run_backwards) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    Cache direct;
    cache_init(direct, cfg_of("D", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);
    Cache full;
    cache_init(full, cfg_of("F", 256, 16, 1, MAP_FULL, REPL_LRU), &ram);

    // The calculation an eviction needs: the writeback address is reconstructed
    // from the block's tag and set alone.
    for (u32 addr = BASE; addr < BASE + 0x200; addr += 4) {
        CacheAddr a = cache_split(direct, addr);
        CHECK_EQ(cache_block_addr(direct, a.set, a.tag), addr & ~0xFu);

        CacheAddr f = cache_split(full, addr);
        CHECK_EQ(cache_block_addr(full, f.set, f.tag), addr & ~0xFu);
    }
}

// --- The lookup ------------------------------------------------------------

TEST(find_says_no_until_the_block_is_here_and_then_says_where) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);

    CacheAddr a = cache_split(c, BASE);
    CHECK_EQ(cache_find(c, a.set, a.tag), -1);   // nothing is here yet

    int latency = 0;
    int way = cache_fill(c, BASE, &latency);
    CHECK(way >= 0);
    CHECK(way < c.assoc);
    CHECK_EQ(cache_find(c, a.set, a.tag), way);  // and now it is, in that way

    // A different tag in the same set is still not here.
    CacheAddr other = cache_split(c, BASE + 0x40);
    CHECK_EQ(other.set, a.set);
    CHECK_EQ(cache_find(c, other.set, other.tag), -1);
}

TEST(find_ignores_a_matching_tag_in_an_invalid_way) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    // A block record starts with tag 0, and may later hold any tag; only
    // `valid` makes the tag meaningful.
    CacheBlock &b = cache_block_at(c, 1, 0);
    b.tag   = 7;
    b.valid = false;
    CHECK_EQ(cache_find(c, 1, 7), -1);

    b.valid = true;
    CHECK_EQ(cache_find(c, 1, 7), 0);
}

// --- Hits, misses and what they cost ---------------------------------------

TEST(the_first_access_misses_and_the_rest_of_the_block_hits) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    for (u32 i = 0; i < 64; i++) {
        ram_write_byte(ram, BASE + i, (u8)i);
    }

    Cache c;
    cache_init(c, cfg_of("D", 256, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = -1;
    CHECK_EQ(c.read(BASE, 4, &latency), 0x03020100u);
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);  // lookup, then the fetch
    CHECK_EQ(c.stats.read_misses, 1);

    // The other three words of the same block came with it.
    for (int i = 4; i < 16; i += 4) {
        CHECK_EQ(c.read(BASE + i, 4, &latency), 0x03020100u + 0x04040404u * (u32)(i / 4));
        CHECK_EQ(latency, CACHE_LATENCY);
    }
    CHECK_EQ(c.stats.read_hits, 3);
    CHECK_EQ(c.stats.read_misses, 1);
    CHECK_EQ(level_accesses(c.stats), 4);

    // The next block is a new miss, and the RAM has supplied exactly two blocks,
    // not eight words.
    c.read(BASE + 16, 4, &latency);
    CHECK_EQ(c.stats.read_misses, 2);
    CHECK_EQ(ram.stats.reads, 2);
}

TEST(a_partial_read_takes_only_its_own_bytes) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    for (u32 i = 0; i < 16; i++) {
        ram_write_byte(ram, BASE + i, (u8)(0x10 + i));
    }
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    CHECK_EQ(c.read(BASE + 2, 1, &latency), 0x12u);
    CHECK_EQ(c.read(BASE + 2, 2, &latency), 0x1312u);   // little-endian
    CHECK_EQ(c.read(BASE + 12, 4, &latency), 0x1F1E1D1Cu);
    CHECK_EQ(c.stats.read_misses, 1);
    CHECK_EQ(c.stats.read_hits, 2);
}

// --- Mapping, measured -----------------------------------------------------

TEST(direct_mapped_conflicts_thrash_however_small_the_working_set) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    // 4 blocks of 16 bytes, direct-mapped: addresses 0x40 apart land in the
    // same set and cannot both be held, even though only two blocks are in use.
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    int latency = 0;
    for (int i = 0; i < 4; i++) {
        c.read(BASE, 4, &latency);
        c.read(BASE + 0x40, 4, &latency);
    }
    CHECK_EQ(c.stats.read_hits, 0);       // never a hit: each evicts the other
    CHECK_EQ(c.stats.read_misses, 8);
    CHECK_EQ(c.stats.evictions, 7);       // the very first miss displaced nothing
}

TEST(two_ways_hold_both_of_the_conflicting_blocks) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    // The same total storage as the test above, arranged two to a set.
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);

    int latency = 0;
    for (int i = 0; i < 4; i++) {
        c.read(BASE, 4, &latency);
        c.read(BASE + 0x40, 4, &latency);
    }
    CHECK_EQ(c.stats.read_misses, 2);     // compulsory, and no more
    CHECK_EQ(c.stats.read_hits, 6);
    CHECK_EQ(c.stats.evictions, 0);
}

TEST(fully_associative_uses_every_block_before_it_evicts_anything) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("F", 64, 16, 1, MAP_FULL, REPL_LRU), &ram);

    // Four blocks 0x40 apart: direct-mapped they would all collide in one set.
    int latency = 0;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 4; i++) {
            c.read(BASE + (u32)i * 0x40u, 4, &latency);
        }
    }
    CHECK_EQ(c.stats.read_misses, 4);
    CHECK_EQ(c.stats.read_hits, 8);
    CHECK_EQ(c.stats.evictions, 0);
}

// --- Replacement -----------------------------------------------------------

TEST(an_empty_way_is_taken_before_anything_is_thrown_out) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);

    // Both ways of set 0 are free, so the first two fills evict nothing and must
    // use different ways.
    int latency = 0;
    int first  = cache_fill(c, BASE, &latency);
    int second = cache_fill(c, BASE + 0x20, &latency);
    CHECK(first != second);
    CHECK_EQ(c.stats.evictions, 0);

    // With the set full, the third fill must displace one of them.
    cache_fill(c, BASE + 0x40, &latency);
    CHECK_EQ(c.stats.evictions, 1);
}

TEST(order_insert_moves_a_way_to_the_back_and_leaves_it_there_once) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 4, MAP_SET, REPL_LRU), &ram);

    cache_order_insert(c, 0, 2);
    cache_order_insert(c, 0, 0);
    cache_order_insert(c, 0, 1);
    CHECK_EQ(c.order[0].size(), 3u);
    CHECK_EQ(order_front(c, 0), 2);    // the oldest arrival is the next to go

    // Inserting a way already in the list moves it; a duplicate would make the
    // set appear fuller than it is.
    cache_order_insert(c, 0, 2);
    CHECK_EQ(c.order[0].size(), 3u);
    CHECK_EQ(order_front(c, 0), 0);
    CHECK_EQ(order_back(c, 0), 2);
}

TEST(order_touch_is_the_whole_difference_between_the_two_policies) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);

    Cache lru;
    cache_init(lru, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);
    Cache fifo;
    cache_init(fifo, cfg_of("S", 64, 16, 2, MAP_SET, REPL_FIFO), &ram);

    for (Cache *c : {&lru, &fifo}) {
        // Both ways are valid, so the choice is made by the policy rather than
        // by a free way.
        cache_block_at(*c, 0, 0).valid = true;
        cache_block_at(*c, 0, 1).valid = true;
        cache_order_insert(*c, 0, 0);
        cache_order_insert(*c, 0, 1);
        CHECK_EQ(order_front(*c, 0), 0);
        cache_order_touch(*c, 0, 0);       // way 0 is used again
    }

    // Under LRU way 1 is now the least recently used; under FIFO way 0 is still
    // the oldest arrival.
    CHECK_EQ(order_front(lru, 0), 1);
    CHECK_EQ(order_front(fifo, 0), 0);

    // cache_choose_way() reads this order, so the two caches now choose
    // different victims.
    CHECK_EQ(cache_choose_way(lru, 0), 1);
    CHECK_EQ(cache_choose_way(fifo, 0), 0);
}

// The scenario that distinguishes the two policies through Cache::read(). In a
// two-way set: load A, load B, use A again, then load C.
//   FIFO evicts A, because A arrived first and using it changes nothing.
//   LRU  evicts B, because A has been used more recently.
static void replacement_scenario(Cache &c, u32 a, u32 b, u32 d) {
    int latency = 0;
    c.read(a, 4, &latency);
    c.read(b, 4, &latency);
    c.read(a, 4, &latency);  // A is used again
    c.read(d, 4, &latency);  // a block must now be evicted
}

TEST(fifo_throws_out_what_arrived_first_however_recently_it_was_used) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 2, MAP_SET, REPL_FIFO), &ram);

    // Three addresses in the same set, 0x20 apart with one set bit at bit 4.
    replacement_scenario(c, BASE, BASE + 0x20, BASE + 0x40);

    // B first: a miss would itself evict a block, so the block expected to
    // remain is checked before the one expected to be gone.
    int latency = 0;
    c.read(BASE + 0x20, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY);                // B is still here
    c.read(BASE, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);  // A was thrown out
}

TEST(lru_keeps_what_was_used_and_throws_out_the_other) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);

    replacement_scenario(c, BASE, BASE + 0x20, BASE + 0x40);

    int latency = 0;
    c.read(BASE, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY);                // A survived, having been used
    c.read(BASE + 0x20, 4, &latency);
    CHECK_EQ(latency, CACHE_LATENCY + RAM_LATENCY);  // B went instead
}

// --- What a fill leaves behind ---------------------------------------------

TEST(a_fill_records_the_tag_and_joins_the_replacement_order) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    for (u32 i = 0; i < 16; i++) {
        ram_write_byte(ram, BASE + 0x40 + i, (u8)(0xA0 + i));
    }
    Cache c;
    cache_init(c, cfg_of("S", 64, 16, 2, MAP_SET, REPL_LRU), &ram);

    int latency = 0;
    int way = cache_fill(c, BASE + 0x40, &latency);
    CacheAddr a = cache_split(c, BASE + 0x40);

    CacheBlock &b = cache_block_at(c, a.set, way);
    CHECK(b.valid);
    CHECK(!b.dirty);                 // a fresh copy of what is below
    CHECK_EQ(b.tag, a.tag);
    CHECK_EQ(latency, RAM_LATENCY);  // the fetch alone: the lookup is the caller's

    // The bytes were copied, at the correct offsets.
    const u8 *bytes = cache_block_data(c, a.set, way);
    for (int i = 0; i < 16; i++) {
        CHECK_EQ(bytes[i], 0xA0 + i);
    }

    // The replacement order records it as the newest, so with the other way
    // still free it is not the next to be replaced.
    CHECK_EQ(order_back(c, a.set), way);
}

TEST(an_eviction_is_only_counted_when_something_was_really_there) {
    Ram ram;
    ram_init(ram, BASE, SIZE, RAM_LATENCY);
    Cache c;
    cache_init(c, cfg_of("D", 64, 16, 1, MAP_DIRECT, REPL_FIFO), &ram);

    // Four distinct blocks into four empty sets: nothing is displaced.
    int latency = 0;
    for (int i = 0; i < 4; i++) {
        c.read(BASE + (u32)i * 0x10u, 4, &latency);
    }
    CHECK_EQ(c.stats.read_misses, 4);
    CHECK_EQ(c.stats.evictions, 0);

    // A block that conflicts with the first: an eviction, reusing the same way.
    c.read(BASE + 0x40, 4, &latency);
    CHECK_EQ(c.stats.evictions, 1);
    CHECK_EQ(cache_find(c, 0, cache_split(c, BASE).tag), -1);
    CHECK_EQ(cache_find(c, 0, cache_split(c, BASE + 0x40).tag), 0);
}
