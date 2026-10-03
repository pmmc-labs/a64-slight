// Tests for code generation. What the generated code *does* is tested by
// the golden tests in t/; these check the pieces that are easy to get
// subtly wrong.

import test from 'node:test';
import assert from 'node:assert/strict';

import { asmString, compileProgram, functionLabel, loadWord } from '../src/codegen.ts';
import { CompileError } from '../src/errors.ts';
import { read } from '../src/reader.ts';

// Runs a movz/movn/movk sequence and returns the register's value.
function simulate(lines: readonly string[]): bigint {
    const MASK = (1n << 64n) - 1n;
    return lines.reduce((reg, line) => {
        const m = /^\s*(movz|movn|movk) x0, #(0x[0-9a-f]+|0), lsl #(\d+)\b/.exec(line) ?? /^\s*(movz|movn) x0, #(0)$/.exec(line);
        assert.ok(m, `unexpected instruction: ${line}`);
        const imm   = BigInt(m[2]!);
        const shift = BigInt(m[3] ?? 0);
        switch (m[1]) {
            case 'movz': return imm << shift;
            case 'movn': return ~(imm << shift) & MASK;
            default:     return (reg & ~(0xffffn << shift) & MASK) | (imm << shift);
        }
    }, 0n);
}

// A deterministic spread of 64-bit words.
function* words(n: number): Generator<bigint> {
    let x = 0x9e3779b97f4a7c15n;
    for (let i = 0; i < n; i++) {
        x = (x * 6364136223846793005n + 1442695040888963407n) & ((1n << 64n) - 1n);
        yield x;
    }
}

test('loadWord builds every word it is given', () => {
    const edges = [0n, 1n, 2n, -1n, -2n, 0xffffn, 0x10000n, 0xffff0000ffff0000n, 0x123456789abcdef0n, -(2n ** 63n), 2n ** 63n - 1n];
    for (const w of [...edges, ...words(2000)]) {
        assert.equal(simulate(loadWord('x0', w)), BigInt.asUintN(64, w), `0x${BigInt.asUintN(64, w).toString(16)}`);
    }
});

test('loadWord uses one instruction for small and small negative words', () => {
    for (const w of [0n, 84n, 0xffffn, -1n, -14n, -0x10000n]) {
        assert.equal(loadWord('x0', w).length, 1, w.toString());
    }
});

test('loadWord never needs more than four instructions', () => {
    for (const w of words(2000)) assert.ok(loadWord('x0', w).length <= 4);
});

const compile = (src: string): string => compileProgram(read(src, 'test.slight'));

