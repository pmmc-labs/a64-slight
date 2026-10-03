// asm.h -- assembler macros for the runtime's assembly and the compiler's
// output. Include it from a .S file (it goes through the C preprocessor).
// Needs clang's integrated assembler: GNU as rejects some of these.
//
// Register convention for compiled code (docs/DESIGN.md):
//   x28        the current process. Set by the runtime, never written by
//              compiled code.
//   x19..x27   callee-saved, and preserved by every runtime op
//   x18        never touched: it belongs to the platform on macOS
//   x0         the accumulator: every expression leaves its value here

#ifndef ASM_H
#define ASM_H

#include "rt.h"

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

#endif // ASM_H
