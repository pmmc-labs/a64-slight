// /adder (die) -- replies to one ^add, then stops.
// If `die` is set, it stops *without* replying.

#include "../actor.h"

MODULE adder, adder_init, 1, 1
TAG    adder, add, 0, 2                 // (x, y) -> replies x + y

FUNC adder_init                         // x0 = self, x1 = die
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x27, x1                        // die
    YIELD Ladd_loop

Ladd_loop:
    RECV
    SWITCH 1, Ladd_drop, Ladd_table
Ladd_table:
    .word Ladd_add - Ladd_table         // ^add
Ladd_drop:
    MSG_DROP
    b    Ladd_loop

Ladd_add:
    MSG_ACCEPT                          // x1 = x, x2 = y
    cbnz x27, Ladd_die
    add  x1, x1, x2
    REPLY 1
    MSG_DONE
    STOP

Ladd_die:
    STOP                                // holding the request, never replying
