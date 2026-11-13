#include "loader.h"

#include "log.h"

u32 loader_load(Ram &ram, const char *path, u32 load_addr) {
    // TODO(lab 3): copy a program image into memory.
    //
    // A .r5ob file is not an ELF: it holds exactly the bytes of memory
    // immediately before execution begins, in order, with no header or
    // trailer. Byte n of the file belongs at load_addr + n. Return the
    // number of bytes copied.
    //
    // Write through ram_write_byte(), not directly into ram.bytes, so that an
    // image too large for the RAM is reported rather than written past the
    // end of the array.
    //
    // If the file cannot be opened or read, call fatal() with its path.
    (void)ram;
    (void)path;
    (void)load_addr;
    return 0;
}
