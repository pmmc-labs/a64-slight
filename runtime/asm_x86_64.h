// asm_x86_64.h -- asm_aarch64.h for x86-64: assembler macros for the
// runtime's assembly and the compiler's output, in Intel syntax. Include
// it from a .S file (it goes through the C preprocessor), with clang's
// integrated assembler.
//
// Register convention for compiled code (docs/BACKGROUND.md, "Other
// targets"):
//   rax        the accumulator: every expression leaves its value here, a
//              function's result comes back in it, and a lambda finds its
//              closure in it at entry (x9 on AArch64)
//   rdi, rsi, rdx, rcx, r8, r9, r10, r11
//              a function's arguments, in order. The first six are System
//              V's, so calls into the runtime line up; the last two are ours.
//   r15        the current process. Set by the runtime, never written by
//              compiled code.
//   rbp        the frame pointer: a frame is push rbp / mov rbp, rsp /
//              sub rsp, size, with size a multiple of 16
//   rbx, r12..r14
//              compiled code doesn't use them. Nothing would see if it did:
//              C only enters compiled code through rt_trampoline, whose
//              caller is rt_switch, which keeps them per process.
//
// No LOADADDR: an address is lea reg, [rip + sym], the same on ELF and
// Mach-O.

#ifndef ASM_X86_64_H
#define ASM_X86_64_H

.intel_syntax noprefix

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

.macro FUNC name
    .text
    .p2align 4
    .globl \name
\name:
.endm

#endif // ASM_X86_64_H
