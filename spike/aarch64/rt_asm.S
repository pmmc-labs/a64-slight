// rt_asm.S -- the parts of the runtime that have to be assembly.
//
//   rt_switch      the context switch: this is what suspension *is*
//   rt_trampoline  first activation of a process: calls .init(self, args...)
//   rt_spawn, rt_send, rt_request, rt_await, rt_reply, rt_msg_accept
//                  stubs that turn the actor register convention (x0 control,
//                  x1..x7 payload, x9/x10 static operands, x11 argc) into
//                  ordinary AAPCS64 calls into rt.c, and back

#include "actor.h"

// ---------------------------------------------------------------------------
// void rt_switch(ctx_t *from, ctx_t *to)
//
// Save the callee-saved state of whoever is running into `from`, load `to`,
// and `ret` into it. Everything else is caller-saved under AAPCS64, so the
// code that called rt_switch (directly, or via any runtime op) has already
// given it up. x18 is never touched: it belongs to the platform on macOS.
// ---------------------------------------------------------------------------

FUNC rt_switch
    stp  x19, x20, [x0, #RT_CTX_X19 +  0]
    stp  x21, x22, [x0, #RT_CTX_X19 + 16]
    stp  x23, x24, [x0, #RT_CTX_X19 + 32]
    stp  x25, x26, [x0, #RT_CTX_X19 + 48]
    stp  x27, x28, [x0, #RT_CTX_X19 + 64]
    stp  x29, x30, [x0, #RT_CTX_FP]
    mov  x9, sp
    str  x9,       [x0, #RT_CTX_SP]
    stp  d8,  d9,  [x0, #RT_CTX_D8 +  0]
    stp  d10, d11, [x0, #RT_CTX_D8 + 16]
    stp  d12, d13, [x0, #RT_CTX_D8 + 32]
    stp  d14, d15, [x0, #RT_CTX_D8 + 48]

    ldp  x19, x20, [x1, #RT_CTX_X19 +  0]
    ldp  x21, x22, [x1, #RT_CTX_X19 + 16]
    ldp  x23, x24, [x1, #RT_CTX_X19 + 32]
    ldp  x25, x26, [x1, #RT_CTX_X19 + 48]
    ldp  x27, x28, [x1, #RT_CTX_X19 + 64]
    ldp  x29, x30, [x1, #RT_CTX_FP]
    ldr  x9,       [x1, #RT_CTX_SP]
    mov  sp, x9
    ldp  d8,  d9,  [x1, #RT_CTX_D8 +  0]
    ldp  d10, d11, [x1, #RT_CTX_D8 + 16]
    ldp  d12, d13, [x1, #RT_CTX_D8 + 32]
    ldp  d14, d15, [x1, #RT_CTX_D8 + 48]
    ret

// ---------------------------------------------------------------------------
// rt_trampoline -- where a new process's first rt_switch "returns" to.
// spawn() sets up its context with x19 = .init, x28 = self, sp = stack top.
// ---------------------------------------------------------------------------

FUNC rt_trampoline
    ldr  x0,     [x28, #RT_PROC_PID]
    ldp  x1, x2, [x28, #RT_PROC_ARGS +  0]
    ldp  x3, x4, [x28, #RT_PROC_ARGS + 16]
    ldp  x5, x6, [x28, #RT_PROC_ARGS + 32]
    ldr  x7,     [x28, #RT_PROC_ARGS + 48]
    blr  x19                            // .init(self, a0..a6)
    bl   rt_stop                        // falling off the end of .init is STOP

// ---------------------------------------------------------------------------
// Op stubs. Each one spills the payload (x1..x7) into a little array on the
// stack, so rt.c can take it as a plain `const uint64_t *args`.
// ---------------------------------------------------------------------------

.macro SPILL_PAYLOAD
    stp  x29, x30, [sp, #-80]!
    mov  x29, sp
    stp  x1, x2, [sp, #16]
    stp  x3, x4, [sp, #32]
    stp  x5, x6, [sp, #48]
    str  x7,     [sp, #64]
.endm

.macro UNSPILL_AND_RETURN
    ldp  x29, x30, [sp], #80
    ret
.endm

// SPAWN:   x9 = module, x11 = argc, x1.. = args            -> x0 = pid
FUNC rt_spawn
    SPILL_PAYLOAD
    mov  x0, x9
    mov  x1, x11
    add  x2, sp, #16
    bl   rt_spawn_c                     // (module, argc, args)
    UNSPILL_AND_RETURN

// SEND:    x0 = pid, x9 = tag, x11 = argc, x1.. = args
FUNC rt_send
    SPILL_PAYLOAD
    mov  x1, x9
    mov  x2, x11
    add  x3, sp, #16
    bl   rt_send_c                      // (pid, tag, argc, args)
    UNSPILL_AND_RETURN

// REQUEST: x0 = pid, x9 = tag, x10 = reply tag, x11 = argc, x1.. = args
//                                                          -> x0 = pending
FUNC rt_request
    SPILL_PAYLOAD
    mov  x1, x9
    mov  x2, x10
    mov  x3, x11
    add  x4, sp, #16
    bl   rt_request_c                   // (pid, tag, reply, argc, args)
    UNSPILL_AND_RETURN

// REPLY:   x11 = argc, x1.. = values
FUNC rt_reply
    SPILL_PAYLOAD
    mov  x0, x11
    add  x1, sp, #16
    bl   rt_reply_c                     // (argc, values)
    UNSPILL_AND_RETURN

// AWAIT:   x0 = pending, x9 = reply tag
//                       -> x0 = 1, x1.. = reply values   (or x0 = 0: failed)
// rt_await_c may suspend this process any number of times before it returns.
FUNC rt_await
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x1, x9
    bl   rt_await_c                     // (pending, reply) -> values or NULL
    cbz  x0, 1f
    mov  x8, x0
    ldp  x1, x2, [x8,  #0]
    ldp  x3, x4, [x8, #16]
    ldp  x5, x6, [x8, #32]
    ldr  x7,     [x8, #48]
    mov  x0, #1
1:  ldp  x29, x30, [sp], #16
    ret

// MSG_ACCEPT:              -> x0 = envelope, x1.. = args
FUNC rt_msg_accept
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    bl   rt_msg_accept_c                // -> envelope, now the top MESSAGE frame
    add  x8, x0, #RT_ENV_ARGS
    ldp  x1, x2, [x8,  #0]
    ldp  x3, x4, [x8, #16]
    ldp  x5, x6, [x8, #32]
    ldr  x7,     [x8, #48]
    ldp  x29, x30, [sp], #16
    ret
