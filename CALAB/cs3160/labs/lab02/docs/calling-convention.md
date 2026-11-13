# The RISC-V calling convention

## Purpose

The hardware has no notion of a function. No instruction saves variables,
there are no local variables, and nothing prevents one piece of code from
overwriting registers on which another depends.

These matters are settled by a *convention*: an agreement on where each value
is placed and who preserves it. Code that follows the convention can call, and
be called by, code whose source it has never seen. This is why the functions
in this lab are tested by a separately written driver.

## Register names

From this lab onwards, use ABI names, which state each register's role. `x10`
and `a0` denote the same register; write `a0`.

| Register    | ABI name  | Role                          | Preserved by     |
| ----------- | --------- | ----------------------------- | ---------------- |
| `x0`        | `zero`    | Always zero                   | --               |
| `x1`        | `ra`      | Return address                | caller           |
| `x2`        | `sp`      | Stack pointer                 | **callee**       |
| `x3`        | `gp`      | Global pointer                | do not modify    |
| `x4`        | `tp`      | Thread pointer                | do not modify    |
| `x5`-`x7`   | `t0`-`t2` | Temporary                     | caller           |
| `x8`        | `s0`/`fp` | Saved / frame pointer         | **callee**       |
| `x9`        | `s1`      | Saved                         | **callee**       |
| `x10`-`x11` | `a0`-`a1` | Arguments 1-2, return value   | caller           |
| `x12`-`x17` | `a2`-`a7` | Arguments 3-8                 | caller           |
| `x18`-`x27` | `s2`-`s11`| Saved                         | **callee**       |
| `x28`-`x31` | `t3`-`t6` | Temporary                     | caller           |

## Caller-saved and callee-saved registers

**Caller-saved (`t0`-`t6`, `a0`-`a7`, `ra`):** a called function may overwrite
these. A value needed after a call must be saved by the caller beforehand.

**Callee-saved (`s0`-`s11`, `sp`):** a called function must return these
unchanged. A function that uses `s0` must save its value on entry and restore
it before returning.

The `s` registers survive calls, which makes them suitable for values such as
a loop counter in a function that makes calls. They survive only because every
function restores them.

The tests verify this: they load a known value into every `s` register, call
the function, and check the values afterwards. A correct result obtained while
modifying `s0` is reported as `abi FAIL`, since it would corrupt the caller's
state.

## Leaf functions

A function that calls no other function is a *leaf*. Using only `t` and `a`
registers, it needs no stack frame:

```asm
        .globl double_it
double_it:
        add  a0, a0, a0         # argument in a0, result in a0
        ret                     # nothing to save, nothing to restore
```

## Non-leaf functions and the stack

`call` overwrites `ra` with its own return address, destroying the address at
which the function must return to its caller. A function that calls another
must therefore save `ra`.

It is saved on the stack. `sp` points to the lowest word in use; space is
allocated by *subtracting* from `sp` and released before returning.

```asm
        .globl f
f:
        addi sp, sp, -16        # make a 16-byte frame
        sw   ra, 12(sp)         # save the return address
        sw   s0, 8(sp)          # save s0, because we are about to use it

        mv   s0, a0             # keep our argument somewhere call-proof
        call g                  # this overwrites ra, a0-a7, t0-t6
        add  a0, a0, s0         # a0 is g's result. s0 still holds our argument

        lw   s0, 8(sp)          # put the caller's s0 back
        lw   ra, 12(sp)         # and the return address
        addi sp, sp, 16         # release the frame
        ret
```

The stack grows *downwards*. With higher addresses drawn at the top,
`addi sp, sp, -16` moves `sp` down:

```
          higher addresses
        +---------------------+
        |                     |
        |   caller's frame    |
        |                     |
        +=====================+  <--- sp when f was entered
        |   saved ra          |  12(sp)     sw ra, 12(sp)
        +---------------------+
        |   saved s0          |   8(sp)     sw s0, 8(sp)
        +---------------------+
        |   unused            |   4(sp)
        +---------------------+
        |   unused            |   0(sp)
        +=====================+  <--- sp inside f, after addi sp, sp, -16
        |                     |
        |   free stack        |
        v                     v
          lower addresses
```

**Offsets are measured upwards from the new `sp`.** `12(sp)` is the top slot
of the frame, twelve bytes above `sp`; hence `sw ra, 12(sp)` follows
`addi sp, sp, -16`.

**The frame is released by restoring `sp`**, with `addi sp, sp, 16`. Memory
below `sp` is free for any use, so a function that returns without restoring
`sp` leaves its caller's stack pointer invalid. This is why `sp` is
callee-saved and why the tests check it.

The frame size must be a multiple of 16, as the convention requires, although
these programs do not fail otherwise.

### Checklist

For every function:

1. If it calls any function, save and restore `ra`.
2. Save and restore each of `s0`-`s11` that it uses.
3. Ensure `sp` is restored to its value on entry.

Most errors in this lab violate one of these three rules.

## Recursion

Recursion requires nothing further: each call has its own frame, lower on the
stack. Its errors, however, become visible only as the calls return.

```asm
        .globl countdown
countdown:
        addi sp, sp, -16
        sw   ra, 12(sp)
        sw   s0, 8(sp)

        beqz a0, base           # base case: stop recursing
        mv   s0, a0             # remember n
        addi a0, a0, -1
        call countdown          # recurse
        mv   a0, s0             # n is still here, one frame down
base:
        lw   s0, 8(sp)
        lw   ra, 12(sp)
        addi sp, sp, 16
        ret
```

The code applies the same checklist: save `ra`, save `s0`, restore `sp`. What
recursion adds is that several frames exist simultaneously, one per active
call. `countdown(2)` builds the following:

```
          higher addresses
        +=====================+  <--- sp before the first call
        |  ra -> our caller   |     frame for countdown(2)
        |  s0 = 2             |
        +=====================+
        |  ra -> inside cd(2) |     frame for countdown(1)
        |  s0 = 1             |
        +=====================+
        |  ra -> inside cd(1) |     frame for countdown(0)
        |  s0 = (unused)      |     base case: does not recurse
        +=====================+  <--- sp at the deepest point
          lower addresses
```

Each call has its own `s0` slot, so its `n` is protected from the calls below
it. The frames are released in reverse order as the calls return, and `sp`
returns to its initial value.

If `ra` is not saved, the innermost call returns correctly but every outer
return jumps to the same incorrect address. The tests report *"crashed"* or
that the instruction limit was exceeded.

The base case must terminate the recursion: `fact(0)` must return without a
recursive call, or the stack grows until it overwrites the data.

## Calling a function

```asm
        la   a0, myarray        # first argument
        li   a1, 10             # second argument
        call myfunc             # jump, and put the return address in ra
        # result is now in a0
        # t0-t6 and a1-a7 may have been destroyed
```

`call` is a pseudo-instruction, shown in disassembly as `jal` or as an
`auipc`/`jalr` pair; `ret` is `jalr zero, 0(ra)`.
