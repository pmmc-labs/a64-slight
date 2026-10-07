// The x86-64 target (System V, on Linux and macOS), in Intel syntax. The
// accumulator is rax, which also carries the closure into a lambda's code;
// a binary operation's left operand is rcx; the arguments go in rdi, rsi,
// rdx, rcx, r8, r9 (System V's, so calls into C line up) and then r10,
// r11 (ours: no runtime function takes more than six); the result comes
// back in rax; r15 is the current process. rdx holds what was just
// allocated; rdx, rsi and r11 are scratch where nothing is in them. rbx
// and r12-r14 are never used (runtime/asm_x86_64.h, D145).
//
// Frames are push rbp / mov rbp, rsp / sub rsp, size, with the slots
// addressed up from rsp, so rsp is 16-aligned for every call, as System V
// requires: nothing checks it, and a mistake crashes deep in libc.

import { comment, placeArgs, type Code, type Cond, type Operand, type Target } from './target.ts';

const ARGS = ['rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9', 'r10', 'r11'];

const slot = (si: number): string => `qword ptr [rsp + ${8 * si}]`;

// Takes down the frame that the prologue built.
const EPILOGUE: Code = ['    mov  rsp, rbp', '    pop  rbp'];

const CC: Readonly<Record<Cond, string>> = { eq: 'e', ne: 'ne', lt: 'l', le: 'le', gt: 'g', ge: 'ge' };

// rax = #true if the condition (cmovcc's suffix) holds, otherwise #false.
// mov leaves the flags alone.
const boolIf = (cc: string): Code => ['    mov  rax, RT_FALSE', '    mov  edx, RT_TRUE', `    cmov${cc} rax, rdx`];

// A word into reg: one mov when it fits in 32 signed bits, else movabs.
function loadWord(reg: string, word: bigint): string {
    const w = BigInt.asIntN(64, word);
    return w >= -0x80000000n && w <= 0x7fffffffn
        ? `    mov  ${reg}, ${w}`
        : `    movabs ${reg}, 0x${BigInt.asUintN(64, w).toString(16)}`;
}

// The tag of rax, in edx.
const TAG: Code = ['    mov  edx, eax', '    and  edx, RT_TAG_MASK'];

// Type predicates: a test of rax that sets the flags, and the condition
// (cmovcc's suffix) that means yes. With no ccmp, a test with two parts
// sets a byte for each and ors them.
const PREDICATES: Readonly<Record<string, readonly [Code, string]>> = {
    'int?':  [['    test al, 1'], 'e'],
    'nil?':  [['    cmp  rax, RT_NIL'], 'e'],
    // a cons when neither "not the list tag" nor "()" holds
    'cons?': [[TAG, '    cmp  edx, RT_TAG_LIST', '    setne dl', '    cmp  rax, RT_NIL', '    sete al', '    or   al, dl'], 'e'],
    'pid?':  [[TAG, '    cmp  edx, RT_TAG_PID'], 'e'],
    'sym?':  [[TAG, '    cmp  edx, RT_TAG_SYMBOL'], 'e'],
    // a boolean when either "#true" or "#false" holds
    'bool?': [['    cmp  rax, RT_TRUE', '    sete dl', '    cmp  rax, RT_FALSE', '    sete al', '    or   al, dl'], 'ne'],
};

// + - * on two integers in rcx (left) and rax (right) into rax. As on
// AArch64, tagged integers add and subtract as words, and an untagged
// right times a tagged left is the tagged product; OF says it didn't fit.
const ARITH: Readonly<Record<'+' | '-' | '*', (overflow: string) => Code>> = {
    '+': (overflow) => ['    add  rax, rcx', `    jo   ${overflow}`],
    '-': (overflow) => ['    sub  rcx, rax', '    mov  rax, rcx', `    jo   ${overflow}`],
    '*': (overflow) => ['    sar  rax, 1', '    imul rax, rcx', `    jo   ${overflow}`],
};

// div and %: idiv divides rdx:rax by its operand, so the left goes in rax
// and the right moves out to rsi. The tagged words give the plain quotient
// (2a / 2b = a / b) in rax and the tagged remainder in rdx. idiv traps on
// -2^63 / -1, but a tagged divisor is even; only -2^62 div -1 overflows.
const DIVISION: Readonly<Record<'div' | '%', (zero: string, overflow: string) => Code>> = {
    'div': (zero, overflow) => [
        '    test rax, rax',
        `    jz   ${zero}`,
        '    mov  rsi, rax',
        '    mov  rax, rcx',
        '    cqo',
        '    idiv rsi',
        '    add  rax, rax',
        `    jo   ${overflow}`,
    ],
    '%': (zero) => [
        '    test rax, rax',
        `    jz   ${zero}`,
        '    mov  rsi, rax',
        '    mov  rax, rcx',
        '    cqo',
        '    idiv rsi',
        '    mov  rax, rdx',
    ],
};

