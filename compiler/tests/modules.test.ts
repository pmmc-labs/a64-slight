// Modules (D248): a file with a header is a module, which exports what
// its header declares and keeps the rest; what each unit's code means by
// a name, after linking.

import test from 'node:test';
import assert from 'node:assert/strict';

import { CompileError } from '../src/errors.ts';
import { expandProgram, type Loader } from '../src/expand.ts';
import { displayName } from '../src/modules.ts';
import { show, toArray } from '../src/sexp.ts';

function files(fs: Readonly<Record<string, string>>): Loader {
    return { key: (path) => (path in fs ? path : null), text: (key) => fs[key]! };
}

const PRELUDE = {
    '/lib/prelude.slight': '(defun inc (n) (+ n 1)) (defun inc-loop (n) n) (defun not (a) (cond (a #false) (#true #true)))',
    '/lib/prelude.sli':    '(sig inc (any) any) (sig not (bool) bool)',
};

const linked = (fs: Readonly<Record<string, string>>): { prelude: string[]; program: string[]; modules: readonly string[] } => {
    const out = expandProgram(['main.slight'], '/lib', files({ ...PRELUDE, ...fs }), true);
    return { prelude: toArray(out.prelude)!.map(show), program: toArray(out.program)!.map(show), modules: out.modules };
};

const fails = (fs: Readonly<Record<string, string>>, message: RegExp): void => {
    assert.throws(() => linked(fs), (e: unknown) => e instanceof CompileError && message.test(e.message));
};

test("a module's functions are its own, but for those its header declares", () => {
    const out = linked({
        'main.slight':   '(@include "m.slight") (m/get 1)',
        'm.slight':      '(defun m/get (k) (m/hash k)) (defun m/hash (k) k)',
        'm.sli':         '(sig m/get (any) any)',
    });
    assert.deepEqual(out.program, [
        '(defun m/get (in m.slight) (k) (m/hash (in m.slight) k))',
        '(defun m/hash (in m.slight) (k) k)',
        '(sig m/get (in m.slight) (any) any)',
        '(m/get (in m.slight) 1)',
    ]);
    assert.deepEqual(out.modules, ['lib/prelude.slight', 'lib/prelude.sli', 'm.slight', 'm.sli']);
});

