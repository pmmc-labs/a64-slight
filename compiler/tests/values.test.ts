// The compiler's idea of how values are represented (values.ts) must match
// the runtime's (runtime/rt.h).

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { DEVICE_WORDS, ERRNO_NAMES, EXEC_WORDS, FAULT_KINDS, FLOAT_HEADER, FLOAT_VALUE, intWord, JSON_WORDS, KEY_NAMES, KIND_WORDS, MAX_ARGS, PEER_WORDS, SOURCE_WORDS, RESERVED_SYMBOLS, TABLE_WORDS, RUNTIME_SYMBOLS, SEXP_WORDS, symbolWord, SYSTEM_WORDS, TAG_BOXED, tupleElement, tupleHeader } from '../src/values.ts';

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

test("a tuple's header is n << RT_BOX_SIZE_SHIFT | RT_BOX_TUPLE, and its elements follow it", () => {
    assert.equal(BigInt(TAG_BOXED), define('RT_TAG_BOXED'));
    assert.equal(tupleHeader(3), (3n << define('RT_BOX_SIZE_SHIFT')) | define('RT_BOX_TUPLE'));
    assert.equal(tupleElement(0), 8 - TAG_BOXED);
    assert.equal(tupleElement(2), 24 - TAG_BOXED);
});

test("a float's header is 8 << RT_BOX_SIZE_SHIFT | RT_BOX_FLOAT, and its double follows it", () => {
    assert.equal(FLOAT_HEADER, (8n << define('RT_BOX_SIZE_SHIFT')) | define('RT_BOX_FLOAT'));
    assert.equal(FLOAT_VALUE, 8 - Number(define('RT_TAG_BOXED')));
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
    assert.equal(BigInt(id(SYSTEM_WORDS[0]!)), define('RT_SYM_SYSTEM'));
    assert.equal(BigInt(id(EXEC_WORDS[0]!)), define('RT_SYM_EXEC'));
    assert.equal(BigInt(id(PEER_WORDS[0]!)), define('RT_SYM_PEER'));
    assert.equal(BigInt(id(KIND_WORDS[0]!)), define('RT_SYM_KIND'));
    assert.equal(BigInt(id(SOURCE_WORDS[0]!)), define('RT_SYM_SOURCE'));
    assert.equal(BigInt(id(TABLE_WORDS[0]!)), define('RT_SYM_TABLE'));
});

test('no name is in the runtime\'s symbols twice', () => {
    assert.deepEqual(RUNTIME_SYMBOLS.filter((name, i) => RUNTIME_SYMBOLS.indexOf(name) !== i), []);
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
        ['RT_SYS_', SYSTEM_WORDS, 'RT_SYS_COUNT'], ['RT_EXEC_', EXEC_WORDS, 'RT_EXEC_COUNT'],
        ['RT_PEER_', PEER_WORDS, 'RT_PEER_COUNT'], ['RT_KIND_', KIND_WORDS, 'RT_KIND_COUNT'],
        ['RT_SOURCE_', SOURCE_WORDS, 'RT_SOURCE_COUNT'], ['RT_TABLE_', TABLE_WORDS, 'RT_TABLE_COUNT'],
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

test('a call passes as many arguments as a waiting process keeps', () => {
    assert.equal(BigInt(MAX_ARGS), define('RT_MAX_ARGS'));
});
