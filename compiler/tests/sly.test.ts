// Tests for the .sly reader (D246): each piece of the C-style syntax reads
// into what reader.ts makes of the s-expressions it stands for, and its
// mistakes are compile errors at the right place.

import test from 'node:test';
import assert from 'node:assert/strict';

import { CompileError } from '../src/errors.ts';
import { read } from '../src/reader.ts';
import { show, toArray } from '../src/sexp.ts';
import { readSly, slightName } from '../src/sly.ts';

const showAll = (forms: ReturnType<typeof read>): string => toArray(forms)!.map(show).join('\n');

// The .sly text reads as the slight text does.
const same = (sly: string, slight: string): void => {
    assert.equal(showAll(readSly(sly, 'test.sly')), showAll(read(slight, 'test.slight')));
};

const fails = (sly: string, message: RegExp, line?: number, col?: number): void => {
    assert.throws(() => readSly(sly, 'test.sly'), (e: unknown) => {
        if (!(e instanceof CompileError) || !message.test(e.message)) return false;
        return line === undefined || (e.pos?.line === line && e.pos?.col === col);
    });
};

// --- names ------------------------------------------------------------------

test('names map to slight names', () => {
    assert.equal(slightName('table::get'), 'table/get');
    assert.equal(slightName('str_to_int'), 'str->int');
    assert.equal(slightName('ask_within'), 'ask-within');
    assert.equal(slightName('tty::screen::rows'), 'tty/screen/rows');
    assert.equal(slightName('nil?'), 'nil?');
    assert.equal(slightName('_'), '_');
    same('table::del_loop; str_to_int; Consumer; `str/<`; `go-to-x`;', 'table/del-loop str->int Consumer str/< go-to-x');
});

test('keywords keep their spelling', () => {
    same(':on-next; :fs/read; :RECV;', ':on-next :fs/read :RECV');
    fails(':12;', /invalid keyword/);
    fails(': x;', /invalid keyword/);
});

test('special names', () => {
    same('self; parent; @ARGV; %ENV; true; false;', '$$ ^$$ @ARGV %ENV #true #false');
});

// --- literals ---------------------------------------------------------------

test('numbers and strings', () => {
    same('42; -7; 3.5; -0.25; 1.5e3; "a\\n\\u{48}";', '42 -7 3.5 -0.25 1.5e3 "a\\n\\u{48}"');
    same('-4611686018427387904;', '-4611686018427387904');
    fails('4611686018427387904;', /integer out of range/);
    fails('1e3;', /invalid number/);
});

test('comments and doc blocks', () => {
    same('// a comment\n1; // another\n=doc\nSome *text*.\n=cut\n2;', '1 2');
});

// --- data -------------------------------------------------------------------

test('lists', () => {
    same('[a, b]; [];', '(list a b) ()');
    same('[a, b, ...xs]; [...xs];', '(cons a (cons b xs)) xs');
    fails('[...xs, a];', /can only be last/);
});

test('a list of literals is a constant', () => {
    same('[1, "two", :three, [4.5, true]];', "'(1 \"two\" three (4.5 #true))");
    same('[:inc];', "'(inc)");
    same('[x, 1];', '(list x 1)');
});

test('tagged lists', () => {
    same(':on-next(v); :tag(); :tag(1, 2); :tag(a, ...xs);',
         "(list :on-next v) '(tag) '(tag 1 2) (cons :tag (cons a xs))");
});

test('tuples', () => {
    same('#(a, 1);', '(tuple a 1)');
});

// --- expressions ------------------------------------------------------------

test('calls', () => {
    same('f(a, b); f(); adder(1)(2); car(xs);', '(f a b) (f) ((adder 1) 2) (car xs)');
});

