// Tests for the recv rule's classification: which functions can reach a recv.

import test from 'node:test';
import assert from 'node:assert/strict';

import { isReceiveBody, patternNames, stateFunctions } from '../src/classify.ts';
import { read } from '../src/reader.ts';
import { toArray, type Sexp } from '../src/sexp.ts';

// The state functions among the defuns in src.
function state(src: string): readonly string[] {
    const defuns = toArray(read(src, 'test.slight'))!.map((d) => {
        const [, name, params, ...body] = toArray(d)!;
        return {
            name:   (name as { name: string }).name,
            params: toArray(params!)!.map((p) => (p as { name: string }).name),
            body:   body.reduceRight<Sexp>((tail, x) => ({ t: 'pair', car: x, cdr: tail, pos: null }), { t: 'nil' }),
        };
    });
    return [...stateFunctions(defuns)].sort();
}

test('a receive function is a defun whose whole body is one recv', () => {
    assert.deepEqual(state('(defun w () (recv (m m)))'), ['w']);
    assert.deepEqual(state('(defun f () (pprint 1) (recv (m m)))'), []);
    assert.equal(isReceiveBody(read('(recv (m m))', 't')), true);
    assert.equal(isReceiveBody(read('(recv (m m)) 1', 't')), false);
});

test('tail-calling a state function makes a state function, transitively', () => {
    const src = `(defun w () (recv (m m)))
                 (defun a () (w))
                 (defun b (n) (cond ((== n 0) (a)) (#true (pprint n))))
                 (defun c () (do (pprint 1) (let x 1) (b x)))
                 (defun plain () (pprint (+ 1 2)))`;
    assert.deepEqual(state(src), ['a', 'b', 'c', 'w']);
});

test('mutual recursion through tail calls', () => {
    assert.deepEqual(state('(defun w () (recv (m (p)))) (defun p () (q)) (defun q () (w))'), ['p', 'q', 'w']);
});

test("calls that aren't in tail position, or are in lambdas or forks, don't count", () => {
    const src = `(defun w () (recv (m m)))
                 (defun not-tail () (pprint (w)))
                 (defun in-lambda () (lambda () (w)))
                 (defun in-fork () (fork (w)))
                 (defun in-cond-test () (cond ((w) 1)))`;
    assert.deepEqual(state(src), ['w']);
});

test('a local of the same name hides the function', () => {
    assert.deepEqual(state('(defun w () (recv (m m))) (defun f (w) (w))'), ['w']);
    assert.deepEqual(state('(defun w () (recv (m m))) (defun f () (let w 1) (w))'), ['w']);
});

test('yield and recv clause bodies are tail positions', () => {
    assert.deepEqual(state('(defun w () (recv (m m))) (defun y () (yield (w)))'), ['w', 'y']);
    assert.deepEqual(state('(defun w () (recv (m (w))))'), ['w']);
});

test('the names a pattern binds', () => {
    const names = (src: string): readonly string[] => patternNames(read(src, 't').t === 'pair' ? (read(src, 't') as { car: Sexp }).car : read(src, 't'));
    assert.deepEqual(names('msg'), ['msg']);
    assert.deepEqual(names('_'), []);
    assert.deepEqual(names(':stop'), []);
    assert.deepEqual(names('(:pair a _ b)'), ['a', 'b']);
});
