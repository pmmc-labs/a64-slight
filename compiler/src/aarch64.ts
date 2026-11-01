// The AArch64 target (AAPCS64, on macOS and Linux). The accumulator is x0,
// a binary operation's left operand is x1, the arguments go in x0..x7 and
// the result comes back in x0, x28 is the current process, x9 carries the
// closure into a lambda's code, and x16 is scratch (runtime/asm_aarch64.h,
// D47).
// Frame slots are addressed up from sp, under x29/x30.

import { comment, NOT, placeArgs, type Code, type BitOp, type Cond, type FloatOp, type Operand, type Target } from './target.ts';
import { FLOAT_HEADER, FLOAT_VALUE, tupleElement } from './values.ts';

const ARGS = ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7'];

const slot = (si: number): string => `[sp, #${8 * si}]`;

// Takes down the frame that the prologue built.
const EPILOGUE: Code = ['    mov  sp, x29', '    ldp  x29, x30, [sp], #16'];

// x0 = #true if cond holds, otherwise #false.
const boolIf = (cond: string): Code => ['    mov  x0, #RT_TRUE', '    mov  x2, #RT_FALSE', `    csel x0, x0, x2, ${cond}`];

// Sets Z when x0 is not a cons: when its tag isn't the list tag, ccmp
// skips the second compare and sets Z (#4) itself; otherwise Z means nil.
const IS_CONS: Code = [
    '    and  x1, x0, #RT_TAG_MASK',
    '    cmp  x1, #RT_TAG_LIST',
    '    ccmp x0, #RT_NIL, #4, eq',
];

// Type predicates: a test of x0 that sets the flags, and the condition that
// means yes. #true and #false are symbols too, so sym? says yes to them.
const PREDICATES: Readonly<Record<string, readonly [Code, string]>> = {
    'int?':  [['    tst  x0, #1'], 'eq'],
    'nil?':  [['    cmp  x0, #RT_NIL'], 'eq'],
    'cons?': [IS_CONS, 'ne'],
    'pid?':  [['    and  x1, x0, #RT_TAG_MASK', '    cmp  x1, #RT_TAG_PID'], 'eq'],
    'sym?':  [['    and  x1, x0, #RT_TAG_MASK', '    cmp  x1, #RT_TAG_SYMBOL'], 'eq'],
    // when x0 is #true, ccmp skips the second compare and sets Z (#4) itself
    'bool?': [['    cmp  x0, #RT_TRUE', '    ccmp x0, #RT_FALSE, #4, ne'], 'eq'],
};

// + - * on two integers in x1 (left) and x0 (right) into x0.
const ARITH: Readonly<Record<'+' | '-' | '*', (overflow: string) => Code>> = {
    // Tagged integers are n << 1, so adding or subtracting the words adds or
    // subtracts the integers, and 64-bit overflow is exactly 63-bit overflow.
    '+': (overflow) => ['    adds x0, x1, x0', `    b.vs ${overflow}`],
    '-': (overflow) => ['    subs x0, x1, x0', `    b.vs ${overflow}`],
    // (a << 1) * b = (a * b) << 1: untag one side. It overflows when the high
    // half of the 128-bit product isn't just the low half's sign.
    '*': (overflow) => [
        '    asr  x2, x0, #1',
        '    smulh x3, x1, x2',
        '    mul  x0, x1, x2',
        '    cmp  x3, x0, asr #63',
        `    b.ne ${overflow}`,
    ],
};

// div and %: integers only, truncating toward zero (D68). sdiv of the two
// tagged words gives the plain quotient (2a / 2b = a / b), and the tagged
// remainder is 2a - q * 2b. Only -2^62 div -1 overflows.
const DIVISION: Readonly<Record<'div' | '%', (zero: string, overflow: string) => Code>> = {
    'div': (zero, overflow) => [
        `    cbz  x0, ${zero}`,
        '    sdiv x2, x1, x0',
        '    adds x0, x2, x2',
        `    b.vs ${overflow}`,
    ],
    '%': (zero) => [
        `    cbz  x0, ${zero}`,
        '    sdiv x2, x1, x0',
        '    msub x0, x2, x0, x1',
    ],
};

