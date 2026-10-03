// rt_asm.S -- the parts of the runtime that have to be assembly.

#include "asm.h"

// ---------------------------------------------------------------------------
// rt_value_t rt_enter(rt_proc_t *proc, rt_value_t (*fn)(void), void *stack_top)
//
// Calls fn on another stack, with x28 = proc. x27 and x28 are callee-saved
// under AAPCS64, so the C caller's values are kept and put back; x27 holds
// the C stack's sp meanwhile, which works because compiled code never
// touches x19..x27.
// ---------------------------------------------------------------------------

FUNC rt_enter
    stp  x29, x30, [sp, #-32]!
    mov  x29, sp
    stp  x27, x28, [sp, #16]
    mov  x28, x0
    mov  x27, sp
    mov  sp, x2
    blr  x1
    mov  sp, x27
    ldp  x27, x28, [sp, #16]
    ldp  x29, x30, [sp], #32
    ret