test('operators and their precedence', () => {
    same('a + b * c;', '(+ a (* b c))');
    same('a - b - c;', '(- (- a b) c)');
    same('(a + b) * c;', '(* (+ a b) c)');
    same('a < b && c == d || !e;', '(or (and (< a b) (eq? c d)) (not e))');
    same('a != b; a ++ b; a % b; a / b;', '(ne? a b) (~ a b) (% a b) (/ a b)');
    same('`==`(a, 1);', '(== a 1)');
    same('-x; - 5; x - -1;', '(- 0 x) -5 (- x -1)');
    fails('a < b < c;', /can't follow/);
    fails('a == b == c;', /can't follow/);
});

test('the pipe puts its value last', () => {
    same('xs |> filter(even?) |> map(square);', '(map square (filter even? xs))');
    same('x |> f;', '(f x)');
    fails('x |> 1;', /the right of '\|>'/);
});

test('lambdas', () => {
    same('fn(x) => x + 1; fn(a, b) { let c = a; c }; fn() {};',
         '(lambda (x) (+ x 1)) (lambda (a b) (let c a) c) (lambda () ())');
});

test('blocks', () => {
    same('{ a; b }; { a }; {}; { let x = 1; x };', '(do a b) a () (do (let x 1) x)');
});

test('if', () => {
    same('if c { a } else { b };', '(if c a b)');
    same('if c { a };', '(if c a)');
    same('if c { a } else if d { b } else { e };', '(if c a (if d b e))');
    same('if c { x; y } else { z };', '(if c (do x y) z)');
});

test('cond and case', () => {
    same('cond { a => 1, b => { x; y } };', '(cond (a 1) (b x y))');
    same('case n { 1 => :one, _ => :many };', '(case n (1 :one) (#true :many))');
});

test('match and patterns', () => {
    same('match x { [] => 0, [h, ...t] => h, _ => 1 };', '(match x (() 0) ((h & t) h) (_ 1))');
    same('match m { :put(k, v) => v, :keys() => 0, :stop => 1, #(a, b) => a };',
         "(match m ((:put k v) v) ((:keys) 0) (:stop 1) (#(a b) a))");
    same('match n { -1 => a, "s" => b, true => c, [[k, v], ...rest] => d };',
         '(match n (-1 a) ("s" b) (#true c) (((k v) & rest) d))');
});

test('the comma after an arm ending in } can be left out', () => {
    same('match x { 1 => { a } 2 => b };', '(match x (1 a) (2 b))');
    fails('match x { 1 => a 2 => b };', /expected ',' or '}' after an arm/);
});

test('recv', () => {
    same('recv { :inc => counter(n + 1), msg => msg };', '(recv (:inc (counter (+ n 1))) (msg msg))');
    same('recv;', '(recv)');
    same('fn f() { let m = recv; m }', '(defun f () (let m (recv)) m)');
});

test('processes', () => {
    same('spawn counter(0); fork f(x) + 1; yield loop(n);', '(spawn (counter 0)) (fork (+ (f x) 1)) (yield (loop n))');
    same('connect(:tcp, "h:7000") { echo() };', '(connect :tcp "h:7000" (echo))');
    same('connect(c) { a; b };', '(connect c (do a b))');
});

// --- definitions and lets ---------------------------------------------------

test('functions', () => {
    same('fn table::get(key, table) { match table { [] => :not-found, } }',
         '(defun table/get (key table) (match table (() :not-found)))');
    same('fn f() {}', '(defun f () ())');
    fails('fn f(a::b) { a }', /a parameter is a name/);
    fails('fn f() { fn g() { 1 } }', /top level only/);
});

test('a let that takes a value apart is a match over the rest', () => {
    same('fn f() { let [a, b] = g(); h(a); b }', '(defun f () (match (g) ((a b) (h a) b)))');
    fails('fn f() { let [a, b] = g(); }', /needs something after it/);
    fails('let [a, b] = g();', /top-level let binds a name/);
});

test('includes', () => {
    same('@include("lib.sly"); @include(:fs);', '(@include "lib.sly") (@include :fs)');
    fails('fn f() { @include(:fs); 1 }', /only allowed at the top level/);
});

// --- mistakes, and where they are --------------------------------------------

test('a missing semicolon', () => {
    fails('f(1)\ng(2);', /expected ';' after a top-level item/, 2, 1);
    fails('fn f() { a b }', /expected ';' between statements/, 1, 12);
});

test('an unclosed block', () => {
    fails('fn f() { a;', /expected '}'/);
});

test('reserved words', () => {
    fails('let if = 1;', /expected a pattern/);
    fails('else;', /expected an expression, not 'else'/);
});

test('positions point into the .sly file', () => {
    const x = toArray(readSly('fn f(x) {\n    x + 1\n}', 'p.sly'))![0]!;
    // (defun f (x) (+ x 1)): the + is at line 2, column 7
    const body = toArray(x)![3]!;
    assert.equal(body.t === 'pair' && body.pos !== null ? `${body.pos.line}:${body.pos.col}` : '', '2:7');
});

// --- sigils and quoting (D246) ----------------------------------------------

test('a name may start with $', () => {
    same('$f; $my_var; f($to, $f);', '$f $my-var (f $to $f)');
    same('fn ask?($to, msg) { send($to, msg) }', '(defun ask? ($to msg) (send $to msg))');
    same('match m { :do($f) => $f };', '(match m ((:do $f) $f))');
    fails('$ x;', /unexpected '\$'/);
    fails('$$;', /unexpected '\$'/);
});

test("' quotes an s-expression, read as slight reads one", () => {
    same("'((lambda (x y) (+ x y)) 10 20);", "'((lambda (x y) (+ x y)) 10 20)");
    same("'x; '42; '\"s\"; '(a ; a comment\n b);", "'x '42 '\"s\" '(a b)");
    same("['a, 'b];", "'(a b)");
    same("f('(1 2), x);", "(f '(1 2) x)");
    same("match e { '(1 2) => a, _ => b };", "(match e ('(1 2) a) (_ b))");
    fails("'(a b", /expected a datum to quote/);
    fails("';", /expected something to quote/);
});
