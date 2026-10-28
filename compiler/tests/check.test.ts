// Tests for types (types.ts) and the checker (check.ts), D247: what fits
// what, and what a program is warned about, and isn't.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { checkProgram, eraseTypes } from '../src/check.ts';
import { CompileError } from '../src/errors.ts';
import { expand } from '../src/expand.ts';
import { read } from '../src/reader.ts';
import { list, show, toArray, type Sexp } from '../src/sexp.ts';
import { readSly } from '../src/sly.ts';
import {
    ANY, BOOL, INT, NEVER, NIL_T, NUM, PID, STR, SYM,
    actorOf, fits, lit, listOf, meet, members, overlaps, readType, shape, showType, tagged, taskOf, tupleOf, union,
} from '../src/types.ts';

const kept = (forms: Sexp): Sexp => list(...(toArray(forms) ?? []).map((f) => expand(f, true)));
const PRELUDE = kept(read(readFileSync(new URL('../../lib/prelude.slight', import.meta.url), 'utf8'), 'lib/prelude.slight'));

// The warnings for a program, as messages with their line and column.
function warnings(src: string, opts: { strict?: boolean; file?: string } = {}): string[] {
    const file = opts.file ?? 'test.slight';
    const forms = file.endsWith('.sly') ? readSly(src, file) : read(src, file);
    return checkProgram(PRELUDE, kept(forms), { strict: opts.strict ?? false })
        .map((w) => `${w.pos?.line}:${w.pos?.col} ${w.message}`);
}

const clean = (src: string, opts: { strict?: boolean; file?: string } = {}): void => assert.deepEqual(warnings(src, opts), []);

const ty = (src: string): ReturnType<typeof readType> => readType(read(src, 't')
    .t === 'pair' ? (read(src, 't') as { car: Sexp }).car : read(src, 't'), [], null);

// --- types ------------------------------------------------------------------------

test('unions flatten, drop what another member holds, and keep one of each', () => {
    assert.equal(showType(union(INT, union(STR, INT)), false), '(or int str)');
    assert.equal(showType(union(lit('ok'), SYM), false), 'sym');
    assert.equal(showType(union(NIL_T, listOf(INT)), false), '(list int)');
    assert.equal(showType(union(INT, ANY), false), 'any');
    assert.equal(showType(union(INT, NEVER), false), 'int');
    assert.equal(showType(union(listOf(INT), listOf(STR)), false), '(list (or int str))');
    assert.equal(showType(BOOL, false), 'bool');
    assert.equal(showType(NUM, true), 'num');
});

test('meet is what two types have in common', () => {
    assert.equal(meet(NUM, INT, null).k, 'int');
    assert.equal(meet(INT, STR, null).k, 'never');
    assert.equal(meet(listOf(INT), listOf(STR), null).k, 'nil');      // () is both
    assert.equal(meet(tagged('ok', INT), tagged('error', ANY), null).k, 'never');
    assert.equal(meet(shape([INT, INT]), shape([INT]), null).k, 'never');
    assert.equal(meet(tupleOf([INT]), tupleOf([INT, INT]), null).k, 'never');
    assert.equal(meet(taskOf(INT), actorOf(ANY), null).k, 'never');
    assert.equal(meet(PID, actorOf(ANY), null).k, 'actor');
    assert.ok(overlaps(ANY, INT, null) && overlaps(INT, ANY, null));
});

test('fits: every value of one is the other', () => {
    assert.ok(fits(INT, NUM, null));
    assert.ok(!fits(NUM, INT, null));
    assert.ok(fits(shape([lit('ok'), INT]), listOf(union(SYM, INT)), null));
    assert.ok(fits(tagged('ok', INT), union(tagged('ok', ANY), tagged('error', ANY)), null));
    assert.ok(fits(shape([BOOL]), union(shape([lit('#true')]), shape([lit('#false')])), null));
    assert.ok(fits(actorOf(ANY), actorOf(INT), null));     // takes more: stands in for one that takes less
    assert.ok(!fits(actorOf(INT), actorOf(STR), null));
    assert.ok(fits(INT, ANY, null) && fits(ANY, INT, null));
});

test('members tell a union apart, a list as () or a cons', () => {
    assert.deepEqual(members(listOf(INT), null).map((m) => showType(m, true)), ['[]', '[int, ...list<int>]']);
    assert.deepEqual(members(shape([BOOL]), null).map((m) => showType(m, true)), ['[true]', '[false]']);
});

