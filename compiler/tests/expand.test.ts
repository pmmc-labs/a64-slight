// The expander: @include, and the forms that become cond.

import test from 'node:test';
import assert from 'node:assert/strict';

import { CompileError } from '../src/errors.ts';
import { expand, expandProgram, relative, type Loader } from '../src/expand.ts';
import { read } from '../src/reader.ts';
import { show, toArray } from '../src/sexp.ts';

// Each form of src, expanded, in reader syntax.
const ex = (src: string): string[] => toArray(read(src, 'test.slight'))!.map((x) => show(expand(x)));

const fails = (f: () => unknown, message: string): void => {
    assert.throws(f, (e: unknown) => e instanceof CompileError && e.message === message);
};

// --- if, when, case, and, or ------------------------------------------------

test('if becomes cond, with () when there is no else', () => {
    assert.deepEqual(ex('(if a b c)'), ['(cond (a b) (#true c))']);
    assert.deepEqual(ex('(if a b)'), ['(cond (a b) (#true ()))']);
    fails(() => ex('(if a)'), 'test.slight:1:1: if takes a test, a then and maybe an else, not (if a)');
    fails(() => ex('(if a b c d)'), 'test.slight:1:1: if takes a test, a then and maybe an else, not (if a b c d)');
});

test('when becomes cond, giving () when its test is #false', () => {
    assert.deepEqual(ex('(when a (f) (g))'), ['(cond (a (f) (g)) (#true ()))']);
    fails(() => ex('(when a)'), 'test.slight:1:1: when takes a test and a body, not (when a)');
});

test('case binds its topic once, compares with eq?, and gives () if nothing matches', () => {
    assert.deepEqual(ex('(case (f) (:a 1) (2 (g) 3))'), [
        '(do (let case topic test.slight:1:1 (f)) (cond ((eq? case topic test.slight:1:1 (quote a)) 1) ((eq? case topic test.slight:1:1 2) (g) 3) (#true ())))',
    ]);
    // a #true clause is the default, and then there's no need for another
    assert.deepEqual(ex('(case x (:a 1) (#true 2))'), [
        '(do (let case topic test.slight:1:1 x) (cond ((eq? case topic test.slight:1:1 (quote a)) 1) (#true 2)))',
    ]);
    fails(() => ex('(case)'), 'test.slight:1:1: case takes a topic and clauses, not (case)');
    fails(() => ex('(case x (:a))'), 'test.slight:1:1: a case clause is (value body...), not ((quote a))');
    fails(() => ex('(case x 5)'), 'test.slight:1:1: a case clause is (value body...), not 5');
});

test('a case topic has a name no program can write', () => {
    const name = 'case topic test.slight:1:1';
    assert.notEqual(show(read(name, 'x')), name);
});

test('and and or become cond, stopping at the first operand that decides', () => {
    assert.deepEqual(ex('(and)'), ['#true']);
    assert.deepEqual(ex('(or)'), ['#false']);
    assert.deepEqual(ex('(and a)'), ['(cond (a #true) (#true #false))']);
    assert.deepEqual(ex('(and a b c)'), ['(cond (a (cond (b (cond (c #true) (#true #false))) (#true #false))) (#true #false))']);
    assert.deepEqual(ex('(or a b c)'), ['(cond (a #true) (b #true) (c #true) (#true #false))']);
});

test('expansion goes all the way down, and into what an expansion makes', () => {
    assert.deepEqual(ex('(f (if (and a b) (when c d) e))'), [
        '(f (cond ((cond (a (cond (b #true) (#true #false))) (#true #false)) (cond (c d) (#true ()))) (#true e)))',
    ]);
    assert.deepEqual(ex('(defun f (x) (if x 1 2))'), ['(defun f (x) (cond (x 1) (#true 2)))']);
    assert.deepEqual(ex('(lambda (x) (or x y))'), ['(lambda (x) (cond (x #true) (y #true) (#true #false)))']);
    assert.deepEqual(ex('(cond ((and a b) (if c 1 2)))'), ['(cond ((cond (a (cond (b #true) (#true #false))) (#true #false)) (cond (c 1) (#true 2))))']);
    assert.deepEqual(ex('(defun s () (recv ((:m x) (if x (s) 0))))'), ['(defun s () (recv (((quote m) x) (cond (x (s)) (#true 0)))))']);
});

test('quoted data, names, parameters and patterns are left alone', () => {
    assert.deepEqual(ex("'(if a b)"), ['(quote (if a b))']);
    assert.deepEqual(ex('(defun when (if) and)'), ['(defun when (if) and)']);
    assert.deepEqual(ex('(lambda (case) case)'), ['(lambda (case) case)']);
    assert.deepEqual(ex('(let or (and))'), ['(let or #true)']);
    assert.deepEqual(ex('(defun s () (recv ((:if when) when)))'), ['(defun s () (recv (((quote if) when) when)))']);
});