const register = (op: Operand): string | null => (op.t === 'acc' ? 'rax' : op.t === 'left' ? 'rcx' : op.t === 'proc' ? 'r15' : null);

function move(dst: string, op: Operand): Code {
    switch (op.t) {
        case 'slot':     return `    mov  ${dst}, ${slot(op.si)}`;
        case 'imm':      return `    mov  ${dst}, ${op.v}`;
        case 'addr':     return `    lea  ${dst}, [rip + ${op.label}]`;
        case 'frame':    return `    mov  ${dst}, rsp`;
        case 'slotAddr': return `    lea  ${dst}, [rsp + ${8 * op.si}]`;
        default:         throw new Error(`x86_64: no move for ${op.t}`);
    }
}

const regMove = (dst: string, src: string): Code => `    mov  ${dst}, ${src}`;

export const X86_64: Target = {
    name: 'x86_64',

    fileStart: ['#include "asm_x86_64.h"', '', '    .text'],
    functionStart: (entry, name) => (entry === 'slight_main' ? 'FUNC slight_main' : ['    .p2align 4', `${entry}:${comment(name)}`]),

    // The return address and rbp on top, then the slots. The captured
    // values come from the closure in rax after the parameters are stored,
    // so r11 (the eighth parameter) is free by then.
    prologue: (size, overflow, params, free, preempt) => [
        '    push rbp',
        '    mov  rbp, rsp',
        size > 0 ? `    sub  rsp, ${size}` : [],
        '    cmp  rsp, qword ptr [r15 + RT_PROC_STACK_LIMIT]',
        `    jb   ${overflow}`,
        params.map((p, i) => `    mov  ${slot(i)}, ${ARGS[i]}${comment(p)}`),
        free.map((name, i) => [
            `    mov  r11, qword ptr [rax + RT_CLOSURE_FREE + ${8 * i}]`,
            `    mov  ${slot(params.length + i)}, r11${comment(name)}`,
        ]),
        '    sub  qword ptr [r15 + RT_PROC_REDUCTIONS], 1',
        `    jle  ${preempt}`,
        `${preempt}_done:`,
    ],
    ret: [EPILOGUE, '    ret'],

    // C takes a seventh and eighth argument on the stack, not in r10 and r11,
    // so the runtime's functions take at most six (rt_apply, in assembly,
    // spreads into all eight).
    call: (fn, args, tail) => {
        if (fn.startsWith('rt_') && fn !== 'rt_apply' && args.length > 6) {
            throw new Error(`x86_64: ${fn} would need its arguments 7 and 8 on the stack`);
        }
        return [
            placeArgs(args, ARGS, register, move, regMove),
            tail ? [EPILOGUE, `    jmp  ${fn}    // tail call`] : `    call ${fn}`,
        ];
    },
    // The closure stays in rax for its code to find its captured values;
    // the checks use r11 before the arguments are loaded.
    closureCall: (fnSlot, argSlots, notFn, arity, tail) => [
        `    mov  rax, ${slot(fnSlot)}`,
        '    mov  r11d, eax',
        '    and  r11d, RT_TAG_MASK',
        '    cmp  r11d, RT_TAG_BOXED',
        `    jne  ${notFn}`,
        '    mov  r11, qword ptr [rax - RT_TAG_BOXED]',
        '    and  r11d, RT_BOX_TYPE_MASK',
        '    cmp  r11d, RT_BOX_CLOSURE',
        `    jne  ${notFn}`,
        `    cmp  qword ptr [rax + RT_CLOSURE_ARITY], ${argSlots.length}`,
        `    jne  ${arity}`,
        argSlots.map((si, i) => `    mov  ${ARGS[i]}, ${slot(si)}`),
        tail
            ? [EPILOGUE, '    jmp  qword ptr [rax + RT_CLOSURE_CODE]    // tail call']
            : '    call qword ptr [rax + RT_CLOSURE_CODE]',
    ],
    jump: (label) => `    jmp  ${label}`,

    loadWord: (word, text) => `${loadWord('rax', word)}${comment(text)}`,
    loadConst: (name) => `    mov  rax, ${name}`,
    loadTagged: (label, tag) => [`    lea  rax, [rip + ${label}]`, `    or   rax, ${tag}`],
    loadProc: (field) => `    mov  rax, qword ptr [r15 + ${field}]`,
    loadSlot: (si, text) => `    mov  rax, ${slot(si)}${comment(text)}`,
    storeSlot: (si, text) => `    mov  ${slot(si)}, rax${comment(text)}`,
    copySlot: (from, to, text) => [`    mov  r11, ${slot(from)}${comment(text)}`, `    mov  ${slot(to)}, r11`],
    loadLeft: (si) => `    mov  rcx, ${slot(si)}`,

    testBool: (ifFalse, notBool) => [
        '    cmp  rax, RT_FALSE',
        `    je   ${ifFalse}`,
        '    cmp  rax, RT_TRUE',
        `    jne  ${notBool}`,
    ],
    branchUnlessInt: (label) => ['    test al, 1', `    jne  ${label}`],
    branchUnlessInts: (label) => ['    mov  edx, eax', '    or   edx, ecx', '    test dl, 1', `    jne  ${label}`],
    branchUnlessList: (label) => [TAG, '    cmp  edx, RT_TAG_LIST', `    jne  ${label}`],
    branchUnlessCons: (label) => [TAG, '    cmp  edx, RT_TAG_LIST', `    jne  ${label}`, '    cmp  rax, RT_NIL', `    je   ${label}`],
    branchUnlessNil: (label) => ['    cmp  rax, RT_NIL', `    jne  ${label}`],
    branchUnlessLeftIs: (word, label) => {
        const w = BigInt.asIntN(64, word);
        const compare = w >= -0x80000000n && w <= 0x7fffffffn ? `    cmp  rcx, ${w}` : [loadWord('rdx', word), '    cmp  rcx, rdx'];
        return [compare, `    jne  ${label}`];
    },
    branchIfSame: (label) => ['    cmp  rcx, rax', `    je   ${label}`],

    predicate: (name) => {
        const [test, cc] = PREDICATES[name]!;
        return [test, boolIf(cc)];
    },
    compareBool: (cond) => ['    cmp  rcx, rax', boolIf(CC[cond])],
    notBool: ['    cmp  rax, RT_TRUE', boolIf('ne')],

    arith: (op, overflow) => ARITH[op](overflow),
    divide: (op, zero, overflow) => DIVISION[op](zero, overflow),

    cxr: (step) => (step === 'a' ? '    mov  rax, qword ptr [rax - 1]    // car' : '    mov  rax, qword ptr [rax + 7]    // cdr'),
    leftFromCar: '    mov  rcx, qword ptr [rax - 1]',
    accFromCdr: '    mov  rax, qword ptr [rax + 7]',
    storeCar: (si, text) => ['    mov  rcx, qword ptr [rax - 1]', `    mov  ${slot(si)}, rcx${comment(text)}`],

    // Bump-allocates into rdx, with rsi for the new heap pointer. When the
    // chunk hasn't room, rt_heap_grow makes a new one (or faults) and the
    // allocation goes again; rax and rcx survive that, on the stack (two
    // pushes keep it 16-aligned for the call).
    allocate: (bytes, again, site) => [
        [
            `${again}:`,
            '    mov  rdx, qword ptr [r15 + RT_PROC_HEAP_PTR]',
            `    lea  rsi, [rdx + ${bytes}]`,
            '    cmp  rsi, qword ptr [r15 + RT_PROC_HEAP_LIMIT]',
            `    ja   ${again}_grow`,
            '    mov  qword ptr [r15 + RT_PROC_HEAP_PTR], rsi',
        ],
        [
            `${again}_grow:`,
            '    push rax',
            '    push rcx',
            `    mov  rdi, ${bytes}`,
            `    lea  rsi, [rip + ${site}]`,
            '    call rt_heap_grow',
            '    pop  rcx',
            '    pop  rax',
            `    jmp  ${again}`,
        ],
    ],
    consCell: (headSlot) => [
        `    mov  rcx, ${slot(headSlot)}`,
        '    mov  qword ptr [rdx], rcx',
        '    mov  qword ptr [rdx + 8], rax',
        '    lea  rax, [rdx + RT_TAG_LIST]',
    ],
    // The cells side by side, each one's cdr the next.
    listCells: (firstSlot, n) => [
        [...Array(n).keys()].map((i) => [
            `    mov  rsi, ${slot(firstSlot + i)}`,
            `    mov  qword ptr [rdx + ${16 * i}], rsi`,
            i + 1 < n ? `    lea  rsi, [rdx + ${16 * (i + 1) + 1}]` : '    mov  rsi, RT_NIL',
            `    mov  qword ptr [rdx + ${16 * i + 8}], rsi`,
        ]),
        '    lea  rax, [rdx + RT_TAG_LIST]',
    ],
    // The header (how many values it captured, and the closure type), the
    // code, the arity, the name (for printing), then the captured values.
    makeClosure: (entry, arity, name, free) => [
        `    mov  qword ptr [rdx], ${free.length} << RT_BOX_SIZE_SHIFT | RT_BOX_CLOSURE`,
        `    lea  rsi, [rip + ${entry}]`,
        '    mov  qword ptr [rdx + 8], rsi',
        `    mov  qword ptr [rdx + 16], ${arity}`,
        `    lea  rsi, [rip + ${name}]`,
        '    mov  qword ptr [rdx + 24], rsi',
        free.map(([si, text], i) => [`    mov  rsi, ${slot(si)}${comment(text)}`, `    mov  qword ptr [rdx + ${32 + 8 * i}], rsi`]),
        '    lea  rax, [rdx + RT_TAG_BOXED]',
    ],
};
