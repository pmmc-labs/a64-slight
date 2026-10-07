// asm_aarch64.h -- assembler macros for the runtime's AArch64 assembly and
// the compiler's AArch64 output (asm_x86_64.h is x86-64's). Include it
// from a .S file (it goes through the C preprocessor). Needs clang's
// integrated assembler: GNU as rejects some of these.
//
// Register convention for compiled code (docs/DESIGN.md):
//   x28        the current process. Set by the runtime, never written by
//              compiled code.
//   x19..x27   callee-saved. Compiled code never uses them, and every
//              runtime op preserves them.
//   x18        never touched: it belongs to the platform on macOS
//   x0         the accumulator: every expression leaves its value here
//   x0..x7     a function's arguments, in order; the result comes back in x0
//   x16        scratch for the checks at function entry

#ifndef ASM_AARCH64_H
#define ASM_AARCH64_H

#include "rt.h"

// Marks the stack non-executable. Without it GNU ld (a native link on
// Linux) warns, and makes the stack executable.
#if !defined(__APPLE__)
    .section .note.GNU-stack,"",@progbits
    .text
#endif

// Read-only data: strings and constants with no pointers in them.
.macro RODATA
#if defined(__APPLE__)
    .section __TEXT,__const
#else
    .section .rodata
#endif
.endm

// Constants with addresses in them (quoted lists), which the loader may
// have to fix up before they become read-only.
.macro CONSTDATA
#if defined(__APPLE__)
    .section __DATA,__const
#else
    .section .data.rel.ro
#endif
.endm

// The address of a symbol, on Mach-O and on ELF.
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

#endif // ASM_AARCH64_H
