// Tests for placeArgs, which orders the moves into argument registers for
// every target, and for the x86-64 target's output. What the generated code
// *does* is tested by the golden tests in t/, on both targets.

import test from 'node:test';
import assert from 'node:assert/strict';

import { compileProgram } from '../src/codegen.ts';
import { read } from '../src/reader.ts';
import { NIL } from '../src/sexp.ts';
import { ACC, addr, imm, inSlot, LEFT, placeArgs, PROC, type Code, type Operand } from '../src/target.ts';
import { X86_64 } from '../src/x86_64.ts';

const flat = (code: Code): string[] => (typeof code === 'string' ? [code] : code.flatMap(flat));

// A machine with registers a, b, c, d: the accumulator is in a and the left
// operand in b, so a call whose first two arguments are those swaps them.
const place = (args: readonly Operand[]): string[] => flat(placeArgs(
    args, ['a', 'b', 'c', 'd'],
    (op) => (op.t === 'acc' ? 'a' : op.t === 'left' ? 'b' : op.t === 'proc' ? 'p' : null),
    (dst, op) => `${dst} <- ${op.t === 'imm' ? op.v : op.t === 'addr' ? op.label : op.t}`,
    (dst, src) => `${dst} <- ${src}`,
));

// Plays the moves out, to check they put each operand where it belongs.
function run(moves: readonly string[], regs: Readonly<Record<string, string>>): Record<string, string> {
    const state: Record<string, string> = { ...regs };
    for (const m of moves) {
        const [dst, src] = m.split(' <- ') as [string, string];
        state[dst] = src in state ? state[src]! : src;
    }
    return state;
}

test('placeArgs moves registers first, without overwriting one before it is read', () => {
    assert.deepEqual(place([ACC]), []);
    assert.deepEqual(place([imm('FAULT'), ACC, addr('site')]), ['b <- a', 'a <- FAULT', 'c <- site']);
    assert.deepEqual(place([inSlot(3), ACC]), ['b <- a', 'a <- slot']);
    assert.deepEqual(place([PROC]), ['a <- p']);
});

test('placeArgs breaks a cycle through the first register no move uses', () => {
    const moves = place([LEFT, ACC, imm('OP'), addr('site')]);
    assert.deepEqual(moves, ['c <- a', 'a <- b', 'b <- c', 'c <- OP', 'd <- site']);
    const state = run(moves, { a: 'acc', b: 'left', c: '?', d: '?' });
    assert.deepEqual(state, { a: 'left', b: 'acc', c: 'OP', d: 'site' });
});

const x86 = (src: string): string => compileProgram(read(src, 'test.slight'), NIL, X86_64);

test('x86-64: a program includes its header, and a frame keeps rsp 16-aligned', () => {
    const asm = x86('(defun f (a b c) (+ a (+ b c))) (f 1 2 3)');
    assert.match(asm, /#include "asm_x86_64.h"/);
    assert.match(asm, /^FUNC slight_main$/m);
    // f: rbp pushed, then slots a multiple of 16 (a, b, c and two waiting operands)
    assert.match(asm, /fn_f: {4}\/\/ f\n {4}push rbp\n {4}mov {2}rbp, rsp\n {4}sub {2}rsp, 48\n/);
    assert.match(asm, /mov {2}qword ptr \[rsp \+ 16\], rdx {4}\/\/ c/);
});

test('x86-64: a tail call takes the frame down and jumps; any other call returns', () => {
    const asm = x86('(defun f (n) (cond ((== n 0) 0) (#true (+ 1 (g n))))) (defun g (n) (f (- n 1))) (f 3)');
    assert.match(asm, /mov {2}rsp, rbp\n {4}pop {2}rbp\n {4}jmp {2}fn_f {4}\/\/ tail call/);
    assert.match(asm, /call fn_g/);
});

test('x86-64: a closure is called through rax, which its code finds it in', () => {
    const asm = x86('(let k 7) (let f (lambda (x) (+ x k))) (pprint (f 1)) (f 2)');
    assert.match(asm, /mov {2}rax, qword ptr \[rsp \+ \d+\]\n {4}mov {2}r11d, eax\n/);
    assert.match(asm, /call qword ptr \[rax \+ RT_CLOSURE_CODE\]/);
    assert.match(asm, /mov {2}rsp, rbp\n {4}pop {2}rbp\n {4}jmp {2}qword ptr \[rax \+ RT_CLOSURE_CODE\] {4}\/\/ tail call/);
    // inside the lambda, k comes out of the closure after x is stored
    assert.match(asm, /mov {2}qword ptr \[rsp \+ 0\], rdi {4}\/\/ x\n {4}mov {2}r11, qword ptr \[rax \+ RT_CLOSURE_FREE \+ 0\]\n {4}mov {2}qword ptr \[rsp \+ 8\], r11 {4}\/\/ k/);
});

test('x86-64: the slow path of a comparison reads rcx before it holds the site', () => {
    const asm = x86('(< 1 2.0)');
    assert.match(asm, /mov {2}rdi, rcx\n {4}mov {2}rsi, rax\n {4}mov {2}rdx, RT_CMP_LT\n {4}lea {2}rcx, \[rip \+ Lsite_\d+\]\n {4}call rt_compare/);
});

test('x86-64: words that fit 32 bits are one mov, others movabs', () => {
    assert.match(x86('-3'), /mov {2}rax, -6 {4}\/\/ -3/);
    assert.match(x86('4611686018427387903'), /movabs rax, 0x7ffffffffffffffe {4}\/\/ 4611686018427387903/);
});

test('x86-64: heap growth keeps the accumulator and left operand, two pushes deep', () => {
    const asm = x86('(cons 1 (list 2))');
    assert.match(asm, /_grow:\n {4}push rax\n {4}push rcx\n {4}mov {2}rdi, 16\n {4}lea {2}rsi, \[rip \+ Lsite_\d+\]\n {4}call rt_heap_grow\n {4}pop {2}rcx\n {4}pop {2}rax\n/);
});
