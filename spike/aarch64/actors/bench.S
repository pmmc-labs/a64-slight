// Benchmarks: REQUEST/AWAIT round trips, and the cost of an idle process.

#include "../actor.h"

// /bench (n) -- n round trips against a /pong, then stops it.
MODULE bench, bench_init, 1, 1
TAG    bench, pong, 0, 1

FUNC bench_init                         // x0 = self, x1 = n
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x19, x1                        // n
    mov  x20, #0                        // i
    SPAWN mod_pong, 0
    mov  x21, x0                        // pong

Lbench_loop:
    cmp  x20, x19
    b.hs Lbench_done
    mov  x0, x21
    mov  x1, x20
    REQUEST tag_pong_ping, tag_bench_pong, 1
    AWAIT   tag_bench_pong, Lbench_failed                       // x1 = i, echoed
    cmp  x1, x20
    b.ne Lbench_failed
    add  x20, x20, #1
    REDUCE
    b    Lbench_loop

Lbench_done:
    mov  x0, x21
    SEND tag_pong_stop, 0
    STOP

Lbench_failed:
    SAY_STR Lbench_msg
    STOP
Lbench_msg:
    .asciz "bench failed"

// /pong -- echoes every ^ping back to its reply address, until ^stop.
MODULE pong, pong_init, 0, 2
TAG    pong, ping, 0, 1
TAG    pong, stop, 1, 0

FUNC pong_init
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    YIELD Lpong_loop

Lpong_loop:
    RECV
    SWITCH 2, Lpong_drop, Lpong_table
Lpong_table:
    .word Lpong_ping - Lpong_table
    .word Lpong_stop - Lpong_table
Lpong_drop:
    MSG_DROP
    b    Lpong_loop

Lpong_ping:
    MSG_ACCEPT                          // x1 = n
    REPLY 1                             // straight back
    MSG_DONE
    YIELD Lpong_loop

Lpong_stop:
    MSG_ACCEPT
    MSG_DONE
    STOP

// /idle -- runs to its RECV and stays there.
MODULE idle, idle_init, 0, 0

FUNC idle_init
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    YIELD Lidle_loop
Lidle_loop:
    RECV
    MSG_DROP
    b    Lidle_loop
