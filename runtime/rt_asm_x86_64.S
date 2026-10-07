// rt_asm_x86_64.S -- rt_asm_aarch64.S for x86-64: the parts of the runtime
// that have to be assembly. Intel syntax, System V ABI.

#include "asm_x86_64.h"

// ---------------------------------------------------------------------------
// void rt_switch(rt_ctx_t *from, rt_ctx_t *to)
//
// Saves the callee-saved registers of whoever is running into `from`, loads
// `to`, and returns into it. The return address is on the stack, so rsp
// is all there is to save of it. Everything else is caller-saved under
// System V, so the code that called rt_switch (directly, or through any
// runtime op) has already given it up. MXCSR and the x87 control word are
// callee-saved too, but nothing in the runtime or in compiled code changes
// them, so every process has the same ones and there's nothing to switch.
// ---------------------------------------------------------------------------

FUNC rt_switch
    mov  [rdi + RT_CTX_RBX], rbx
    mov  [rdi + RT_CTX_RBP], rbp
    mov  [rdi + RT_CTX_R12], r12
    mov  [rdi + RT_CTX_R13], r13
    mov  [rdi + RT_CTX_R14], r14
    mov  [rdi + RT_CTX_R15], r15
    mov  [rdi + RT_CTX_RSP], rsp

    mov  rbx, [rsi + RT_CTX_RBX]
    mov  rbp, [rsi + RT_CTX_RBP]
    mov  r12, [rsi + RT_CTX_R12]
    mov  r13, [rsi + RT_CTX_R13]
    mov  r14, [rsi + RT_CTX_R14]
    mov  r15, [rsi + RT_CTX_R15]
    mov  rsp, [rsi + RT_CTX_RSP]
    ret

// ---------------------------------------------------------------------------
// rt_trampoline: where a process's first switch lands, by rt_switch's ret
// popping the address start() put on top of a fresh stack. So r15 = the
// process and rsp is 16-aligned, as before a call. Calls proc->code with
// proc->args in the eight argument registers, then ends the process with
// what it returned.
// ---------------------------------------------------------------------------

FUNC rt_trampoline
    mov  rdi, [r15 + RT_PROC_ARGS +  0]
    mov  rsi, [r15 + RT_PROC_ARGS +  8]
    mov  rdx, [r15 + RT_PROC_ARGS + 16]
    mov  rcx, [r15 + RT_PROC_ARGS + 24]
    mov  r8,  [r15 + RT_PROC_ARGS + 32]
    mov  r9,  [r15 + RT_PROC_ARGS + 40]
    mov  r10, [r15 + RT_PROC_ARGS + 48]
    mov  r11, [r15 + RT_PROC_ARGS + 56]
    call qword ptr [r15 + RT_PROC_CODE]
    mov  rsi, rax
    mov  rdi, r15
    call rt_exit                        // doesn't return

// ---------------------------------------------------------------------------
// rt_value_t rt_apply(rt_value_t f, rt_value_t args, const char *site)
//
// Spreads up to 8 elements of args into the argument registers, puts the
// closure in rax, and jumps to its code: f returns straight to apply's
// caller, so a tail call to apply is still a tail call. Faults if f isn't
// a function, args isn't a list, or the counts don't match, as on AArch64.
//
// The spread needs all eight argument registers and rax, which leaves no
// caller-saved register for the list or the count. So it counts first,
// checking every cell while f, args and site are still where rt_fault
// wants them, and then spreads a list it knows is right, with the cursor in
// r11 until the last element takes its place.
// ---------------------------------------------------------------------------

// The next element of the list in r11 into \reg, unless there are no more.
.macro SPREAD reg
    cmp  r11, RT_NIL
    je   Lapply_call
    mov  \reg, [r11 - RT_TAG_LIST]
    mov  r11, [r11 + 8 - RT_TAG_LIST]
.endm

FUNC rt_apply
    mov  eax, edi
    and  eax, RT_TAG_MASK
    cmp  eax, RT_TAG_BOXED
    jne  Lapply_not_function
    cmp  byte ptr [rdi - RT_TAG_BOXED], RT_BOX_CLOSURE     // the header's low byte is the type
    jne  Lapply_not_function
    mov  rax, rdi

    mov  rcx, rsi
    xor  r8d, r8d
Lapply_count:
    cmp  rcx, RT_NIL
    je   Lapply_counted
    cmp  r8, 8                          // more than 8: no function takes that many
    je   Lapply_arity
    lea  r9, [rcx - RT_TAG_LIST]
    test r9b, RT_TAG_MASK
    jnz  Lapply_not_list
    mov  rcx, [rcx + 8 - RT_TAG_LIST]
    inc  r8
    jmp  Lapply_count
Lapply_counted:
    cmp  r8, [rax + RT_CLOSURE_ARITY]
    jne  Lapply_arity

    mov  r11, rsi
    SPREAD rdi
    SPREAD rsi
    SPREAD rdx
    SPREAD rcx
    SPREAD r8
    SPREAD r9
    SPREAD r10
    cmp  r11, RT_NIL
    je   Lapply_call
    mov  r11, [r11 - RT_TAG_LIST]       // the eighth, and the list's last cell
Lapply_call:
    jmp  qword ptr [rax + RT_CLOSURE_CODE]

// rdi is still f, rsi the list, rdx the site.
Lapply_not_function:
    mov  rsi, rdi
    mov  edi, RT_FAULT_NOT_FUNC
    jmp  Lapply_fault
Lapply_not_list:
    mov  edi, RT_FAULT_NOT_LIST
    jmp  Lapply_fault
Lapply_arity:
    mov  rsi, rax
    mov  edi, RT_FAULT_ARITY
Lapply_fault:
    sub  rsp, 8                         // entered by call or tail jump: rsp was 8 mod 16
    call rt_fault                       // doesn't return