// Bits (D221). An integer is n << 1, so and, or and xor of the tagged
// words are the tagged results, and a shift left of the word is the tagged
// n << k, wrapped to 63 bits. A shift right of the word is n >> (k - 1),
// whose bit 0, cleared, leaves the tagged n >> k. A count is clamped to
// 63, which the shifts would otherwise take mod 64: that shifts out every
// bit, or leaves only the sign's.
const BITS: Readonly<Record<BitOp, (range: string) => Code>> = {
    and: () => '    and  x0, x1, x0',
    or:  () => '    orr  x0, x1, x0',
    xor: () => '    eor  x0, x1, x0',
    shl: (range) => [...COUNT(range), '    lsl  x0, x1, x2'],
    shr: (range) => [...COUNT(range), '    asr  x0, x1, x2', '    and  x0, x0, #-2'],
};

// The accumulator's count, untagged and clamped to 63, into x2.
const COUNT = (range: string): readonly string[] => [
    '    cmp  x0, #0',
    `    b.lt ${range}`,
    '    asr  x2, x0, #1',
    '    mov  x16, #63',
    '    cmp  x2, x16',
    '    csel x2, x2, x16, lt',
];

// The shortest movz/movn + movk sequence that puts a 64-bit word in reg:
// start from all zeros (movz) or all ones (movn), whichever leaves fewer
// 16-bit chunks to patch with movk.
export function loadWord(reg: string, word: bigint): readonly string[] {
    const w      = BigInt.asUintN(64, word);
    const chunks = [0, 1, 2, 3].map((k) => ({ k, v: Number((w >> BigInt(16 * k)) & 0xffffn) }));
    const ones   = chunks.filter((c) => c.v === 0xffff).length;
    const zeros  = chunks.filter((c) => c.v === 0).length;
    const fill   = ones > zeros ? 0xffff : 0;
    const [first, ...rest] = chunks.filter((c) => c.v !== fill);
    const hex    = (n: number): string => `#0x${n.toString(16)}`;
    const head   = first === undefined
        ? `    ${fill === 0 ? 'movz' : 'movn'} ${reg}, #0`
        : fill === 0
            ? `    movz ${reg}, ${hex(first.v)}, lsl #${16 * first.k}`
            : `    movn ${reg}, ${hex(~first.v & 0xffff)}, lsl #${16 * first.k}`;
    return [head, ...rest.map((c) => `    movk ${reg}, ${hex(c.v)}, lsl #${16 * c.k}`)];
}

// Compares x0 with a word: as an immediate when it fits cmp's (or, for a
// negative one, cmn's) twelve bits, which most small integers' words do.
function compareWord(word: bigint): Code {
    const w = BigInt.asIntN(64, word);
    if (w >= 0n && w <= 4095n) return `    cmp  x0, #${w}`;
    if (w < 0n && w >= -4095n) return `    cmn  x0, #${-w}`;
    return [loadWord('x2', word), '    cmp  x0, x2'];
}

// Floats: whether reg is a float's box, by its tag and then its header,
// which fits cmp's twelve bits; and the box just allocated (x2) made to
// hold d0, into x0. Only d0 and d1, which nothing keeps across a call.
const FLOAT_OPS: Readonly<Record<FloatOp, string>> = { '+': 'fadd', '-': 'fsub', '*': 'fmul', '/': 'fdiv' };

const isFloat = (reg: string, label: string): Code => [
    `    and  x16, ${reg}, #RT_TAG_MASK`,
    '    cmp  x16, #RT_TAG_BOXED',
    `    b.ne ${label}`,
    `    ldur x16, [${reg}, #-RT_TAG_BOXED]`,
    `    cmp  x16, #${FLOAT_HEADER}`,
    `    b.ne ${label}`,
];