test("a name another unit keeps, or that's in a module not included, is an error that says whose", () => {
    const m = { 'm.slight': '(defun m/get (k) (m/hash k)) (defun m/hash (k) k)', 'm.sli': '(sig m/get (any) any)' };
    fails({ ...m, 'main.slight': '(@include "m.slight") (m/hash 1)' }, /m\/hash is private to m\.slight: its header doesn't declare it/);
    fails({ ...m, 'n.slight': '(@include "m.slight") (defun n/f () 1)', 'n.sli': '(sig n/f () any)',
            'main.slight': '(@include "n.slight") (m/get 1)' }, /m\/get is m\.slight's: include it to use it/);
    fails({ 'main.slight': '(inc-loop 1)' }, /inc-loop is private to the prelude/);
});

test("what a header includes, whoever includes it sees too, as C's headers", () => {
    const out = linked({
        'main.slight': '(@include "n.slight") (m/get 1) (n/f)',
        'm.slight':    '(defun m/get (k) k)',
        'm.sli':       '(sig m/get (any) any)',
        'n.slight':    '(@include "m.slight") (defun n/f () (m/get 2))',
        'n.sli':       '(@include "m.slight") (sig n/f () any)',
    });
    assert.deepEqual(out.program.slice(-2), ['(m/get (in m.slight) 1)', '(n/f (in n.slight))']);
});

test("a library's code calls the prelude's functions as the prelude's, whatever the program defines", () => {
    const out = linked({
        'main.slight': '(@include "m.slight") (defun inc (n) 0) (inc 1) (m/f 1)',
        'm.slight':    '(defun m/f (n) (if (not #false) (inc n) 0))',
        'm.sli':       '(sig m/f (any) any)',
    });
    assert.ok(out.program[0]!.includes('(not (prelude) #false)') && out.program[0]!.includes('(inc (prelude) n)'), out.program[0]);
    assert.ok(out.program.includes('(inc 1)'));
    assert.deepEqual(out.prelude.slice(0, 2), ['(defun inc (n) (+ n 1))', '(defun inc-loop (in lib/prelude.slight) (n) n)']);
});

test("a module's own types are its own, and its header's anyone's", () => {
    const out = linked({
        'main.slight': '(@include "m.slight") (the Pub 1)',
        'm.slight':    '(deftype Priv int) (sig m/f (Priv) Pub) (defun m/f (x) x) (defun m/g () 1)',
        'm.sli':       '(deftype Pub int) (sig m/g () any)',
    });
    assert.ok(out.program.includes('(deftype Priv (in m.slight) int)'));
    assert.ok(out.program.includes('(sig m/f (in m.slight) (Priv (in m.slight)) Pub)'));
    assert.ok(out.program.includes('(the Pub 1)'));
});

test('locals hide functions, in every place a name is bound', () => {
    const out = linked({
        'main.slight': '(@include "m.slight") (m/f 1)',
        'm.slight':    '(defun m/f (m/g) (m/g 1)) (defun m/h () (let m/g 5) m/g) (defun m/k (x) (match x ((m/g) m/g) (_ (m/g)))) (defun m/g () 0)',
        'm.sli':       '(sig m/f (any) any)',
    });
    assert.deepEqual(out.program.slice(0, 3), [
        '(defun m/f (in m.slight) (m/g) (m/g 1))',
        '(defun m/h (in m.slight) () (let m/g 5) m/g)',
        '(defun m/k (in m.slight) (x) (match x ((m/g) m/g) (_ (m/g (in m.slight)))))',
    ]);
});

test('what a module and its header can be', () => {
    fails({ 'main.slight': '(@include "m.slight")', 'm.slight': '(defun m/f () 1) (pprint 1)', 'm.sli': '(sig m/f () any)' },
          /m\.slight has a header, so it's a module: it can only define functions and types/);
    fails({ 'main.slight': '(@include "m.slight")', 'm.slight': '(defun m/f () 1)', 'm.sli': '(sig m/g () any)' },
          /m\.slight's header declares m\/g, which it doesn't define/);
    fails({ 'main.slight': '(@include "m.slight")', 'm.slight': '(defun m/f () 1)', 'm.sli': '(pprint 1)' },
          /m\.sli is a header: it has deftype and sig forms, and @include, not \(pprint 1\)/);
    fails({ 'main.slight': '(@include "m.slight")', 'm.slight': '(defun m/f () 1)', 'm.sli': '(@include "x.slight")', 'x.slight': '(defun x () 1)' },
          /a header includes only modules, files with headers: x\.slight has none/);
    fails({ 'main.slight': '(@include "a.slight") (@include "b.slight")',
            'a.slight': '(defun f () 1)', 'a.sli': '(sig f () any)', 'b.slight': '(defun f () 2)', 'b.sli': '(sig f () any)' },
          /f is exported by both a\.slight and b\.slight, which the program includes/);
    fails({ 'main.slight': '(@include "a.slight")', 'a.slight': '(@include "b.slight") (defun f () 1)', 'a.sli': '(sig f () any)',
            'b.slight': '(@include "a.slight") (defun g () 1)', 'b.sli': '(sig g () any)' },
          /including a\.slight makes a cycle: main\.slight -> a\.slight -> b\.slight -> a\.slight/);
});

test('a file with no header is spliced in, as ever, into whoever includes it', () => {
    const out = linked({
        'main.slight': '(@include "m.slight") (@include "plain.slight") (m/f)',
        'm.slight':    '(@include "plain.slight") (defun m/f () (helper))',
        'm.sli':       '(sig m/f () any)',
        'plain.slight': '(defun helper () 1)',
    });
    assert.deepEqual(out.program, [
        '(defun helper (in m.slight) () 1)',
        '(defun m/f (in m.slight) () (helper (in m.slight)))',
        '(sig m/f (in m.slight) () any)',
        '(defun helper () 1)',
        '(m/f (in m.slight))',
    ]);
});

test('a header in the C-style syntax declares with prototypes', () => {
    const out = linked({
        'main.slight': '(@include "m.sly") (m/f 1)',
        'm.sly':       'fun m::f(x) { x }',
        'm.slyi':      'type Num = int | float;\nfun m::f(x : Num) : Num;',
    });
    assert.deepEqual(out.program, [
        '(defun m/f (in m.sly) (x) x)',
        '(deftype Num (or int float))',
        '(sig m/f (in m.sly) (Num) Num)',
        '(m/f (in m.sly) 1)',
    ]);
});

test('a name as people read it has no file in it', () => {
    assert.equal(displayName('map/bit (in lib/map.slight)'), 'map/bit');
    assert.equal(displayName('f (in m.slight) (recv at m.slight:3:4)'), 'f (recv at m.slight:3:4)');
    assert.equal(displayName('inc (prelude)'), 'inc');
    assert.equal(displayName('plain'), 'plain');
});
