// rt_asm_aarch64.S -- the parts of the runtime that have to be assembly,
// for AArch64 (rt_asm_x86_64.S has the x86-64 ones).

#include "asm_aarch64.h"

// ---------------------------------------------------------------------------
// void rt_switch(rt_ctx_t *from, rt_ctx_t *to)
//
// Saves the callee-saved state of whoever is running into `from`, loads
// `to`, and returns into it. Everything else is caller-saved under AAPCS64,
// so the code that called rt_switch (directly, or through any runtime op)
// has already given it up. x18 is never touched: on macOS it's the
// platform's.
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
// rt_trampoline: where a process's first switch lands, with x28 = the
// process and sp = the top of a fresh stack. Calls proc->code with
// proc->args in x0..x7, then ends the process with what it returned.
// ---------------------------------------------------------------------------

FUNC rt_trampoline
    ldp  x0, x1, [x28, #RT_PROC_ARGS +  0]
    ldp  x2, x3, [x28, #RT_PROC_ARGS + 16]
    ldp  x4, x5, [x28, #RT_PROC_ARGS + 32]
    ldp  x6, x7, [x28, #RT_PROC_ARGS + 48]
    ldr  x16, [x28, #RT_PROC_CODE]
    blr  x16
    mov  x1, x0
    mov  x0, x28
    bl   rt_exit                        // doesn't return

// ---------------------------------------------------------------------------
// rt_value_t rt_apply(rt_value_t f, rt_value_t args, const char *site)
//
// Spreads up to 8 elements of args into x0..x7, puts the closure in x9,
// and jumps to its code: f returns straight to apply's caller, so a tail
// call to apply is still a tail call. Faults if f isn't a function, args
// isn't a list, or the counts don't match.
// ---------------------------------------------------------------------------

// The next element of the list in x11 into \reg, counting in x10.
.macro SPREAD reg
    cmp  x11, #RT_NIL
    b.eq Lapply_spread
    and  x12, x11, #RT_TAG_MASK
    cmp  x12, #RT_TAG_LIST
    b.ne Lapply_not_list
    ldur \reg, [x11, #-1]
    ldur x11, [x11, #7]
    add  x10, x10, #1
.endm

FUNC rt_apply
    mov  x9, x0
    mov  x13, x2                        // the site, out of the argument registers
    mov  x14, x1                        // the list, for a fault
    and  x16, x9, #RT_TAG_MASK
    cmp  x16, #RT_TAG_BOXED
    b.ne Lapply_not_function
    ldur x16, [x9, #-RT_TAG_BOXED]
    and  x16, x16, #RT_BOX_TYPE_MASK
    cmp  x16, #RT_BOX_CLOSURE
    b.ne Lapply_not_function
    mov  x10, #0
    mov  x11, x1
    SPREAD x0
    SPREAD x1
    SPREAD x2
    SPREAD x3
    SPREAD x4
    SPREAD x5
    SPREAD x6
    SPREAD x7
    cmp  x11, #RT_NIL                   // more than 8: no function takes that many
    b.ne Lapply_arity
Lapply_spread:
    ldur x16, [x9, #RT_CLOSURE_ARITY]
    cmp  x16, x10
    b.ne Lapply_arity
    ldur x16, [x9, #RT_CLOSURE_CODE]
    br   x16

Lapply_not_function:
    mov  x1, x9
    mov  x0, #RT_FAULT_NOT_FUNC
    mov  x2, x13
    bl   rt_fault
Lapply_not_list:
    mov  x1, x14
    mov  x0, #RT_FAULT_NOT_LIST
    mov  x2, x13
    bl   rt_fault
Lapply_arity:
    mov  x1, x9
    mov  x0, #RT_FAULT_ARITY
    mov  x2, x13
    bl   rt_fault