// After fcmp, the condition for each comparison, false when either is a
// NaN (unordered sets C and V), and its negation, true then.
const FCC: Readonly<Record<Cond, string>>  = { eq: 'eq', ne: 'ne', lt: 'mi', le: 'ls', gt: 'gt', ge: 'ge' };
const FNOT: Readonly<Record<Cond, string>> = { eq: 'ne', ne: 'eq', lt: 'pl', le: 'hi', gt: 'le', ge: 'lt' };

// fcmp of the left operand's float with the accumulator's, or of the
// accumulator's with the integer n.
const fcmp = (n?: bigint): Code =>
    n === undefined ? [`    ldur d0, [x1, #${FLOAT_VALUE}]`, `    ldur d1, [x0, #${FLOAT_VALUE}]`, '    fcmp d0, d1']
    : n === 0n      ? [`    ldur d0, [x0, #${FLOAT_VALUE}]`, '    fcmp d0, #0.0']
    :                 [`    ldur d0, [x0, #${FLOAT_VALUE}]`, loadWord('x16', n), '    scvtf d1, x16', '    fcmp d0, d1'];

// A float expression's register k (D261): d0 to d7, then d16 to d31,
// leaving d8 to d15, whose low halves C keeps across a call.
const dreg = (k: number): string => `d${k < 8 ? k : k + 8}`;

const BOX_D0: Code = [
    `    mov  x3, #${FLOAT_HEADER}`,
    '    str  x3, [x2]',
    '    str  d0, [x2, #8]',
    '    orr  x0, x2, #RT_TAG_BOXED',
];

// dst = src + n, for any n.
function addImm(dst: string, src: string, n: number, scratch: string): Code {
    return n <= 4095 ? `    add  ${dst}, ${src}, #${n}` : [loadWord(scratch, BigInt(n)), `    add  ${dst}, ${src}, ${scratch}`];
}

// reg = the closure field at `offset` (rt.h's name for it, and its value)
// from the closure value in base.
function closureField(reg: string, base: string, name: string, offset: number): Code {
    return offset <= 255 ? `    ldur ${reg}, [${base}, #${name}]` : [`    add  ${reg}, ${base}, #${offset}`, `    ldr  ${reg}, [${reg}]`];
}

const register = (op: Operand): string | null =>
    op.t === 'acc' ? 'x0' : op.t === 'left' ? 'x1' : op.t === 'proc' ? 'x28' : op.t === 'fp' ? 'x29' : null;

// Slots from..to-1 = 0, two at a time while stp's offset reaches.
function zeroSlots(from: number, to: number): Code {
    const out: string[] = [];
    for (let i = from; i < to; i += i + 1 < to && 8 * i <= 504 ? 2 : 1) {
        out.push(i + 1 < to && 8 * i <= 504 ? `    stp  xzr, xzr, ${slot(i)}` : `    str  xzr, ${slot(i)}`);
    }
    return out;
}

function move(dst: string, op: Operand): Code {
    switch (op.t) {
        case 'slot':     return `    ldr  ${dst}, ${slot(op.si)}`;
        case 'imm':      return `    mov  ${dst}, #${op.v}`;
        case 'word':     return loadWord(dst, op.v);
        case 'addr':     return `    LOADADDR ${dst}, ${op.label}`;
        case 'frame':    return `    mov  ${dst}, sp`;
        case 'slotAddr': return addImm(dst, 'sp', 8 * op.si, 'x5');
        default:         throw new Error(`aarch64: no move for ${op.t}`);
    }
}

const regMove = (dst: string, src: string): Code => `    mov  ${dst}, ${src}`;

