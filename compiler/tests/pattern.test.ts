// Tests for patterns (pattern.ts): the names they bind, the tests and
// bindings they become, and the patterns that aren't. What a match or a
// recv does with them is tested by the golden tests in t/.

import test from 'node:test';
import assert from 'node:assert/strict';

import { CompileError } from '../src/errors.ts';
import { BUILTIN, lowerPattern, ON_CONSES, patternNames } from '../src/pattern.ts';
import { read } from '../src/reader.ts';
import { show, sym, type Pair, type Sexp } from '../src/sexp.ts';

const pattern = (src: string): Sexp => (read(src, 't.slight') as Pair).car;

// The tests and bindings, as text, with the builtins' hidden names shown
// as the names they hide, and a tuple pattern's as tuple-of? and tuple/get.
function lowered(src: string): { tests: string[]; binds: string[] } {
    const { tests, binds } = lowerPattern(pattern(src), sym('v'));
    const plain = (x: Sexp): string => show(x).split(BUILTIN).join('').split(ON_CONSES).join('').split(' (pattern)').join('');
    return { tests: tests.map(plain), binds: binds.map(([name, part]) => `${name.name} = ${plain(part)}`) };
}

test('the names a pattern binds, in order', () => {
    const names = (src: string): readonly string[] => patternNames(pattern(src));
    assert.deepEqual(names('msg'), ['msg']);
    assert.deepEqual(names('_'), []);
    assert.deepEqual(names(':stop'), []);
    assert.deepEqual(names('(:pair a _ b)'), ['a', 'b']);
    assert.deepEqual(names('(:move (x y) & rest)'), ['x', 'y', 'rest']);
    assert.deepEqual(names('((a (b)) "s" 5 #true ())'), ['a', 'b']);
    assert.deepEqual(names('#(a #(b _) (c & d))'), ['a', 'b', 'c', 'd']);
});

test('a name or _ takes anything, and a literal is compared with eq?', () => {
    assert.deepEqual(lowered('x'), { tests: [], binds: ['x = v'] });
    assert.deepEqual(lowered('_'), { tests: [], binds: [] });
    assert.deepEqual(lowered(':stop').tests, ['(eq? v (quote stop))']);
    assert.deepEqual(lowered("'(1 2)").tests, ['(eq? v (quote (1 2)))']);
    assert.deepEqual(lowered('5').tests, ['(eq? v 5)']);
    assert.deepEqual(lowered('2.5').tests, ['(eq? v 2.5)']);
    assert.deepEqual(lowered('"hi"').tests, ['(eq? v "hi")']);
    assert.deepEqual(lowered('#false').tests, ['(eq? v #false)']);
    assert.deepEqual(lowered('()').tests, ['(nil? v)']);
});

test('a list pattern tests each cons, then the end', () => {
    assert.deepEqual(lowered('(:add n)'), {
        tests: ['(cons? v)', '(eq? (car v) (quote add))', '(cons? (cdr v))', '(nil? (cddr v))'],
        binds: ['n = (cadr v)'],
    });
});

test('patterns nest, and a long path is c[ad]r four steps at a time', () => {
    assert.deepEqual(lowered('(_ _ _ _ (x))'), {
        tests: ['(cons? v)', '(cons? (cdr v))', '(cons? (cddr v))', '(cons? (cdddr v))', '(cons? (cddddr v))',
                '(cons? (car (cddddr v)))', '(nil? (cdar (cddddr v)))', '(nil? (cdr (cddddr v)))'],
        binds: ['x = (caar (cddddr v))'],
    });
});

test('& takes the rest of a list, which needs no test unless it is all of it', () => {
    assert.deepEqual(lowered('(:write & xs)'), {
        tests: ['(cons? v)', '(eq? (car v) (quote write))'],
        binds: ['xs = (cdr v)'],
    });
    assert.deepEqual(lowered('(& xs)'), { tests: ['(cond ((nil? v) #true) (#true (cons? v)))'], binds: ['xs = v'] });
    assert.deepEqual(lowered('(a & ())').tests, ['(cons? v)', '(nil? (cdr v))']);
});

test('a tuple pattern tests that it is a tuple of so many elements, in one test, then each element', () => {
    assert.deepEqual(lowered('#(a :b)'), {
        tests: ['(tuple-of? v 2)', '(eq? (tuple/get v 1) (quote b))'],
        binds: ['a = (tuple/get v 0)'],
    });
    assert.deepEqual(lowered('#()').tests, ['(tuple-of? v 0)']);
    assert.deepEqual(lowered('(:at #(x (y)))'), {
        tests: ['(cons? v)', '(eq? (car v) (quote at))', '(cons? (cdr v))',
                '(tuple-of? (cadr v) 2)',
                '(cons? (tuple/get (cadr v) 1))', '(nil? (cdr (tuple/get (cadr v) 1)))',
                '(nil? (cddr v))'],
        binds: ['x = (tuple/get (cadr v) 0)', 'y = (car (tuple/get (cadr v) 1))'],
    });
});

test('what is not a pattern', () => {
    const fails = (src: string, message: string): void => {
        assert.throws(() => lowerPattern(pattern(src), sym('v')), (e: unknown) => e instanceof CompileError && e.message === message);
    };
    fails('(:a x x)', 't.slight:1:7: x is in the pattern twice');
    fails('(a &)', 't.slight:1:1: & can only come before the last pattern of a list, not (a &)');
    fails('(& a b)', 't.slight:1:1: & can only come before the last pattern of a list, not (& a b)');
    fails('(a & b & c)', 't.slight:1:1: & can only come before the last pattern of a list, not (a & b & c)');
    fails('&', 't.slight:1:1: & can only come before the last pattern of a list, not &');
    fails('(quote a b)', 't.slight:1:1: quote takes 1 argument, not (quote a b)');
    fails('#(a & b)', "t.slight:1:1: & can't be in a tuple pattern, not #(a & b)");
    fails('#(x x)', 't.slight:1:5: x is in the pattern twice');
});
