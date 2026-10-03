// actor.h -- assembler macros for writing actors by hand.
//
// These are the ops from docs/CALLING_CONVENTIONS.md, spelled for AArch64.
// Include it from a .S file (it goes through the C preprocessor first).
// Needs clang's integrated assembler (Apple's `cc`, or clang on Linux).
//
// Register convention for actor code
// ----------------------------------
//   x28        self (the process). Set by the runtime, never written by actors.
//   x19..x27   the actor's own. Preserved by every op, *including the ones
//              that suspend*: this is where state lives across an AWAIT.
//   x0         the control value, in and out: pid, pending, ok flag, envelope
//   x1..x7     the payload: args 0..6 going out (SPAWN, SEND, REQUEST, REPLY)
//              and coming in (.init, MSG_ACCEPT, AWAIT). Arg i is in x(i+1).
//   x9, x10    an op's static operands (module, tag, reply tag); set by the macros
//   x11        argc; set by the macros
//   everything else is caller-saved and clobbered by any op, as in AAPCS64.
//
// Every op is an ordinary `bl` into the runtime. The ones that can suspend
// (RECV, AWAIT, REDUCE) just return later; nothing is unwound.

#ifndef ACTOR_H
#define ACTOR_H

#include "rt.h"

// --- plumbing ---------------------------------------------------------------

.macro LOADADDR reg, sym
#if defined(__APPLE__)
    adrp \reg, \sym@PAGE
    add  \reg, \reg, \sym@PAGEOFF
#else
    adrp \reg, \sym
    add  \reg, \reg, :lo12:\sym
#endif
.endm

.macro FUNC name
    .text
    .p2align 2
    .globl \name
\name:
.endm

// --- module header ----------------------------------------------------------
//
//   MODULE adder, adder_init, 1, 1        name, .init, spawn arity, tag count
//   TAG    adder, add, 0, 2               module, name, index, arity
//
// Emits `mod_<module>` and one `tag_<module>_<name>` per tag. TAGs must come
// straight after their MODULE, in index order (they form the tag table).

.macro MODULE name, init, arity, ntags
    .data
    .p2align 3
    .globl mod_\name
mod_\name:
    .quad   modname_\name
    .quad   \init
    .quad   \arity
    .quad   \ntags
    .quad   tags_\name
tags_\name:
    .text
modname_\name:
    .asciz  "/\name"
    .data
.endm

.macro TAG module, name, index, arity
    .data
    .p2align 3
    .globl tag_\module\()_\name
tag_\module\()_\name:
    .quad   mod_\module
    .quad   \index
    .quad   \arity
    .quad   tagname_\module\()_\name
    .text
tagname_\module\()_\name:
    .asciz  "\name"
    .data
.endm

// --- processes --------------------------------------------------------------

// SPAWN mod_x, argc        x1.. = args             -> x0 = pid
.macro SPAWN mod, argc
    LOADADDR x9, \mod
    mov  x11, #\argc
    bl   rt_spawn
.endm

// YIELD label              the commit point: no live MESSAGE frame, and sp
//                          back where it was at the first YIELD. Resets the
//                          mailbox cursor and jumps to the receive loop.
.macro YIELD loop
    mov  x0, sp
    bl   rt_yield
    b    \loop
.endm

// STOP                     never returns
.macro STOP
    bl   rt_stop
.endm

// REDUCE                   put one at every loop back-edge: counts down this
//                          activation's quota and preempts when it runs out.
//                          Clobbers caller-saved registers, like any op.
.macro REDUCE
    ldr  x9, [x28, #RT_PROC_REDUCTIONS]
    subs x9, x9, #1
    str  x9, [x28, #RT_PROC_REDUCTIONS]
    b.gt 1f
    bl   rt_preempt
1:
.endm

// --- messages ---------------------------------------------------------------

// SEND tag_x, argc         x0 = pid, x1.. = args
.macro SEND tag, argc
    LOADADDR x9, \tag
    mov  x11, #\argc
    bl   rt_send
.endm

// RECV                     -> x0 = tag index of the message under the cursor
//                          (suspends until there is one)
.macro RECV
    bl   rt_recv
.endm

// SWITCH count, default, table
//                          jump on x0 through a table of `.word Lx - table`
//                          entries, one per tag index; out of range -> default
.macro SWITCH count, default, table
    cmp   x0, #\count
    b.hs  \default
    adr   x9, \table
    ldrsw x10, [x9, x0, lsl #2]
    add   x9, x9, x10
    br    x9
.endm

// MSG_ACCEPT               -> x0 = envelope, x1.. = args (a MESSAGE frame)
.macro MSG_ACCEPT
    bl   rt_msg_accept
.endm

.macro MSG_SKIP
    bl   rt_msg_skip
.endm

.macro MSG_DROP
    bl   rt_msg_drop
.endm

// MSG_DONE                 pops the MESSAGE frame
.macro MSG_DONE
    bl   rt_msg_done
.endm

// MSG_SENDER               -> x0 = pid of the nearest MESSAGE frame's sender
.macro MSG_SENDER
    bl   rt_msg_sender
.endm

// --- async calls ------------------------------------------------------------

// REQUEST tag_x, tag_reply, argc
//                          x0 = pid, x1.. = args   -> x0 = pending
.macro REQUEST tag, reply, argc
    LOADADDR x9, \tag
    LOADADDR x10, \reply
    mov  x11, #\argc
    bl   rt_request
.endm

// AWAIT tag_reply, failed  x0 = pending            -> x1.. = reply values
//                          or branch to `failed` if the callee stopped
//                          without replying
.macro AWAIT reply, failed
    LOADADDR x9, \reply
    bl   rt_await
    cbz  x0, \failed
.endm

// REPLY argc               x1.. = values, sent to the reply address of the
//                          nearest MESSAGE frame
.macro REPLY argc
    mov  x11, #\argc
    bl   rt_reply
.endm

// --- output -----------------------------------------------------------------

.macro SAY_INT                // x0 = value
    bl   rt_say
.endm

.macro SAY_STR label
    adr  x0, \label
    bl   rt_say_str
.endm

#endif // ACTOR_H
