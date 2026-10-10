// Tests for splitting functions at recv (split.ts, D167).

import test from 'node:test';
import assert from 'node:assert/strict';

import { compileProgram } from '../src/codegen.ts';
import { CompileError } from '../src/errors.ts';
import { read } from '../src/reader.ts';
import { show, toArray, type Sexp } from '../src/sexp.ts';
import { splitProgram } from '../src/split.ts';

const split = (src: string): readonly string[] => toArray(splitProgram(read(src, 't')))!.map(show);

// The generated defun called name, as its parts.
function defun(src: string, name: string): readonly Sexp[] {
    const d = toArray(splitProgram(read(src, 't')))!.find((f) => {
        const parts = toArray(f);
        return parts !== null && parts[1]!.t === 'sym' && parts[1]!.name === name;
    });
    assert.ok(d, `no defun ${name}`);
    return toArray(d)!;
}

const fails = (src: string, message: RegExp): void => {
    assert.throws(() => compileProgram(read(src, 't')), (e: unknown) => e instanceof CompileError && message.test(e.message));
};

test('a receive function, and a function with no recv, stay as they are', () => {
    for (const src of ['(defun w (n) (recv ((:inc) (w (+ n 1))) (:stop n)))', '(defun f (x) (pprint x) (f (+ x 1)))']) {
        assert.deepEqual(split(src), [show(toArray(read(src, 't'))![0]!)]);
    }
});

test('what comes before a recv stays, and the recv and the rest become a receive function', () => {
    assert.deepEqual(split('(defun ask (c) (send c 1) (let n (recv ((:count n) n))) (list c n))'), [
        '(defun ask (c) (send c 1) (ask (recv at t:1:34) c))',
        '(defun ask (recv at t:1:34) (c) (recv (((quote count) n) (let n (do n)) (list c n))))',
    ]);
});

test('a recv inside a call is taken out in the order things are evaluated', () => {
    // (g) is evaluated before the recv, so it's taken out first; x, a
    // name, is the same whenever it's evaluated, so it stays.
    assert.deepEqual(split('(defun k (x) (f (g) x (recv) (h)))'), [
        '(defun k (x) (let (argument 1 of the call at t:1:14) (g)) (k (recv at t:1:23) x (argument 1 of the call at t:1:14)))',
        '(defun k (recv at t:1:23) (x (argument 1 of the call at t:1:14)) (recv (message (recv at t:1:23)'
            + ' (let (recv at t:1:23) (do message (recv at t:1:23))) (f (argument 1 of the call at t:1:14) x (recv at t:1:23) (h)))))',
    ]);
});

test('a recv with several clauses and a rest gets a function for the rest', () => {
    const src = '(defun f (x) (let y (recv ((:a v) v) ((:b w) w))) (list x y))';
    assert.deepEqual(defun(src, 'f (after recv at t:1:21)').map(show).slice(2), ['(x y)', '(list x y)']);
    assert.equal(show(defun(src, 'f')[3]!), '(f (recv at t:1:21) x)');
});

test('a name a pattern binds comes across under another name if the rest needs the one from before', () => {
    // the rest's x is f's; in the clause, x is the message's
    const params = defun('(defun f (x) (let y (recv ((:a x) x))) (list x y))', 'f (recv at t:1:21)')[2]!;
    assert.deepEqual(toArray(params)!.map(show), ['x (before recv at t:1:21)']);
    assert.doesNotThrow(() => compileProgram(read('(defun f (x) (let y (recv ((:a x) x) ((:b z) z))) (list x y)) (f 1)', 't')));
});

test('more than 8 locals come across as one list', () => {
    const src = '(let a 1) (let b 2) (let c 3) (let d 4) (let e 5) (let f 6) (let g 7) (let h 8) (let i 9) (recv (m (list a b c d e f g h i m)))';
    const g = defun(src, 'the top level (recv at t:1:91)');
    assert.equal(toArray(g[2]!)!.length, 1);
    assert.doesNotThrow(() => compileProgram(read(src, 't')));
    assert.throws(() => compileProgram(read(src.replace('(let i 9)', '(let car 9)').replace(' i m', ' car m'), 't')),
        (e: unknown) => e instanceof CompileError && /a local is named car/.test(e.message));
});

test('@ARGV comes across under another name, since it can\'t be a parameter', () => {
    const src = '(let m (recv)) (pprint (list m @ARGV))';
    assert.deepEqual(toArray(defun(src, 'the top level (recv at t:1:8)')[2]!)!.map(show), ['@ARGV (carried across recv at t:1:8)']);
    assert.doesNotThrow(() => compileProgram(read(src, 't')));
});

test('a recv in a fork is split in the new process', () => {
    assert.deepEqual(split('(defun p (x) (fork (do (send x 1) (recv))))'), [
        '(defun p (x) (fork (do (send x 1) (p (recv at t:1:35)))))',
        '(defun p (recv at t:1:35) () (recv (message (recv at t:1:35) message (recv at t:1:35))))',
    ]);
});

test('a recv in a cond test takes the clauses from there into a cond of their own', () => {
    assert.deepEqual(split('(defun g (a) (cond (a 1) ((recv) 2) (#true 3)))')[0],
        '(defun g (a) (cond (a 1) (#true (g (recv at t:1:27)))))');
});

test('where a recv can\'t be', () => {
    fails('(defun f () (map (lambda (x) (recv)) (list 1)))', /recv can't be in a lambda/);
    fails('(defun f () (pprint (cond (#true (recv)))))', /cond that's in tail position/);
});