test('types read from their s-expressions, and show in either syntax', () => {
    assert.equal(showType(ty('(or int (:ok str) (shape int & (list str)))'), false), '(or int (:ok str) (shape int & (list str)))');
    assert.equal(showType(ty('(or int (:ok str) (shape int & (list str)))'), true), 'int | :ok(str) | [int, ...list<str>]');
    assert.equal(showType(ty('(fun (int (tuple str int)) (task #true))'), true), 'fun(int, #(str, int)) -> task<true>');
    assert.equal(showType(ty('(:write & (list str))'), true), ':write(...list<str>)');
    assert.equal(showType(ty('()'), false), '()');
    assert.throws(() => ty('Nope'), /Nope isn't a type/);
    assert.throws(() => ty('(list int str)'), /takes one type/);
    assert.throws(() => ty('5'), /a type is a name or a form/);
});

// --- what's certain ----------------------------------------------------------------

test('a builtin given what it never takes', () => {
    assert.deepEqual(warnings('(str/len 5)'), ["1:10 str/len's argument is int, not str: it faults :not-a-string"]);
    assert.deepEqual(warnings('(+ 1 "two")'), ["1:6 +'s second argument is str, not num: it faults :not-a-number"]);
    assert.deepEqual(warnings('(cons 1 2)'), ["1:9 cons's second argument is int, not (list any): it faults :not-a-list"]);
    assert.deepEqual(warnings('(join/race (list 1))'), ["1:12 join/race's argument is (shape int), not (list pid): it faults :not-a-pid"]);
    assert.deepEqual(warnings('(car ())'), ['1:1 car takes a non-empty list, and this is (): it faults :not-a-cons']);
    assert.deepEqual(warnings('(cadr (list 1))'), ['1:7 cadr takes a list of at least 2 conses down, and this is (shape int): it faults :not-a-cons']);
});

test('a test that is never a boolean', () => {
    assert.deepEqual(warnings('(if (str/index-of "ab" "b") 1 2)'), ['1:5 this test is int, not bool: it faults :not-a-bool']);
    assert.deepEqual(warnings('(and 5 #true)'), ['1:6 this test is int, not bool: it faults :not-a-bool']);
    clean('(defun f (x) (if x 1 2)) (f 5)');     // what f's x is isn't known in f
});

test('a call of what is no function, or with the wrong number of arguments', () => {
    assert.deepEqual(warnings('(let f 5) (f 1)'), ["1:12 what's called here is int, not fun: it faults :not-a-function"]);
    assert.deepEqual(warnings('(let g (lambda (a b) a)) (g 1)'), ["1:27 what's called here takes 2 arguments, and it's given 1: it faults :arity"]);
});

test('nothing is said of what is not known', () => {
    clean('(defun f (x) (str/len x)) (f 5)');
    clean('(defun g (x) (+ x 1)) (str/len (g "a"))');
    clean('(let m (json/parse "[1]")) (match m ((:ok v) v) ((:error e) e))');
});

test('code after a fault is not looked at', () => {
    assert.deepEqual(warnings('(car ()) (str/len 5)'), ['1:1 car takes a non-empty list, and this is (): it faults :not-a-cons']);
});

// --- narrowing ---------------------------------------------------------------------

test('a type predicate narrows what it tests, and its opposite after', () => {
    const src = (x: string) => `(sig f ((or int str)) any)
(defun f (x)
    (cond
        ((int? x) ${x})
        (#true    (str/len x))))`;
    clean(src('(+ x 1)'));
    assert.deepEqual(warnings(src('(str/len x)')), ["4:28 str/len's argument is int, not str: it faults :not-a-string"]);
});

test('eq? to a literal, not, and and or narrow', () => {
    clean(`(sig f ((or :none int)) int)
(defun f (x) (if (eq? x :none) 0 (+ x 1)))`);
    clean(`(sig g ((or int #false)) int)
(defun g (x) (if (not x) 0 (+ x 1)))`);
    clean(`(sig h ((or int str) (or int str)) int)
(defun h (a b) (if (and (int? a) (int? b)) (+ a b) 0))`);
    clean(`(sig k ((or int str)) int)
(defun k (a) (if (or (str? a) (< a 0)) 0 (+ a 1)))`);
});

test("a case's topic narrows the local it was", () => {
    clean(`(sig f ((or :a :b int)) int)
(defun f (x) (case x (:a 1) (:b 2) (#true (+ x 1))))`);
});

test('the rest of a fold is worked out before its lambda is checked', () => {
    clean("(fold/l #false (lambda (at x) (if (or (not at) (< x at)) x at)) '(3 1 2))");
});

// --- match and recv -----------------------------------------------------------------

test('a match whose arms can never match, or whose arm never can', () => {
    assert.deepEqual(warnings('(match 5 ((a b) a))'), ['1:1 no arm of this match can match int: it faults :no-match']);
    assert.deepEqual(warnings("(match '(1 2) ((x y) x) (_ 0))"), ["1:26 this arm can't match: the ones before it take everything"]);
    assert.deepEqual(warnings('(match 1 (1.0 :f) (1 :i))'), ["1:11 this arm can't match int"]);
    assert.deepEqual(warnings(`(sig f (bool) int)
(defun f (b) (match b (#true 1) (#false 0) (_ 2)))`), ["2:45 this arm can't match: the ones before it take everything"]);
});

test('an arm binds the parts of what it matched', () => {
    assert.deepEqual(warnings(`(sig f ((or (:ok int) (:error str))) int)
(defun f (r) (match r ((:ok n) (+ n 1)) ((:error e) (+ e 1))))`), ["2:56 +'s first argument is str, not num: it faults :not-a-number"]);
});

test('--strict says what a match leaves out, when it is known', () => {
    const src = `(sig f ((or :a :b :c)) int)
(defun f (x) (match x (:a 1) (:b 2)))`;
    clean(src);
    assert.deepEqual(warnings(src, { strict: true }), ['2:14 this match has no arm for :c: it would fault :no-match']);
    clean('(defun g (x) (match x (:a 1)))', { strict: true });
});

// --- processes ----------------------------------------------------------------------

const COUNTER = `(defun counter (n)
    (recv
        ((:inc)      (counter (+ n 1)))
        ((:get from) (send from n) (counter n))
        (:stop       n)))
(let c (spawn (counter 0)))
`;

test("a message no recv of the actor takes is a dead letter", () => {
    clean(`${COUNTER}(send c (list :inc))`);
    assert.deepEqual(warnings(`${COUNTER}(send c :inc)`),
        ['7:9 this actor takes (or (:inc) (:get any) :stop), and this is :inc: it would be a dead letter']);
    assert.deepEqual(warnings(`${COUNTER}(after 10 c (list :stop))`),
        ['7:13 this actor takes (or (:inc) (:get any) :stop), and this is (:stop): it would be a dead letter']);
});

test('what an actor takes is what every function it can go on to takes', () => {
    clean(`(defun a () (recv (:x (b))))
(defun b () (recv (:y (a))))
(let p (spawn (a)))
(send p :y)`);
    clean(`(defun any-msg () (recv (m (any-msg))))
(send (spawn (any-msg)) 42)`);
});

test('tasks and actors are told apart', () => {
    assert.deepEqual(warnings(`${COUNTER}(join c)`),
        ["7:7 join of an actor, which can't be joined: it faults :not-a-task (read it, from a task)"]);
    // read never comes to be called, since address faults first
    assert.deepEqual(warnings('(defun w () (recv (m (read (address)))))'),
        ["1:28 address in an actor, which is a task's: it faults :not-a-task"]);
    assert.deepEqual(warnings('(defun w (a) (recv (m (read a))))'),
        ['1:23 read in an actor, which takes what comes to it with recv: it faults :not-a-task']);
    assert.deepEqual(warnings('(monitor (fork 1))'), ["1:1 monitor in a task, which can't be sent the notice: it faults :not-an-actor"]);
    // a task that has ended takes it, as any ended process does
    clean('(send (fork 1) :hi)');
    assert.deepEqual(warnings('(send (fork 1) :hi)', { strict: true }),
        ["1:7 send to a task, which can't be sent to unless it has ended: it would fault :not-an-actor"]);
});

test("a task's value comes out of join and read with its type", () => {
    assert.deepEqual(warnings('(match (join (fork 5)) ((:ok v) (str/len v)) (_ 0))'),
        ["1:42 str/len's argument is int, not str: it faults :not-a-string"]);
});

test('the keyboard sends keys', () => {
    assert.deepEqual(warnings(`(defun k () (recv (:quit ())))
(connect :keypress (k))`), ['2:20 the keyboard sends (shape (or str sym) & (list sym)), and k takes :quit: every key would be a dead letter']);
    clean(`(defun k () (recv (("q" & _) ()) (_ (k))))
(connect :keypress (k))`);
});

// --- annotations ----------------------------------------------------------------------

test('a call against its sig, and a body against its result', () => {
    assert.deepEqual(warnings(`(sig f (int) str)
(defun f (n) n)
(f "a")`), ['2:1 f says it gives str, and it gives int', "3:4 f's argument is str, not int"]);
});

test('a generic function takes its type variables from its arguments', () => {
    assert.deepEqual(warnings('(str/len (car (map (lambda (x) (+ x 1)) (list 1 2))))'),
        ["1:10 str/len's argument is int, not str: it faults :not-a-string"]);
    assert.deepEqual(warnings('(str/len (fold/l 0 + (list 1 2)))'),
        ["1:10 str/len's argument is int, not str: it faults :not-a-string"]);
    clean('(str/len (lookup :a (list (list :a "x"))))', {});
    assert.deepEqual(warnings('(str/len (lookup :a (list (list :a "x"))))', { strict: true }),
        ["1:10 str/len's argument may be (or str :not-found), not just str: it would fault :not-a-string"]);
});

test('the says what an expression is, and is checked', () => {
    assert.deepEqual(warnings('(let s (the str 5))'), ['1:17 this is int, not str']);
    clean("(let r (the (or (:ok int) (:error str)) (list :ok 1))) (match r ((:ok n) (+ n 1)) ((:error e) 0))");
});

test('a recv type says what an actor takes', () => {
    assert.deepEqual(warnings(`(deftype Msg (or (:add int) :stop))
(sig acc (int) int (recv Msg))
(defun acc (n)
    (recv
        ((:add k) (acc (+ n k)))
        (:reset   (acc 0))
        (:stop    n)))
(let a (spawn (acc 0)))
(send a (list :add "1"))`), [
        '6:10 this arm takes nothing this actor is sent, which is Msg',
        '9:9 this actor takes Msg, and this is (:add str): it would be a dead letter',
    ]);
});

test('types that refer to themselves', () => {
    clean(`(deftype Tree (or :leaf (:node Tree int Tree)))
(sig size (Tree) int)
(defun size (t) (match t (:leaf 0) ((:node l _ r) (+ 1 (+ (size l) (size r))))))
(size (list :node :leaf 1 :leaf))`);
    assert.deepEqual(warnings(`(deftype Tree (or :leaf (:node Tree int Tree)))
(sig size (Tree) int)
(defun size (t) 0)
(size (list :node :leaf "1" :leaf))`), ["4:7 size's argument is (:node :leaf str :leaf), not Tree"]);
});

test('declarations that are wrong are compile errors', () => {
    const errs = (src: string, re: RegExp) => assert.throws(() => warnings(src), (e: unknown) => e instanceof CompileError && re.test(e.message));
    errs('(sig f (int) int)', /a sig for f, which isn't a function here/);
    errs('(sig f (int int) int) (defun f (a) a)', /f's sig has 2 parameters, and f 1/);
    errs('(sig f (int) int) (sig f (int) int) (defun f (a) a)', /f has a sig already/);
    errs('(deftype int str)', /int is a type already/);
    errs('(deftype Json int)', /Json is a type in the prelude/);
    errs('(deftype A int) (deftype A str)', /A is already a type/);
    errs('(sig f (Unknown) int) (defun f (a) a)', /Unknown isn't a type/);
});

test('types are erased before code generation', () => {
    const forms = eraseTypes(read('(deftype T int) (sig f (int) int) (defun f (x) (the int x)) (f (the int 1))', 't'));
    assert.equal(toArray(forms)!.map(show).join(' '), '(defun f (x) x) (f 1)');
});

// --- the prelude ------------------------------------------------------------------------

test("the prelude checks clean against its own sigs, strictly too", () => {
    for (const strict of [false, true]) {
        assert.deepEqual(checkProgram(PRELUDE, list(), { prelude: true, strict }).map((w) => w.message), []);
    }
});

// --- the .sly syntax ----------------------------------------------------------------------

test('warnings in a .sly file speak its syntax', () => {
    assert.deepEqual(warnings(`fun counter(n) {
    recv {
        :inc() => counter(n + 1),
        :stop => n,
    }
}
let c = spawn counter(0);
send(c, :inc);
str::len(5)`, { file: 't.sly' }), [
        '8:9 this actor takes :inc() | :stop, and this is :inc: it would be a dead letter',
        "9:10 str::len's argument is int, not str: it faults :not-a-string",
    ]);
});
