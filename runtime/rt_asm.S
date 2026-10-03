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

