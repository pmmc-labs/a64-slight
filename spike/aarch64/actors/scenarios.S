// Smaller scenarios: a wrong tag, preemption, and a deadlock.

#include "../actor.h"

// ---------------------------------------------------------------------------
// /wrongtag -- a good REQUEST, then a SEND of /multiplier's tag to an /adder.
// The ownership check makes the *sender* fault.
// ---------------------------------------------------------------------------

MODULE wrongtag, wrongtag_init, 0, 1
TAG    wrongtag, sum, 0, 1

FUNC wrongtag_init
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x1, #0                         // die = no
    SPAWN mod_adder, 1                  // x0 = a
    mov  x19, x0
    mov  x1, #1
    mov  x2, #2
    REQUEST tag_adder_add, tag_wrongtag_sum, 2
    AWAIT   tag_wrongtag_sum, Lwt_failed                        // x1 = 3
    mov  x0, x1
    SAY_INT

    mov  x0, x19                        // the (now stopped) adder
    mov  x1, #4
    mov  x2, #5
    SEND tag_multiplier_mul, 2          // faults: ^mul belongs to /multiplier
Lwt_failed:
    STOP

// ---------------------------------------------------------------------------
// /preempt (n) -- a /spinner burns n reductions in a tight loop, while we ask
// an /adder for 2 + 3. Without REDUCE the spinner would hold the CPU until it
// finished; with it, the answer comes back first.
// ---------------------------------------------------------------------------

MODULE preempt, preempt_init, 1, 1
TAG    preempt, sum, 0, 1

FUNC preempt_init                       // x0 = self, x1 = n
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    SPAWN mod_spinner, 1                // x1 = n is already in place
    mov  x1, #0                         // die = no
    SPAWN mod_adder, 1                  // x0 = a
    mov  x1, #2
    mov  x2, #3
    REQUEST tag_adder_add, tag_preempt_sum, 2
    AWAIT   tag_preempt_sum, Lpre_failed                         // x1 = 5
    mov  x0, x1
    SAY_INT
Lpre_failed:
    STOP

MODULE spinner, spinner_init, 1, 0

FUNC spinner_init                       // x0 = self, x1 = n
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x19, x1
Lspin:
    cbz  x19, Lspin_done
    sub  x19, x19, #1
    REDUCE
    b    Lspin
Lspin_done:
    SAY_STR Lspin_msg
    STOP
Lspin_msg:
    .asciz "spinner done"

// ---------------------------------------------------------------------------
// /deadlock -- starts two /dl processes; A asks B, and B, to answer, first
// asks A, who is busy AWAITing B. Nothing is READY, the bus is empty, and
// the scheduler reports a deadlock instead of spinning.
// ---------------------------------------------------------------------------

MODULE deadlock, deadlock_init, 0, 0

FUNC deadlock_init
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    SPAWN mod_dl, 0
    mov  x19, x0                        // A
    SPAWN mod_dl, 0
    mov  x1, x0                         // B
    mov  x0, x19
    SEND tag_dl_start, 1                // A ! ^start(B)
    STOP

MODULE dl, dl_init, 0, 3
TAG    dl, start,  0, 1                 // (peer)
TAG    dl, ask,    1, 0
TAG    dl, answer, 2, 0

FUNC dl_init
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    YIELD Ldl_loop

Ldl_loop:
    RECV
    SWITCH 3, Ldl_drop, Ldl_table
Ldl_table:
    .word Ldl_start - Ldl_table
    .word Ldl_ask   - Ldl_table
    .word Ldl_drop  - Ldl_table
Ldl_drop:
    MSG_DROP
    b    Ldl_loop

Ldl_start:                              // ask the peer
    MSG_ACCEPT                          // x1 = peer
    mov  x0, x1
    REQUEST tag_dl_ask, tag_dl_answer, 0
    AWAIT   tag_dl_answer, Ldl_failed
    MSG_DONE
    YIELD Ldl_loop

Ldl_ask:                                // to answer, first ask them back
    MSG_ACCEPT
    MSG_SENDER                          // x0 = whoever asked
    REQUEST tag_dl_ask, tag_dl_answer, 0
    AWAIT   tag_dl_answer, Ldl_failed
    REPLY 0
    MSG_DONE
    YIELD Ldl_loop

Ldl_failed:
    STOP
