// Tests for the reader: booleans are symbols, integers are 63-bit, strings
// take \r and \e among their escapes, and there is no quasiquote.

import test from 'node:test';
import assert from 'node:assert/strict';

import { CompileError } from '../src/errors.ts';
import { read } from '../src/reader.ts';
import { show, toArray, type Sexp } from '../src/sexp.ts';

const forms = (src: string): readonly Sexp[] => toArray(read(src, 'test.slight'))!;
const one = (src: string): Sexp => {
    const xs = forms(src);
    assert.equal(xs.length, 1);
    return xs[0]!;
};
const shows = (src: string): string => forms(src).map(show).join(' ');
const fails = (src: string, message: RegExp): void => {
    assert.throws(() => read(src, 'test.slight'), (e: unknown) => e instanceof CompileError && message.test(e.message));
};

// --- numbers ----------------------------------------------------------------

test('integers', () => {
    assert.deepEqual(forms('42 -7 0').map((x) => x.t === 'int' && x.v), [42n, -7n, 0n]);
});

test('integers are 63-bit', () => {
    assert.equal(show(one('4611686018427387903')), '4611686018427387903');
    assert.equal(show(one('-4611686018427387904')), '-4611686018427387904');
    fails('4611686018427387904', /integer out of range/);
    fails('-4611686018427387905', /integer out of range/);
});

test('floats', () => {
    const vs = forms('3.5 -0.25 1.0e3 2.5E2 1.5e-2 1.0e+3').map((x) => x.t === 'float' && x.v);
    assert.deepEqual(vs, [3.5, -0.25, 1000, 250, 0.015, 1000]);
});

test('floats need digits on both sides of the point', () => {
    for (const s of ['.5', '5.', '1e3', '1.0e', '1.e3', '-.5']) {
        const x = one(s);
        assert.equal(x.t, 'sym', s);
    }
});

test('things that start like numbers are symbols', () => {
    for (const s of ['123abc', '-', '-foo', '+', '1-2']) {
        const x = one(s);
        assert.ok(x.t === 'sym' && x.name === s, s);
    }
});

// --- booleans and symbols ---------------------------------------------------

test('#true and #false are symbols', () => {
    const [t, f] = forms('#true #false');
    assert.ok(t!.t === 'sym' && t!.name === '#true');
    assert.ok(f!.t === 'sym' && f!.name === '#false');
});

test('other # literals are errors', () => {
    fails('#maybe', /unknown literal '#maybe'/);
});

test('symbols', () => {
    assert.equal(shows('name list/map IO::print a-b $$ ^$$ eq? str->int'), 'name list/map IO::print a-b $$ ^$$ eq? str->int');
});

test('the control-character names read as symbols', () => {
    const xs = forms('\\e \\n \\r \\t');
    assert.deepEqual(xs.map((x) => x.t === 'sym' && x.name), ['\\e', '\\n', '\\r', '\\t']);
});

// --- strings ----------------------------------------------------------------

test('strings', () => {
    const x = one('"hello"');
    assert.ok(x.t === 'str' && x.v === 'hello');
});

test('string escapes', () => {
    const x = one('"q\\" b\\\\ n\\n t\\t r\\r e\\e"');
    assert.ok(x.t === 'str');
    assert.equal(x.v, 'q" b\\ n\n t\t r\r e\x1b');
});

test('unicode escapes', () => {
    const [a, b] = forms('"\\u{41}" "\\u{1F600}"');
    assert.ok(a!.t === 'str' && a!.v === 'A');
    assert.ok(b!.t === 'str' && b!.v === '😀');
});

test('bad escapes are errors', () => {
    fails('"a\\xb"', /invalid escape '\\x'/);
    fails('"\\u41"', /invalid unicode escape/);
    fails('"\\u{41"', /unterminated unicode escape/);
    fails('"\\u{110000}"', /invalid unicode escape/);
    fails('"\\u{D800}"', /invalid unicode escape/);
    fails('"\\u{dfff}"', /invalid unicode escape/);
    fails('"\\u{}"', /invalid unicode escape/);
    fails('"\\u{zz}"', /invalid unicode escape/);
});

test('unterminated strings are errors', () => {
    fails('"hello', /test.slight:1:1: unterminated string/);
    fails('"hello\\', /unterminated string/);
});

test('strings can span lines, and positions keep counting', () => {
    const [s, x] = forms('"a\nb" x');
    assert.ok(s!.t === 'str' && s!.v === 'a\nb');
    assert.deepEqual(x!.t === 'sym' && x!.pos, { file: 'test.slight', line: 2, col: 4 });
});

// --- quote and keywords -----------------------------------------------------

test("'x is (quote x)", () => {
    assert.equal(shows("'x '(a b) ''x"), '(quote x) (quote (a b)) (quote (quote x))');
});

test(':x is (quote x), positioned at the colon', () => {
    const x = one('  :ping');
    assert.equal(show(x), '(quote ping)');
    assert.deepEqual(x.t === 'pair' && x.pos, { file: 'test.slight', line: 1, col: 3 });
});

test('keywords can contain colons', () => {
    assert.equal(show(one(':a::b')), '(quote a::b)');
});

test('bad keywords are errors', () => {
    for (const s of [':', ': ', ':12', ':-3', ':1.5', ':#foo', '::foo', ":'foo", ':(']) {
        fails(s, /invalid keyword/);
    }
});

test("a ' with nothing after it is an error", () => {
    fails("'", /expected something to quote/);
});

test('quasiquote is not part of slight', () => {
    fails('`x', /quasiquote/);
    fails(',x', /quasiquote/);
    fails(',@x', /quasiquote/);
    fails('a,b', /quasiquote/);
    fails('a`b', /quasiquote/);
});

// --- lists ------------------------------------------------------------------

test('lists', () => {
    assert.equal(shows('() (a b c) ((a) (b c)) (42 "hi" #true 1.5)'), '() (a b c) ((a) (b c)) (42 "hi" #true 1.5)');
});

test('dotted pairs are not part of slight', () => {
    fails('(a . b)', /dotted pairs/);
});

test('unbalanced parentheses are errors', () => {
    fails('(a (b c)', /test.slight:1:1: unterminated list/);
    fails(')', /test.slight:1:1: unexpected '\)'/);
    fails('(a))', /test.slight:1:4: unexpected '\)'/);
});

// --- comments and whitespace ------------------------------------------------

test('comments run to the end of the line', () => {
    assert.equal(shows('42 ; a comment\n"hi" ; another'), '42 "hi"');
    assert.equal(shows('; only a comment'), '');
});

test('whitespace', () => {
    assert.equal(shows('  \n\t 42 \r\n "x"  '), '42 "x"');
});

// --- positions --------------------------------------------------------------

test('atoms have positions', () => {
    const [a, b] = forms('x\n  42');
    assert.deepEqual(a!.t === 'sym' && a!.pos, { file: 'test.slight', line: 1, col: 1 });
    assert.deepEqual(b!.t === 'int' && b!.pos, { file: 'test.slight', line: 2, col: 3 });
});

test("a list's position is its opening paren", () => {
    const [, l] = forms('x\n (a b)');
    assert.deepEqual(l!.t === 'pair' && l!.pos, { file: 'test.slight', line: 2, col: 2 });
});

test('elements inside a list have their own positions', () => {
    const l = toArray(one('(defun f (x)\n    (+ x 1))'))!;
    const body = toArray(l[3]!)!;
    assert.deepEqual(body[1]!.t === 'sym' && body[1]!.pos, { file: 'test.slight', line: 2, col: 8 });
});
