// /multiplier (die) -- one multiplication per process, recursively:
//
//     x * y = x + (x * (y - 1))
//
// The "multiplier" worked example from docs/CALLING_CONVENTIONS.md. Note that
// x and y live in x19/x20 across both AWAITs: the process is suspended in the
// middle of its handler, with its registers and stack intact.
//
// `die` is passed down to every /adder; if it's set they stop without
// replying, which exercises the AWAIT failure path.

#include "../actor.h"

MODULE multiplier, multiplier_init, 1, 3
TAG    multiplier, mul,     0, 2        // (x, y) -> replies x * y
TAG    multiplier, product, 1, 1        // reply from a child /multiplier
TAG    multiplier, sum,     2, 1        // reply from an /adder

FUNC multiplier_init                    // x0 = self, x1 = die
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x27, x1                        // die, for the life of the process
    YIELD Lmul_loop

Lmul_loop:
    RECV                                // x0 = tag index
    SWITCH 3, Lmul_drop, Lmul_table
Lmul_table:
    .word Lmul_mul  - Lmul_table        // ^mul
    .word Lmul_drop - Lmul_table        // ^product: late, or forged
    .word Lmul_drop - Lmul_table        // ^sum:     late, or forged
Lmul_drop:
    MSG_DROP
    b    Lmul_loop

Lmul_mul:
    MSG_ACCEPT                          // x1 = x, x2 = y
    mov  x19, x1                        // x
    mov  x20, x2                        // y
    cbz  x20, Lmul_zero
    cmp  x20, #1
    b.eq Lmul_one

    mov  x1, x27                        // die
    SPAWN mod_multiplier, 1             // x0 = m
    mov  x1, x19                        // x
    sub  x2, x20, #1                    // y - 1
    REQUEST tag_multiplier_mul, tag_multiplier_product, 2      // x0 = pending
    AWAIT   tag_multiplier_product, Lmul_failed                // x1 = p
    mov  x21, x1                        // p

    mov  x1, x27                        // die
    SPAWN mod_adder, 1                  // x0 = a
    mov  x1, x21                        // p
    mov  x2, x19                        // x   (still here after the AWAIT)
    REQUEST tag_adder_add, tag_multiplier_sum, 2               // x0 = pending
    AWAIT   tag_multiplier_sum, Lmul_failed                    // x1 = s
    b    Lmul_reply

Lmul_zero:
    mov  x1, #0
    b    Lmul_reply
Lmul_one:
    mov  x1, x19
Lmul_reply:
    REPLY 1                             // x1 -> whatever reply tag our caller asked for
    MSG_DONE
    STOP                                // one multiplication per process

Lmul_failed:                            // a helper stopped without replying, so we
    STOP                                // stop too, and our caller's AWAIT fails