// --- @include ---------------------------------------------------------------

// A loader over an in-memory file system, which counts what it reads.
function files(fs: Readonly<Record<string, string>>): Loader & { readonly reads: string[] } {
    const reads: string[] = [];
    return {
        reads,
        key:  (path) => (path in fs ? path : null),
        text: (key) => {
            reads.push(key);
            return fs[key]!;
        },
    };
}

const PRELUDE = { '/lib/prelude.slight': '(defun inc (n) (+ n 1))' };
const program = (fs: Readonly<Record<string, string>>, main = 'main.slight'): string[] =>
    toArray(expandProgram([main], '/lib', files({ ...PRELUDE, ...fs })).program)!.map(show);

test('an include splices in the forms of a file, where it stands', () => {
    assert.deepEqual(program({
        'main.slight':     '(pprint 1) (@include "lib/a.slight") (pprint 3)',
        'lib/a.slight':    '(pprint 2) (defun a () (if x 1 2))',
    }), ['(pprint 1)', '(pprint 2)', '(defun a () (cond (x 1) (#true 2)))', '(pprint 3)']);
});

test('a path is relative to the file it is in, and positions name the file', () => {
    const fs = files({
        ...PRELUDE,
        't/main.slight':          '(@include "data/inc/a.slight")',
        't/data/inc/a.slight':    '(@include "../b.slight") (a)',
        't/data/b.slight':        '(b)',
    });
    const forms = toArray(expandProgram(['t/main.slight'], '/lib', fs).program)!;
    assert.deepEqual(forms.map(show), ['(b)', '(a)']);
    assert.equal(forms[0]!.t === 'pair' && forms[0]!.pos?.file, 't/data/b.slight');
});

test('a built-in is lib/name.slight, named so in positions', () => {
    const fs = files({ ...PRELUDE, '/lib/fs.slight': '(defun slurp (p) p)', 'main.slight': '(@include :fs)' });
    const forms = toArray(expandProgram(['main.slight'], '/lib', fs).program)!;
    assert.deepEqual(forms.map(show), ['(defun slurp (p) p)']);
    assert.equal(forms[0]!.t === 'pair' && forms[0]!.pos?.file, 'lib/fs.slight');
});

test('a file is included once, and read once', () => {
    const fs = files({
        ...PRELUDE,
        'main.slight': '(@include "a.slight") (@include "b.slight") (@include "a.slight") (@include :prelude)',
        'a.slight':    '(@include "c.slight") (a)',
        'b.slight':    '(@include "c.slight") (b)',
        'c.slight':    '(c)',
    });
    const out = expandProgram(['main.slight'], '/lib', fs);
    assert.deepEqual(toArray(out.program)!.map(show), ['(c)', '(a)', '(b)']);
    assert.deepEqual(toArray(out.prelude)!.map(show), ['(defun inc (n) (+ n 1))']);
    assert.deepEqual([...fs.reads].sort(), ['/lib/prelude.slight', 'a.slight', 'b.slight', 'c.slight', 'main.slight']);
});

test('a file that includes itself, directly or not, is an error', () => {
    fails(() => program({ 'main.slight': '(@include "a.slight")', 'a.slight': '\n(@include "b.slight")', 'b.slight': '(@include "a.slight")' }),
          'b.slight:1:1: including a.slight makes a cycle: main.slight -> a.slight -> b.slight -> a.slight');
    fails(() => program({ 'main.slight': '(@include "main.slight")' }),
          'main.slight:1:1: including main.slight makes a cycle: main.slight -> main.slight');
});

test('what can go wrong with an include', () => {
    fails(() => program({ 'main.slight': '(pprint 1)\n  (@include "nope.slight")' }), "main.slight:2:3: can't read nope.slight");
    fails(() => program({ 'main.slight': '(@include :nope)' }), "main.slight:1:1: can't read lib/nope.slight");
    fails(() => program({ 'main.slight': '(@include)' }), 'main.slight:1:1: @include takes 1 argument, not 0');
    fails(() => program({ 'main.slight': '(@include 5)' }), 'main.slight:1:1: @include takes a path ("file.slight") or a built-in (:fs), not 5');
    fails(() => program({ 'main.slight': '(defun f () (@include :fs))' }), 'main.slight:1:13: @include is only allowed at the top level');
});

test('relative paths take their . and .. steps', () => {
    assert.equal(relative('t/main.slight', 'data/a.slight'), 't/data/a.slight');
    assert.equal(relative('t/data/inc/a.slight', '../b.slight'), 't/data/b.slight');
    assert.equal(relative('./main.slight', './lib/x.slight'), 'lib/x.slight');
    assert.equal(relative('main.slight', '../x.slight'), '../x.slight');
    assert.equal(relative('t/main.slight', '/abs/x.slight'), '/abs/x.slight');
    assert.equal(relative('/lib/a.slight', 'b.slight'), '/lib/b.slight');
});
