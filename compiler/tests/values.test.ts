// The compiler's idea of how values are represented (values.ts) must match
// the runtime's (runtime/rt.h).

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { FAULT_KINDS, intWord, RESERVED_SYMBOLS, RUNTIME_SYMBOLS, symbolWord } from '../src/values.ts';

const RT_H = readFileSync(new URL('../../runtime/rt.h', import.meta.url), 'utf8');

function define(name: string): bigint {
    const m = new RegExp(`^#define ${name}\\s+(\\d+)`, 'm').exec(RT_H);
    assert.ok(m, `rt.h defines ${name}`);
    return BigInt(m[1]!);
}

test('integers are n << 1, with a 0 tag bit', () => {
    assert.equal(define('RT_TAG_INT_MASK'), 1n);
    assert.equal(intWord(21n), 42n);
    assert.equal(intWord(-1n) & define('RT_TAG_INT_MASK'), 0n);
});

test('symbols are id << RT_SYMBOL_SHIFT | RT_TAG_SYMBOL', () => {
    assert.equal(symbolWord(7), (7n << define('RT_SYMBOL_SHIFT')) | define('RT_TAG_SYMBOL'));
});

test('#false and #true are symbols 0 and 1', () => {
    assert.equal(symbolWord(RESERVED_SYMBOLS.indexOf('#false')), define('RT_FALSE'));
    assert.equal(symbolWord(RESERVED_SYMBOLS.indexOf('#true')), define('RT_TRUE'));
});

test("the runtime's symbols follow #true, as rt.h numbers them", () => {
    const id = (name: string): number => RESERVED_SYMBOLS.length + RUNTIME_SYMBOLS.indexOf(name);
    assert.equal(BigInt(id('ok')), define('RT_SYM_OK'));
    assert.equal(BigInt(id('error')), define('RT_SYM_ERROR'));
    assert.equal(BigInt(id('exit')), define('RT_SYM_EXIT'));
    assert.equal(BigInt(id('killed')), define('RT_SYM_KILLED'));
    assert.equal(BigInt(id(FAULT_KINDS[0]!)), define('RT_SYM_FAULTS'));
});

test("the fault kinds are rt.h's, in order", () => {
    const faults = [...RT_H.matchAll(/^#define RT_FAULT_\w+\s+(\d+)\s+\/\/ :(\S+)/gm)];
    assert.equal(BigInt(faults.length), define('RT_FAULT_COUNT'));
    assert.deepEqual(faults.map((m) => Number(m[1])), FAULT_KINDS.map((_, i) => i + 1));
    assert.deepEqual(faults.map((m) => m[2]), FAULT_KINDS);
});

test('nil is the list tag on a null pointer', () => {
    assert.equal(define('RT_NIL'), define('RT_TAG_LIST'));
});
