// The compiler's idea of how values are represented (values.ts) must match
// the runtime's (runtime/rt.h).

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { DEVICE_WORDS, ERRNO_NAMES, FAULT_KINDS, intWord, JSON_WORDS, KEY_NAMES, RESERVED_SYMBOLS, RUNTIME_SYMBOLS, SEXP_WORDS, symbolWord } from '../src/values.ts';

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
    assert.equal(BigInt(id(KEY_NAMES[0]!)), define('RT_SYM_KEYS'));
    assert.equal(BigInt(id(DEVICE_WORDS[0]!)), define('RT_SYM_DEVICE'));
    assert.equal(BigInt(id(ERRNO_NAMES[0]!)), define('RT_SYM_ERRS'));
    assert.equal(BigInt(id(JSON_WORDS[0]!)), define('RT_SYM_JSON'));
    assert.equal(BigInt(id(SEXP_WORDS[0]!)), define('RT_SYM_SEXP'));
});

test("the fault kinds are rt.h's, in order", () => {
    const faults = [...RT_H.matchAll(/^#define RT_FAULT_\w+\s+(\d+)\s+\/\/ :(\S+)/gm)];
    assert.equal(BigInt(faults.length), define('RT_FAULT_COUNT'));
    assert.deepEqual(faults.map((m) => Number(m[1])), FAULT_KINDS.map((_, i) => i + 1));
    assert.deepEqual(faults.map((m) => m[2]), FAULT_KINDS);
});

test("the key names are rt.h's, in order", () => {
    const keys = [...RT_H.matchAll(/^#define RT_KEY_\w+\s+(\d+)\s+\/\/ :(\S+)/gm)];
    assert.equal(BigInt(keys.length), define('RT_KEY_COUNT'));
    assert.deepEqual(keys.map((m) => Number(m[1])), KEY_NAMES.map((_, i) => i));
    assert.deepEqual(keys.map((m) => m[2]), KEY_NAMES);
});

test("a device's words, the errno names and the data's words are rt.h's, in order", () => {
    const groups = [
        ['RT_DEV_', DEVICE_WORDS, 'RT_DEV_COUNT'], ['RT_ERR_', ERRNO_NAMES, 'RT_ERR_COUNT'],
        ['RT_JSON_', JSON_WORDS, 'RT_JSON_COUNT'], ['RT_SEXP_', SEXP_WORDS, 'RT_SEXP_COUNT'],
    ] as const;
    for (const [prefix, names, count] of groups) {
        const found = [...RT_H.matchAll(new RegExp(`^#define ${prefix}\\w+\\s+(\\d+)\\s+// :(\\S+)`, 'gm'))];
        assert.equal(BigInt(found.length), define(count));
        assert.deepEqual(found.map((m) => Number(m[1])), names.map((_, i) => i));
        assert.deepEqual(found.map((m) => m[2]), names);
    }
});

test('nil is the list tag on a null pointer', () => {
    assert.equal(define('RT_NIL'), define('RT_TAG_LIST'));
});
