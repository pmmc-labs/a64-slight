// Tests for code generation. What the generated code *does* is tested by
// the golden tests in t/; these check the pieces that are easy to get
// subtly wrong.

import test from 'node:test';
import assert from 'node:assert/strict';

import { compileProgram, loadWord } from '../src/codegen.ts';
import { CompileError } from '../src/errors.ts';
import { read } from '../src/reader.ts';

// Runs a movz/movn/movk sequence and returns the register's value.
function simulate(lines: readonly string[]): bigint {
    const MASK = (1n << 64n) - 1n;
    return lines.reduce((reg, line) => {
        const m = /^\s*(movz|movn|movk) x0, #(0x[0-9a-f]+|0), lsl #(\d+)\b/.exec(line) ?? /^\s*(movz|movn) x0, #(0)$/.exec(line);
        assert.ok(m, `unexpected instruction: ${line}`);
        const imm   = BigInt(m[2]!);
        const shift = BigInt(m[3] ?? 0);
        switch (m[1]) {
            case 'movz': return imm << shift;
            case 'movn': return ~(imm << shift) & MASK;
            default:     return (reg & ~(0xffffn << shift) & MASK) | (imm << shift);
        }
    }, 0n);
}

// A deterministic spread of 64-bit words.
function* words(n: number): Generator<bigint> {
    let x = 0x9e3779b97f4a7c15n;
    for (let i = 0; i < n; i++) {
        x = (x * 6364136223846793005n + 1442695040888963407n) & ((1n << 64n) - 1n);
        yield x;
    }
}

test('loadWord builds every word it is given', () => {
    const edges = [0n, 1n, 2n, -1n, -2n, 0xffffn, 0x10000n, 0xffff0000ffff0000n, 0x123456789abcdef0n, -(2n ** 63n), 2n ** 63n - 1n];
    for (const w of [...edges, ...words(2000)]) {
        assert.equal(simulate(loadWord('x0', w)), BigInt.asUintN(64, w), `0x${BigInt.asUintN(64, w).toString(16)}`);
    }
});

test('loadWord uses one instruction for small and small negative words', () => {
    for (const w of [0n, 84n, 0xffffn, -1n, -14n, -0x10000n]) {
        assert.equal(loadWord('x0', w).length, 1, w.toString());
    }
});

test('loadWord never needs more than four instructions', () => {
    for (const w of words(2000)) assert.ok(loadWord('x0', w).length <= 4);
});

const compile = (src: string): string => compileProgram(read(src, 'test.slight'));

test('a program is the function slight_main', () => {
    const asm = compile('42');
    assert.match(asm, /#include "asm.h"/);
    assert.match(asm, /^FUNC slight_main$/m);
    assert.match(asm, /movz x0, #0x54, lsl #0 {4}\/\/ 42/);
    assert.match(asm, /^ {4}ret$/m);
});

test('every form is compiled, in order', () => {
    const asm = compile('1 2 3');
    const loads = asm.split('\n').filter((l) => l.includes('movz'));
    assert.deepEqual(loads.map((l) => l.split('// ')[1]), ['1', '2', '3']);
});

test('an empty program is an error', () => {
    assert.throws(() => compile('; nothing'), (e: unknown) => e instanceof CompileError && /empty/.test(e.message));
});

test('what step 0 does not support is an error, with a position', () => {
    assert.throws(() => compile('42\n  "hi"'), (e: unknown) =>
        e instanceof CompileError && e.message === 'test.slight:2:3: not supported yet: "hi"');
    assert.throws(() => compile('(+ 1 2)'), (e: unknown) =>
        e instanceof CompileError && e.message === 'test.slight:1:1: not supported yet: (+ 1 2)');
});
