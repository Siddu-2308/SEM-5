# RISC-V registers and instructions: quick reference

The lab has no internet access. `RISCV_CARD.pdf` in this folder lists every
instruction; this file covers the registers, a worked example, and the most
frequently consulted details.

## The 32 registers

RISC-V has 32 registers, `x0` to `x31`, each 32 bits wide.

**In this lab, use register numbers (`x5`, `x11`, `x28`).** Disassembly shows
alternative names such as `t0`, `a0` and `sp`. These *ABI names* belong to the
calling convention introduced in Lab 2 and have no meaning to the hardware:
`x10` and `a0` encode identically.

| Register    | Reserved for                         | Use in Lab 1         |
|-------------|--------------------------------------|----------------------|
| `x0`        | Always reads 0; writes are discarded | yes (see below)      |
| `x1`        | Return address                       | **no**               |
| `x2`        | Stack pointer                        | **no**               |
| `x3`        | Global pointer                       | **no**               |
| `x4`        | Thread pointer                       | **no**               |
| `x5`-`x7`   | --                                   | yes                  |
| `x8`-`x9`   | (callee-saved; Lab 2)                | avoid                |
| `x10`       | Exit status when `main` returns      | yes, at the end      |
| `x11`-`x17` | --                                   | yes                  |
| `x18`-`x27` | (callee-saved; Lab 2)                | avoid                |
| `x28`-`x31` | --                                   | yes                  |

**`x5`-`x7`, `x11`-`x17` and `x28`-`x31` are free for use**: fourteen
registers, sufficient for these problems.

### Why `x1` to `x4` must not be modified

The start-up code initialises these registers before calling `main`, and
modifying them breaks the program. `x1` holds the address to which `ret`
returns; if it is overwritten, `ret` may jump into the data section, or
elsewhere in RAM, and execute its contents as instructions. The tests report
this as *"program crashed"*.

### Uses of `x0`

Since `x0` always reads zero and discards writes, several common operations are
expressed through it:

```asm
    add  x5, x6, x0     # copy x6 into x5
    beq  x5, x0, label  # branch if x5 is zero
    sw   x0, 0(x6)      # store zero to memory
```

## Worked example: summing an array

The standard loop structure: advance a pointer through an array one element at
a time until a counter reaches zero.

```asm
.section .data
        .align 2
n:      .word 5                 # how many elements
list:   .word 4, 8, 15, 16, 23  # the elements themselves
total:  .word 0                 # where the answer goes

.section .text
        .globl main
main:
        la   x5, list           # x5 = ADDRESS of the first element
        lw   x6, n              # x6 = how many are left to visit
        li   x7, 0              # x7 = running total, starts at zero

loop:
        beqz x6, done           # nothing left? then we are finished
        lw   x28, 0(x5)         # x28 = the element x5 currently points at
        add  x7, x7, x28        # add it to the running total

        addi x5, x5, 4          # advance to the next element: +4 BYTES
        addi x6, x6, -1         # one fewer left to visit
        j    loop

done:
        la   x28, total         # x28 = address of `total`
        sw   x7, 0(x28)         # write the answer into memory
        li   x10, 0             # exit status 0
        ret                     # return -- this stops the machine
```

**`la` and `lw`.** `la x5, list` loads the *address* of `list`; `lw x6, n`
loads the *contents* of `n`.

```asm
    la   x5, n          # x5 = the ADDRESS of n
    lw   x6, 0(x5)      # x6 = the CONTENTS at that address
    lw   x6, n          # shorthand for both lines above
```

**`0(x5)` denotes the memory at the address in `x5`.** The number is a byte
offset: `4(x5)` is the following word. Loads and stores are the only
instructions that access memory; arithmetic instructions operate on registers
only.

**Stepping by 4.** Each `.word` occupies four bytes, so element `i` is at
`base + 4*i`; incrementing a pointer by 1 addresses the middle of a word.
Advance by 4 as above, or compute the offset with a shift:

```asm
    slli x29, x28, 2    # x29 = x28 * 4   (shift left 2 = multiply by 4)
    add  x29, x5, x29   # x29 = &list[x28]
```

**Storing the result.** The tests read the result from *memory*, so a value
computed in a register must be stored with `sw`.

## Pseudo-instructions

The assembler expands each of these into one or two machine instructions;
`programs/dumps/*.dump` shows the expansion.

| Written          | Meaning                                  |
| ---------------- | ---------------------------------------- |
| `li rd, imm`     | Load a constant into `rd`                |
| `la rd, symbol`  | Load the **address** of a variable       |
| `mv rd, rs`      | Copy `rs` into `rd`                      |
| `ret`            | Return from a function                   |
| `j label`        | Jump                                     |
| `beqz rs, label` | Branch if `rs` is zero                   |
| `bnez rs, label` | Branch if `rs` is not zero               |
| `bltz rs, label` | Branch if `rs` is negative               |
| `bgt rs, rt, l`  | Branch if `rs > rt`                      |
| `ble rs, rt, l`  | Branch if `rs <= rt`                     |

## Declaring data

```asm
.section .data
        .align 2                # align to a 4-byte boundary
count:  .word 0                 # one 32-bit variable, initially 0
list:   .word 4, 8, 15, 16      # four consecutive words
        .fill 12, 4, 0          # 12 more words, each 4 bytes, each 0
```

`.fill count, size, value` reserves space for an array without listing every
entry. Do not change the declarations in the starter files: the tests
overwrite them with their own data and depend on their sizes.

## Arithmetic and comparison

- `div` gives the quotient and `rem` the remainder: `rem x28, x5, x6` computes
  `x5 % x6`.
- `slli rd, rs, 2` multiplies by 4, converting an array index to a byte offset.
- Comparisons are signed (`blt`) or unsigned (`bltu`). The arrays contain
  negative numbers, so use the signed forms; `bltu` treats -1 as a large
  positive number.
- There is no "branch if greater than" instruction: `bgt` is a
  pseudo-instruction that swaps the operands of `blt`.
