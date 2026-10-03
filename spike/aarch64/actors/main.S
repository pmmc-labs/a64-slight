// /main (x, y, die) -- asks a /multiplier for x * y, and forges a ^product
// at it while it works. The forgery has the right tag but no ref, so the
// multiplier's AWAIT never mistakes it for its child's reply. (In AVM, where
// a plain RECV did the waiting, one stray message turned 4 * 10 into 1003.)

#include "../actor.h"

MODULE main, main_init, 3, 1
TAG    main, result, 0, 1

FUNC main_init                          // x0 = self, x1 = x, x2 = y, x3 = die
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x19, x1                        // x
    mov  x20, x2                        // y

    mov  x1, x3                         // die
    SPAWN mod_multiplier, 1             // x0 = m
    mov  x21, x0

    mov  x1, x19
    mov  x2, x20
    REQUEST tag_multiplier_mul, tag_main_result, 2     // x0 = pending
    mov  x22, x0

    mov  x0, x21
    mov  x1, #999
    SEND tag_multiplier_product, 1      // the forgery

    mov  x0, x22
    AWAIT tag_main_result, Lmain_failed // x1 = x * y
    mov  x0, x1
    SAY_INT
    STOP

Lmain_failed:
    SAY_STR Lmain_died
    STOP

Lmain_died:
    .asciz "the multiplier died"
