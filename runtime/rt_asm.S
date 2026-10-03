// rt_asm.S -- the parts of the runtime that have to be assembly.

#include "asm.h"

// ---------------------------------------------------------------------------
// rt_value_t rt_enter(rt_proc_t *proc, rt_value_t (*fn)(void))
//
// Calls fn with x28 = proc. x28 is callee-saved under AAPCS64, so the C
// caller's value is kept and put back.
// ---------------------------------------------------------------------------

FUNC rt_enter
    stp  x29, x30, [sp, #-32]!
    mov  x29, sp
    str  x28,      [sp, #16]
    mov  x28, x0
    blr  x1
    ldr  x28,      [sp, #16]
    ldp  x29, x30, [sp], #32
    ret