test('a program is the function slight_main', () => {
    const asm = compile('42');
    assert.match(asm, /#include "asm.h"/);
    assert.match(asm, /^FUNC slight_main$/m);
    assert.match(asm, /movz x0, #0x54, lsl #0 {4}\/\/ 42/);
    assert.match(asm, /^ {4}ret$/m);
});

test('every form is compiled, in order', () => {
    const asm = compile('1 2 3');
    const loads = asm.split('\n').filter((l) => l.includes('movz'));
    assert.deepEqual(loads.map((l) => l.split('// ')[1]), ['1', '2', '3']);
});

test("a program with no top-level expressions has the value ()", () => {
    assert.match(compile('; nothing'), /FUNC slight_main[^]*mov {2}x0, #RT_NIL/);
    assert.match(compile('(defun f () 1)'), /FUNC slight_main[^]*mov {2}x0, #RT_NIL/);
});

test('what isn\'t built yet is an error, with a position', () => {
    assert.throws(() => compile('42\n  1.5'), (e: unknown) =>
        e instanceof CompileError && e.message === 'test.slight:2:3: not supported yet: 1.5');
    assert.throws(() => compile('(lambda (x) x)'), (e: unknown) =>
        e instanceof CompileError && e.message === 'test.slight:1:1: not supported yet: (lambda (x) x)');
});

const fails = (src: string, message: string): void => {
    assert.throws(() => compile(src), (e: unknown) => e instanceof CompileError && e.message === message);
};

test('names must be bound', () => {
    fails('(+ x 1)', "test.slight:1:4: unknown name 'x'");
    fails('(do (let x 1) x) x', "test.slight:1:18: unknown name 'x'");
    fails('(frob 1)', "test.slight:1:2: unknown function 'frob'");
});

test('primitives check their arity', () => {
    fails('(+ 1)', 'test.slight:1:1: + takes 2 arguments, not 1');
    fails('(< 1 2 3)', 'test.slight:1:1: < takes 2 arguments, not 3');
    fails('(pprint)', 'test.slight:1:1: pprint takes 1 argument, not 0');
});

test('let is (let name expr), and only in a body', () => {
    fails('(let x)', 'test.slight:1:1: let is (let name expr), not (let x)');
    fails('(let x 1 2)', 'test.slight:1:1: let is (let name expr), not (let x 1 2)');
    fails('(let 5 1)', 'test.slight:1:6: let needs a name, not 5');
    fails('(let #true 1)', "test.slight:1:6: can't bind #true");
    fails('(let cond 1)', "test.slight:1:6: can't bind cond");
    fails('(+ (let x 1) 2)', 'test.slight:1:4: let can only be a form of a body (a function, do or cond clause)');
});

test('cond needs clauses with bodies', () => {
    fails('(cond)', 'test.slight:1:1: cond needs at least one clause');
    fails('(cond (#true))', 'test.slight:1:7: a cond clause is (test body...), not (#true)');
    fails('(cond 5)', 'test.slight:1:7: a cond clause is (test body...), not 5');
});

test('do needs a form', () => {
    fails('(do)', 'test.slight:1:1: do needs at least one form');
});

test('locals can shadow builtins, but calling a local is not built yet', () => {
    fails('(let pprint 1) (pprint 2)', 'test.slight:1:16: not supported yet: (pprint 2)');
});

test('a slot is reused once the value in it is dead', () => {
    const frame = (src: string): string | undefined => /sub {2}sp, sp, #(\d+)/.exec(compile(src))?.[1];
    assert.equal(frame('42'), undefined);
    assert.equal(frame('(+ 1 2) (+ 3 4)'), '16');            // one slot, rounded up to 16 bytes
    assert.equal(frame('(+ 1 (+ 2 (+ 3 4)))'), '32');        // three waiting left operands
    assert.equal(frame('(let a 1) (let b 2) (+ a b)'), '32'); // a, b, and +'s left operand
});

test('strings in the assembly are escaped byte by byte', () => {
    assert.equal(asmString('t/a.slight:1:2'), '"t/a.slight:1:2"');
    assert.equal(asmString('say "hi"\\'), '"say \\042hi\\042\\134"');
    assert.equal(asmString('é\n'), '"\\303\\251\\012"');
});

// --- functions --------------------------------------------------------------

test('defun is (defun name (params...) body...)', () => {
    fails('(defun f (x))', 'test.slight:1:1: defun is (defun name (params...) body...)');
    fails('(defun (x) 1)', 'test.slight:1:1: defun is (defun name (params...) body...)');
    fails('(defun 5 (x) 1)', 'test.slight:1:8: defun needs a name, not 5');
    fails('(defun f x 1)', "test.slight:1:10: f's parameters must be a list, not x");
    fails('(defun f (x 5) 1)', 'test.slight:1:13: a parameter must be a name, not 5');
    fails('(defun f (x y x) 1)', 'test.slight:1:15: x is a parameter twice');
    fails('(defun f (a b c d e f g h i) 1)', 'test.slight:1:8: f has 9 parameters; the most is 8');
});

test("some names can't be defined", () => {
    fails('(defun f () 1) (defun f () 2)', 'test.slight:1:23: f is already defined');
    fails('(defun pprint (x) x)', "test.slight:1:8: can't define pprint: it's a builtin");
    fails('(defun + (a b) a)', "test.slight:1:8: can't define +: it's a builtin");
    fails('(defun cond () 1)', "test.slight:1:8: can't bind cond");
    fails('(defun f (#true) 1)', "test.slight:1:11: can't bind #true");
});

test('defun is only allowed at the top level', () => {
    fails('(do (defun f () 1))', 'test.slight:1:5: defun is only allowed at the top level');
});

test('calls are checked against the function', () => {
    fails('(defun f (x) x) (f 1 2)', 'test.slight:1:17: f takes 1 argument, not 2');
    fails('(defun f (x y) x) (f)', 'test.slight:1:19: f takes 2 arguments, not 0');
    fails('(defun f () 1) f', "test.slight:1:16: functions as values aren't supported yet: f");
});

test("a defun can't see the top level's lets", () => {
    fails('(let x 1) (defun f () x) (f)', "test.slight:1:23: unknown name 'x'");
});

test('a call in tail position is a jump; any other call returns', () => {
    const asm = compile('(defun f (n) (cond ((== n 0) 0) (#true (+ 1 (g n))))) (defun g (n) (f (- n 1))) (f 3)');
    assert.match(asm, /^ {4}bl {3}fn_g$/m);
    assert.match(asm, /^ {4}b {4}fn_f {4}\/\/ tail call$/m);
    assert.match(asm, /mov {2}sp, x29\n {4}ldp {2}x29, x30, \[sp\], #16\n {4}b {4}fn_f/);
});

test('every function checks the stack and its reductions on entry', () => {
    const asm = compile('(defun f (a b) (+ a b)) (f 1 2)');
    const entry = asm.slice(asm.indexOf('fn_f:'));
    for (const check of ['ldr  x16, [x28, #RT_PROC_STACK_LIMIT]', 'b.lo Lfault_', 'str  x0, [sp, #0]    // a',
                         'str  x1, [sp, #8]    // b', 'ldr  x16, [x28, #RT_PROC_REDUCTIONS]', 'b.le Lpreempt_']) {
        assert.ok(entry.includes(check), check);
    }
    // the parameters are saved before rt_preempt could clobber x0..x7
    assert.ok(entry.indexOf('// b') < entry.indexOf('RT_PROC_REDUCTIONS'));
});

test('function labels spell out everything but letters and digits', () => {
    assert.equal(functionLabel('fib'), 'fn_fib');
    assert.equal(functionLabel('sum-to'), 'fn_sum_2dto');
    assert.equal(functionLabel('even?'), 'fn_even_3f');
    assert.equal(functionLabel('a_b'), 'fn_a__b');
    assert.equal(functionLabel('é'), 'fn__c3_a9');
    // different names, different labels
    const names = ['a-b', 'a_2db', 'a__b', 'a_b', 'ab', 'a/b', 'a_2fb'];
    assert.equal(new Set(names.map(functionLabel)).size, names.length);
});

// --- symbols ----------------------------------------------------------------

const symbolTable = (asm: string): string[] => {
    const lines = asm.split('\n');
    const start = lines.indexOf('slight_symbol_names:');
    const count = Number(/slight_symbol_count:\n {4}\.quad (\d+)/.exec(asm)![1]);
    const names = lines.slice(start + 1).map((l) => /^ {4}\.asciz "(.*)"$/.exec(l)?.[1]).filter((n) => n !== undefined);
    assert.equal(names.length, count);
    return names;
};

test('symbols get ids in order of first mention, after #false and #true', () => {
    assert.deepEqual(symbolTable(compile(':b \'a :b (quote c) :a')), ['#false', '#true', 'b', 'a', 'c']);
    assert.deepEqual(symbolTable(compile('42')), ['#false', '#true']);
    assert.deepEqual(symbolTable(compile("'#true :x")), ['#false', '#true', 'x']);
});

test('a symbol is its id, shifted and tagged', () => {
    assert.match(compile(':a'), /movz x0, #0x15, lsl #0 {4}\/\/ 'a/);     // id 2: 2 << 3 | 5
});

test('quote takes one thing', () => {
    fails('(quote)', 'test.slight:1:1: quote takes 1 argument, not 0');
    fails('(quote a b)', 'test.slight:1:1: quote takes 1 argument, not 2');
    fails("'1.5", 'test.slight:1:1: not supported yet: (quote 1.5)');
});

test('equality and the predicates are builtins', () => {
    fails('(eq? 1)', 'test.slight:1:1: eq? takes 2 arguments, not 1');
    fails('(nil? 1 2)', 'test.slight:1:1: nil? takes 1 argument, not 2');
    fails('(defun sym? (x) x)', "test.slight:1:8: can't define sym?: it's a builtin");
    fails('(defun ne? (a b) a)', "test.slight:1:8: can't define ne?: it's a builtin");
});

test('eq? takes any values, so it has no integer check', () => {
    assert.doesNotMatch(compile('(eq? :a 1)'), /RT_FAULT_NOT_INT/);
    assert.match(compile('(== 1 1)'), /RT_FAULT_NOT_INT/);
});

// --- lists ------------------------------------------------------------------

test('car, cdr, and c[ad]r with up to four letters are builtins', () => {
    for (const name of ['car', 'cdr', 'cadr', 'cddr', 'caar', 'caddr', 'cadddr', 'cddddr', 'list', 'cons', 'cons?']) {
        fails(`(defun ${name} (x) x)`, `test.slight:1:8: can't define ${name}: it's a builtin`);
    }
    // five letters is just a name
    assert.match(compile('(defun caddddr (x) x) (caddddr 1)'), /b {4}fn_caddddr {4}\/\/ tail call/);
    fails('(cr 1)', "test.slight:1:2: unknown function 'cr'");
    fails('(cxr 1)', "test.slight:1:2: unknown function 'cxr'");
});

test('a c[ad]r applies its letters right to left', () => {
    const steps = (src: string): string[] => compile(src).split('\n').filter((l) => /\/\/ c[ad]r$/.test(l)).map((l) => l.slice(-3));
    assert.deepEqual(steps('(cadr (list 1 2))'), ['cdr', 'car']);
    assert.deepEqual(steps('(cdar (list (list 1 2)))'), ['car', 'cdr']);
    assert.deepEqual(steps('(cadddr (list 1 2 3 4))'), ['cdr', 'cdr', 'cdr', 'car']);
});

test('list primitives check their arity', () => {
    fails('(cons 1)', 'test.slight:1:1: cons takes 2 arguments, not 1');
    fails('(car)', 'test.slight:1:1: car takes 1 argument, not 0');
    fails('(cadr 1 2)', 'test.slight:1:1: cadr takes 1 argument, not 2');
});

test('a quoted list is cells in the constant data', () => {
    const asm = compile("'(1 (a) ())");
    const consts = asm.slice(asm.indexOf('CONSTDATA'));
    // the inner list first, then the outer one: 1, (a), (), each cell's cdr the next cell
    assert.match(consts, /Lquoted_0:\n {4}\.quad 0x15, RT_NIL\n/);
    assert.match(consts, /Lquoted_1:\n {4}\.quad 0x2, Lquoted_1\+17\n {4}\.quad Lquoted_0\+1, Lquoted_1\+33\n {4}\.quad RT_NIL, RT_NIL\n/);
    assert.match(asm, /LOADADDR x0, Lquoted_1\n {4}orr {2}x0, x0, #RT_TAG_LIST/);
    fails("'(1 2.5)", 'test.slight:1:1: not supported yet: (quote (1 2.5))');
});

test("eq? calls rt_equal unless one side is a literal immediate", () => {
    const callsEqual = (src: string): boolean => compile(src).includes('bl   rt_equal');
    assert.equal(callsEqual('(eq? (list 1) (list 1))'), true);
    assert.equal(callsEqual("(eq? '(1) (list 1))"), true);
    assert.equal(callsEqual('(eq? (list 1) :a)'), false);
    assert.equal(callsEqual('(eq? 5 (list 1))'), false);
    assert.equal(callsEqual('(eq? () (list 1))'), false);
    assert.equal(callsEqual('(ne? #true (list 1))'), false);
});

// --- strings ----------------------------------------------------------------

test('a string literal is a box in the read-only data', () => {
    const asm = compile('"héllo"');
    assert.match(asm, /Lstring_0:\n {4}\.quad 6 << RT_BOX_SIZE_SHIFT \| RT_BOX_STRING\n {4}\.asciz "h\\303\\251llo"/);
    assert.match(asm, /LOADADDR x0, Lstring_0\n {4}orr {2}x0, x0, #RT_TAG_BOXED/);
    assert.ok(asm.indexOf('Lstring_0:') > asm.indexOf('RODATA'));
});

test("ts-slight's control-character names are strings, unless shadowed", () => {
    assert.match(compile('\\e'), /\.quad 1 << RT_BOX_SIZE_SHIFT \| RT_BOX_STRING\n {4}\.asciz "\\033"/);
    assert.match(compile('(let \\n 5) \\n'), /ldr {2}x0, \[sp, #0\] {4}\/\/ \\n/);
});

test('C builtins get their site after their arguments', () => {
    assert.match(compile('(str-len "a")'), /ldr {2}x0, \[sp, #0\]\n {4}LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_str_len/);
    // a missing optional argument is ()
    assert.match(compile('(format-num 1 2)'), /mov {2}x2, #RT_NIL\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_format_num/);
    // a variadic one gets a list
    assert.match(compile('(concat "a" 1)'), /orr {2}x0, x2, #RT_TAG_LIST\n {4}LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_concat/);
    assert.match(compile('(concat)'), /mov {2}x0, #RT_NIL\n {4}LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_concat/);
});

test('C builtins check their arity', () => {
    fails('(substring "a" 1)', 'test.slight:1:1: substring takes 3 arguments, not 2');
    fails('(format-num 1)', 'test.slight:1:1: format-num takes 2 or 3 arguments, not 1');
    fails('(format-num 1 2 3 4)', 'test.slight:1:1: format-num takes 2 or 3 arguments, not 4');
    fails('(str? "a" "b")', 'test.slight:1:1: str? takes 1 argument, not 2');
    fails('(defun concat (x) x)', "test.slight:1:8: can't define concat: it's a builtin");
    fails('(defun tty/write (x) x)', "test.slight:1:8: can't define tty/write: it's a builtin");
});

test('strings in quoted lists, and eq? on strings', () => {
    assert.match(compile('\'("a")'), /\.quad Lstring_0\+RT_TAG_BOXED, RT_NIL/);
    assert.ok(compile('(eq? "a" (list 1))').includes('bl   rt_equal'));
    assert.ok(compile('(eq? \'"a" (list 1))').includes('bl   rt_equal'));
});