export const AARCH64: Target = {
    name: 'aarch64',

    fileStart: ['#include "asm_aarch64.h"', '', '    .text'],
    functionStart: (entry, name) => (entry === 'slight_main' ? 'FUNC slight_main' : ['    .p2align 2', `${entry}:${comment(name)}`]),

    // x29/x30 on top, then the slots. The captured values come from the
    // closure in x9 after the parameters are stored (x9 is gone after
    // rt_preempt, hence the order).
    prologue: (size, overflow, params, free, preempt, zero) => [
        '    stp  x29, x30, [sp, #-16]!',
        '    mov  x29, sp',
        size > 0 ? `    sub  sp, sp, #${size}` : [],
        '    ldr  x16, [x28, #RT_PROC_STACK_LIMIT]',
        '    cmp  sp, x16',
        `    b.lo ${overflow}`,
        params.map((p, i) => `    str  x${i}, ${slot(i)}${comment(p)}`),
        free.map((name, i) => [
            closureField('x16', 'x9', `RT_CLOSURE_FREE + ${8 * i}`, 29 + 8 * i),
            `    str  x16, ${slot(params.length + i)}${comment(name)}`,
        ]),
        zero ? zeroSlots(params.length + free.length, size / 8) : [],
        '    ldr  x16, [x28, #RT_PROC_REDUCTIONS]',
        '    subs x16, x16, #1',
        '    str  x16, [x28, #RT_PROC_REDUCTIONS]',
        `    b.le ${preempt}`,
        `${preempt}_done:`,
    ],
    ret: [EPILOGUE, '    ret'],

    call: (fn, args, tail) => [
        placeArgs(args, ARGS, register, move, regMove),
        tail ? [EPILOGUE, `    b    ${fn}    // tail call`] : `    bl   ${fn}`,
    ],
    // The closure goes in x9 for its code to find its captured values.
    closureCall: (fnSlot, argSlots, notFn, arity, tail) => [
        `    ldr  x0, ${slot(fnSlot)}`,
        '    and  x16, x0, #RT_TAG_MASK',
        '    cmp  x16, #RT_TAG_BOXED',
        `    b.ne ${notFn}`,
        '    ldur x16, [x0, #-RT_TAG_BOXED]',
        '    and  x16, x16, #RT_BOX_TYPE_MASK',
        '    cmp  x16, #RT_BOX_CLOSURE',
        `    b.ne ${notFn}`,
        '    ldur x16, [x0, #RT_CLOSURE_ARITY]',
        `    cmp  x16, #${argSlots.length}`,
        `    b.ne ${arity}`,
        '    mov  x9, x0',
        argSlots.map((si, i) => `    ldr  x${i}, ${slot(si)}`),
        '    ldur x16, [x9, #RT_CLOSURE_CODE]',
        tail ? [EPILOGUE, '    br   x16    // tail call'] : '    blr  x16',
    ],
    jump: (label) => `    b    ${label}`,

    loadWord: (word, note) => {
        const [first, ...rest] = loadWord('x0', word);
        return [`${first}${comment(note)}`, rest];
    },
    loadConst: (name) => `    mov  x0, #${name}`,
    loadTagged: (label, tag) => [`    LOADADDR x0, ${label}`, `    orr  x0, x0, #${tag}`],
    loadProc: (field) => `    ldr  x0, [x28, #${field}]`,
    loadSlot: (si, note) => `    ldr  x0, ${slot(si)}${comment(note)}`,
    storeSlot: (si, note) => `    str  x0, ${slot(si)}${comment(note)}`,
    copySlot: (from, to, note) => [`    ldr  x16, ${slot(from)}${comment(note)}`, `    str  x16, ${slot(to)}`],
    loadLeft: (si) => `    ldr  x1, ${slot(si)}`,
    load: (dst, op) => move(dst === 'acc' ? 'x0' : 'x1', op),
    accToLeft: '    mov  x1, x0',

    testBool: (ifFalse, notBool) => [
        '    cmp  x0, #RT_FALSE',
        `    b.eq ${ifFalse}`,
        '    cmp  x0, #RT_TRUE',
        `    b.ne ${notBool}`,
    ],
    branchUnlessInt: (label) => ['    tst  x0, #1', `    b.ne ${label}`],
    branchUnlessInts: (label) => ['    orr  x2, x0, x1', '    tst  x2, #1', `    b.ne ${label}`],
    branchUnlessList: (label) => ['    and  x1, x0, #RT_TAG_MASK', '    cmp  x1, #RT_TAG_LIST', `    b.ne ${label}`],
    branchUnlessCons: (label) => [IS_CONS, `    b.eq ${label}`],
    branchUnlessNil: (label) => ['    cmp  x0, #RT_NIL', `    b.ne ${label}`],
    branchUnlessIs: (word, label) => [loadWord('x2', word), '    cmp  x0, x2', `    b.ne ${label}`],
    branchIfSame: (label) => ['    cmp  x1, x0', `    b.eq ${label}`],
    branchUnlessCompare: (cond, label) => ['    cmp  x1, x0', `    b.${NOT[cond]} ${label}`],
    branchUnlessCompareWord: (cond, word, label) => [compareWord(word), `    b.${NOT[cond]} ${label}`],
    branchIfFalse: (label) => ['    cmp  x0, #RT_FALSE', `    b.eq ${label}`],
    branchUnlessBox: (header, label) => [
        '    and  x1, x0, #RT_TAG_MASK',
        '    cmp  x1, #RT_TAG_BOXED',
        `    b.ne ${label}`,
        '    ldur x1, [x0, #-RT_TAG_BOXED]     // the header',
        header <= 4095n ? `    cmp  x1, #${header}` : [loadWord('x2', header), '    cmp  x1, x2'],
        `    b.ne ${label}`,
    ],

    predicate: (name) => {
        const [test, cond] = PREDICATES[name]!;
        return [test, boolIf(cond)];
    },
    compareBool: (cond: Cond) => ['    cmp  x1, x0', boolIf(cond)],
    notBool: ['    cmp  x0, #RT_TRUE', boolIf('ne')],

    branchUnlessFloat: (label) => isFloat('x0', label),
    branchUnlessFloats: (label) => [isFloat('x0', label), isFloat('x1', label)],
    branchIfZeroFloat: (label) => [`    ldur d1, [x0, #${FLOAT_VALUE}]`, '    fcmp d1, #0.0', `    b.eq ${label}`],
    floatArith: (op) => [
        `    ldur d0, [x1, #${FLOAT_VALUE}]`,
        `    ldur d1, [x0, #${FLOAT_VALUE}]`,
        `    ${FLOAT_OPS[op]} d0, d0, d1`,
        BOX_D0,
    ],
    // n to a double as C's (double) makes it, rounding to nearest
    floatArithInt: (op, n) => [
        `    ldur d0, [x0, #${FLOAT_VALUE}]`,
        loadWord('x16', n),
        '    scvtf d1, x16',
        `    ${FLOAT_OPS[op]} d0, d0, d1`,
        BOX_D0,
    ],
    floatCompareBool: (cond, n) => [fcmp(n), boolIf(FCC[cond])],
    floatRegs: 24,
    floatFromAcc: (k) => `    ldur ${dreg(k)}, [x0, #${FLOAT_VALUE}]`,
    floatFromInt: (n, k) => [loadWord('x16', n), `    scvtf ${dreg(k)}, x16`],
    floatOp: (op, k) => `    ${FLOAT_OPS[op]} ${dreg(k)}, ${dreg(k)}, ${dreg(k + 1)}`,
    floatUnary: (op, k) => `    ${op === 'sqrt' ? 'fsqrt' : 'fabs'} ${dreg(k)}, ${dreg(k)}`,
    // fcmp says unordered for a NaN, which isn't eq
    branchIfFloatZero: (k, label) => [`    fcmp ${dreg(k)}, #0.0`, `    b.eq ${label}`],
    boxFloat: BOX_D0,
    branchUnlessFloatCompare: (cond, label, n) => [fcmp(n), `    b.${FNOT[cond]} ${label}`],

    arith: (op, overflow) => ARITH[op](overflow),
    // adds or subs with the word as an immediate when it fits twelve bits,
    // turned round for a negative one: x + -2 is x - 2, which overflows
    // when x + -2 does.
    arithWord: (op, word, overflow) => {
        const w     = BigInt.asIntN(64, word);
        const n     = w < 0n ? -w : w;
        const ins   = (op === '+') === (w >= 0n) ? 'adds' : 'subs';
        const apply = n <= 4095n ? `    ${ins} x0, x0, #${n}` : [loadWord('x2', word), `    ${op === '+' ? 'adds' : 'subs'} x0, x0, x2`];
        return [apply, `    b.vs ${overflow}`];
    },
    divide: (op, zero, overflow) => DIVISION[op](zero, overflow),
    bits: (op, range) => BITS[op](range),
    bitNot: '    eor  x0, x0, #-2',

    cxr: (step) => (step === 'a' ? '    ldur x0, [x0, #-1]    // car' : '    ldur x0, [x0, #7]     // cdr'),
    // ldur reaches 255 bytes on; past that, the offset is in a register
    tupleRef: (i) => tupleElement(i) <= 255
        ? `    ldur x0, [x0, #${tupleElement(i)}]    // element ${i}`
        : [loadWord('x1', BigInt(tupleElement(i))), `    ldr  x0, [x0, x1]    // element ${i}`],

    // Bump-allocates into x2. When the chunk hasn't room, rt_heap_grow makes
    // a new one (or faults, past the heap's limit) and the allocation goes
    // again; x0 and x1 survive that. Clobbers x3, x4 and x5.
    allocate: (bytes, again, site) => [
        [
            `${again}:`,
            '    ldr  x2, [x28, #RT_PROC_HEAP_PTR]',
            '    ldr  x3, [x28, #RT_PROC_HEAP_LIMIT]',
            addImm('x4', 'x2', bytes, 'x5'),
            '    cmp  x4, x3',
            `    b.hi ${again}_grow`,
            '    str  x4, [x28, #RT_PROC_HEAP_PTR]',
        ],
        [
            `${again}_grow:`,
            '    stp  x0, x1, [sp, #-16]!',
            loadWord('x0', BigInt(bytes)),
            `    LOADADDR x1, ${site}`,
            '    bl   rt_heap_grow',
            '    ldp  x0, x1, [sp], #16',
            `    b    ${again}`,
        ],
    ],
    consCell: (headSlot) => [`    ldr  x1, ${slot(headSlot)}`, '    stp  x1, x0, [x2]', '    orr  x0, x2, #RT_TAG_LIST'],
    // The cells side by side, each one's cdr the next.
    listCells: (firstSlot, n) => [
        [...Array(n).keys()].map((i) => [
            `    ldr  x5, ${slot(firstSlot + i)}`,
            `    str  x5, [x2, #${16 * i}]`,
            i + 1 < n ? addImm('x6', 'x2', 16 * (i + 1) + 1, 'x7') : '    mov  x6, #RT_NIL',
            `    str  x6, [x2, #${16 * i + 8}]`,
        ]),
        '    orr  x0, x2, #RT_TAG_LIST',
    ],
    tupleCells: (header, firstSlot, n) => [
        loadWord('x3', header),
        '    str  x3, [x2]',
        [...Array(n).keys()].map((i) => [`    ldr  x5, ${slot(firstSlot + i)}`, `    str  x5, [x2, #${8 * (i + 1)}]`]),
        '    orr  x0, x2, #RT_TAG_BOXED',
    ],
    // The header (how many values it captured, and the closure type), the
    // code, the arity, the name (for printing), then the captured values.
    makeClosure: (entry, arity, name, free) => [
        `    mov  x3, #${free.length} << RT_BOX_SIZE_SHIFT | RT_BOX_CLOSURE`,
        `    LOADADDR x4, ${entry}`,
        '    stp  x3, x4, [x2]',
        `    mov  x3, #${arity}`,
        `    LOADADDR x4, ${name}`,
        '    stp  x3, x4, [x2, #16]',
        free.map(([si, note], i) => [`    ldr  x3, ${slot(si)}${comment(note)}`, `    str  x3, [x2, #${32 + 8 * i}]`]),
        '    orr  x0, x2, #RT_TAG_BOXED',
    ],
};
