// Tests for code generation. What the generated code *does* is tested by
// the golden tests in t/; these check the pieces that are easy to get
// subtly wrong.

import test from 'node:test';
import assert from 'node:assert/strict';

import { asmString, compileProgram, functionLabel } from '../src/codegen.ts';
import { loadWord } from '../src/aarch64.ts';
import { CompileError } from '../src/errors.ts';
import { expand } from '../src/expand.ts';
import { read } from '../src/reader.ts';
import { TUPLE_OF } from '../src/pattern.ts';
import { int, list, sym } from '../src/sexp.ts';
import { RESERVED_SYMBOLS, RUNTIME_SYMBOLS, symbolWord } from '../src/values.ts';

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
    assert.match(asm, /#include "asm_aarch64.h"/);
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


const fails = (src: string, message: string): void => {
    assert.throws(() => compile(src), (e: unknown) => e instanceof CompileError && e.message === message);
};

test('a tuple pattern can\'t be quoted', () => {
    fails("(pprint '#(1 2))", "test.slight:1:10: a tuple pattern can't be quoted: #(...) is only a pattern, not #(1 2)");
    fails("(pprint '(a #(1)))", "test.slight:1:13: a tuple pattern can't be quoted: #(...) is only a pattern, not #(1)");
});

test('a tuple pattern compiles inline: one compare of the header, and a load for each element (D178)', () => {
    const expanded = (src: string): string => compileProgram(expand(read(src, 'test.slight')));
    const asm = expanded('(defun f (t) (match t (#(a b) (+ a b)) (_ 0))) (f (tuple 1 2))');
    assert.doesNotMatch(asm, /rt_is_tuple_p|rt_tuple_len_of|rt_tuple_ref/);
    assert.match(asm, /ldur x1, \[x0, #-RT_TAG_BOXED\]     \/\/ the header\n    cmp  x1, #516/);
    assert.match(asm, /ldur x0, \[x0, #5\]    \/\/ element 0/);
    assert.match(asm, /ldur x0, \[x0, #13\]    \/\/ element 1/);
    // past ldur's reach, the offset goes in a register
    const wide = `#(${Array.from({ length: 40 }, (_, i) => (i === 39 ? 'z' : '_')).join(' ')})`;
    assert.match(expanded(`(defun g (t) (match t (${wide} z) (_ 0))) (g 1)`), /ldr  x0, \[x0, x1\]    \/\/ element 39/);
});

test("a tuple pattern's test, as a boolean rather than a branch, is the builtins'", () => {
    const program = list(list(sym('pprint'), list(sym(TUPLE_OF), list(sym('tuple'), int(1n), int(2n)), int(2n))));
    assert.match(compileProgram(program), /rt_is_tuple_p[^]*rt_tuple_len_of/);
});

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

test('a local that shadows a builtin is called as a closure', () => {
    assert.match(compile('(let pprint (lambda (x) x)) (pprint 2)'), /ldur x16, \[x9, #RT_CLOSURE_CODE\]/);
});

test('a slot is reused once the value in it is dead', () => {
    // The top level's first slots hold its parameters, @ARGV and %ENV.
    const frame = (src: string): string | undefined => /sub {2}sp, sp, #(\d+)/.exec(compile(src))?.[1];
    const len = (s: string): string => `(str/len "${s}")`;
    assert.equal(frame('42'), '16');                          // just @ARGV and %ENV
    assert.equal(frame(`(+ ${len('a')} ${len('b')}) (+ ${len('c')} ${len('d')})`), '32'); // and one slot, used twice, rounded up
    assert.equal(frame(`(+ ${len('a')} (+ ${len('b')} (+ ${len('c')} ${len('d')})))`), '48'); // and three waiting left operands
    // a local or a literal operand waits nowhere: it's read when it's wanted (D197)
    assert.equal(frame('(+ 1 (+ 2 (+ 3 4)))'), '16');
    assert.equal(frame('(let a 1) (let b 2) (+ a b)'), '32'); // a and b, and no more
});

test('only a function that calls another, not in tail position, zeroes its slots', () => {
    // A frame a collection may read from above has to hold no stale word
    // (D166); a loop's frame comes down at each tail call, so it never is one.
    const body = (src: string, fn: string): string => compile(src).split(`${fn}:`)[1]!.split('    ret')[0]!;
    const zeroes = (src: string, fn: string): boolean => /xzr/.test(body(src, fn));
    const loop = '(defun loop (n acc) (cond ((== n 0) acc) (#true (loop (- n 1) (+ acc n)))))';
    assert.equal(zeroes(`${loop} (loop 3 0)`, 'fn_loop'), false);
    assert.equal(zeroes(`${loop} (defun g (n) (+ 1 (loop n 0))) (g 3)`, 'fn_g'), true);
    assert.equal(zeroes('(defun g (f n) (+ 1 (f n))) (g car 3)', 'fn_g'), true);
    assert.equal(zeroes('(defun g (f n) (+ 1 (apply f (list n)))) (g car 3)', 'fn_g'), true);
    assert.equal(zeroes('(defun g (f n) (f n)) (g car 3)', 'fn_g'), false);
    assert.equal(zeroes('(defun g (n) (+ 1 (str/len n))) (g "abc")', 'fn_g'), false);
    // A lambda's calls are its own, not its enclosing function's.
    assert.equal(zeroes(`${loop} (defun g (n) (lambda () (+ 1 (loop n 0)))) (g 3)`, 'fn_g'), false);
});

test('a string literal all below 0x80 is marked ASCII', () => {
    // so the str/ builtins index it in one step (D219)
    assert.match(compile('"abc"'), /\.quad 3 << RT_BOX_SIZE_SHIFT \| RT_BOX_STRING \| RT_BOX_ASCII\n/);
    assert.match(compile('"é"'), /\.quad 2 << RT_BOX_SIZE_SHIFT \| RT_BOX_STRING\n/);
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
    assert.match(compile('(defun f () 1) f'), /LOADADDR x0, fn_f_closure/);
});

test("a lambda's or a fork's stack fault says where it is once", () => {
    const asm = compile('(let f (lambda (x) x)) (fork (pprint 1))');
    assert.match(asm, /\.asciz "lambda at test\.slight:1:8"/);
    assert.match(asm, /\.asciz "fork at test\.slight:1:24"/);
    assert.doesNotMatch(asm, / at test\.slight:\d+:\d+ at /);
});

test("a defun can't see the top level's lets", () => {
    fails('(let x 1) (defun f () x) (f)', "test.slight:1:23: unknown name 'x'");
});

test('@ARGV is the top level\'s parameter: only it sees @ARGV, and nothing can bind it', () => {
    const only = '@ARGV is only seen at the top level: pass it to the functions that need it';
    fails('(defun f () @ARGV) (f)', `test.slight:1:13: ${only}`);
    fails('(defun f () (lambda () @ARGV)) (f)', `test.slight:1:24: ${only}`);
    fails('(let @ARGV 1)', "test.slight:1:6: can't bind @ARGV");
    fails('(defun f (@ARGV) 1) (f 1)', "test.slight:1:11: can't bind @ARGV");
    fails('(defun @ARGV () 1)', "test.slight:1:8: can't bind @ARGV");
    assert.match(compile('(car @ARGV)'), /str  x0, \[sp, #0\]    \/\/ @ARGV/);
    assert.doesNotThrow(() => compile('((lambda () @ARGV)) (fork (pprint @ARGV))'));
});

test('%ENV is the top level\'s second parameter, and only it sees %ENV', () => {
    const only = '%ENV is only seen at the top level: pass it to the functions that need it';
    fails('(defun f () %ENV) (f)', `test.slight:1:13: ${only}`);
    fails('(defun f () (lambda () %ENV)) (f)', `test.slight:1:24: ${only}`);
    fails('(let %ENV 1)', "test.slight:1:6: can't bind %ENV");
    fails('(defun f (%ENV) 1) (f 1)', "test.slight:1:11: can't bind %ENV");
    assert.match(compile('(car %ENV)'), /str  x1, \[sp, #8\]    \/\/ %ENV/);
    assert.doesNotThrow(() => compile('((lambda () %ENV)) (fork (pprint %ENV))'));
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
    // rt_preempt is told the function's entry, for :system (D171)
    assert.match(asm, /LOADADDR x4, fn_f\n {4}bl {3}rt_preempt\n/);
});

test('every function is in the table of entries and names, the top level too', () => {
    const asm   = compile('(defun f (a) a) (let g (lambda (x) x)) (f 1)');
    const table = asm.slice(asm.indexOf('slight_function_count:'));
    const pairs = [...table.matchAll(/^ {4}\.quad (\S+), (\S+)$/gm)];
    const entry = new Map(pairs.map((m) => [asm.match(new RegExp(`^${m[2]}:\\n {4}\\.asciz "(.*)"$`, 'm'))?.[1], m[1]]));
    assert.match(table, new RegExp(`^ {4}\\.quad ${pairs.length}$`, 'm'));
    assert.equal(entry.get('f'), 'fn_f');
    assert.match(entry.get('lambda at test.slight:1:24') ?? '', /^Llambda_\d+$/);
    assert.equal(entry.get('the top level'), 'slight_main');
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

// Every program has these, in order; its own symbols come after.
const BUILT_IN = [...RESERVED_SYMBOLS, ...RUNTIME_SYMBOLS];
const firstWord = symbolWord(BUILT_IN.length).toString(16);    // the first program symbol's id, shifted and tagged

test('symbols get ids in order of first mention, after #false, #true and the runtime\'s', () => {
    assert.deepEqual(BUILT_IN.slice(0, 4), ['#false', '#true', 'ok', 'error']);
    assert.deepEqual(symbolTable(compile(':b \'a :b (quote c) :a')), [...BUILT_IN, 'b', 'a', 'c']);
    assert.deepEqual(symbolTable(compile('42')), BUILT_IN);
    assert.deepEqual(symbolTable(compile("'#true :x :ok")), [...BUILT_IN, 'x']);
});

test('a symbol is its id, shifted and tagged', () => {
    assert.match(compile(':a'), new RegExp(`movz x0, #0x${firstWord}, lsl #0 {4}// 'a`));
});

test('quote takes one thing', () => {
    fails('(quote)', 'test.slight:1:1: quote takes 1 argument, not 0');
    fails('(quote a b)', 'test.slight:1:1: quote takes 1 argument, not 2');
});

test('equality and the predicates are builtins', () => {
    fails('(eq? 1)', 'test.slight:1:1: eq? takes 2 arguments, not 1');
    fails('(nil? 1 2)', 'test.slight:1:1: nil? takes 1 argument, not 2');
    fails('(defun sym? (x) x)', "test.slight:1:8: can't define sym?: it's a builtin");
    fails('(defun ne? (a b) a)', "test.slight:1:8: can't define ne?: it's a builtin");
});

test('eq? takes any values, so it has no integer check', () => {
    assert.doesNotMatch(compile('(eq? :a 1)'), /RT_FAULT_NOT_INT/);
    assert.match(compile('(== 1 1)'), /bl {3}rt_compare/);
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
    assert.match(consts, new RegExp(`Lquoted_0:\\n {4}\\.quad 0x${firstWord}, RT_NIL\\n`));
    assert.match(consts, /Lquoted_1:\n {4}\.quad 0x2, Lquoted_1\+17\n {4}\.quad Lquoted_0\+1, Lquoted_1\+33\n {4}\.quad RT_NIL, RT_NIL\n/);
    assert.match(asm, /LOADADDR x0, Lquoted_1\n {4}orr {2}x0, x0, #RT_TAG_LIST/);
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

test('the control-character names are strings, unless shadowed', () => {
    assert.match(compile('\\e'), /\.quad 1 << RT_BOX_SIZE_SHIFT \| RT_BOX_STRING \| RT_BOX_ASCII\n {4}\.asciz "\\033"/);
    // after @ARGV and %ENV; a backslash in a comment is escaped (160-backslash-names)
    assert.match(compile('(let \\n 5) \\n'), /ldr {2}x0, \[sp, #16\] {4}\/\/ \\134n/);
});

test('C builtins get their site after their arguments', () => {
    // the last argument worked out stays in the accumulator, which is x0
    assert.match(compile('(str/len "a")'), /orr {2}x0, x0, #RT_TAG_BOXED\n {4}LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_str_len/);
    // a local is read from its slot, straight into its register
    assert.match(compile('(let s "a") (str/slice s 0 1)'), /ldr {2}x0, \[sp, #16\]\n {4}movz x1, #0\n {4}movz x2, #0x2, lsl #0\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_str_slice/);
    // a missing optional argument is ()
    assert.match(compile('(str/pad-start "1" 2)'), /mov {2}x2, #RT_NIL\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_str_pad_start/);
    // a variadic one gets a list
    assert.match(compile('(str "a" 1)'), /orr {2}x0, x2, #RT_TAG_LIST\n {4}LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_str\n/);
    assert.match(compile('(str)'), /mov {2}x0, #RT_NIL\n {4}LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_str\n/);
});

test('the bit operations are inline (D221)', () => {
    // two integers' tagged words, as they are
    assert.match(compile('(let a 12) (let b 10) (bit/and a b)'), /and {2}x0, x1, x0/);
    assert.match(compile('(let a 12) (bit/xor a 3)'), /eor {2}x0, x1, x0/);
    assert.match(compile('(let a 12) (bit/not a)'), /eor {2}x0, x0, #-2/);
    // a shift's count, checked, untagged and clamped to 63
    assert.match(compile('(let a 12) (bit/shl a 3)'), /b\.lt Lfault_\d+\n {4}asr {2}x2, x0, #1\n {4}mov {2}x16, #63\n {4}cmp {2}x2, x16\n {4}csel x2, x2, x16, lt\n {4}lsl {2}x0, x1, x2/);
    assert.match(compile('(let a 12) (bit/shr a 3)'), /asr {2}x0, x1, x2\n {4}and {2}x0, x0, #-2/);
    assert.doesNotMatch(compile('(let a 12) (bit/and a 3)'), /rt_bit/);
    assert.match(compile('(bit/count 255)'), /bl {3}rt_bit_count/);
    assert.match(compile('(let f bit/or) (f 1 2)'), /orr {2}x0, x1, x0/);
    fails('(bit/and 1)', 'test.slight:1:1: bit/and takes 2 arguments, not 1');
    fails('(bit/not 1 2)', 'test.slight:1:1: bit/not takes 1 argument, not 2');
});

test('C builtins check their arity', () => {
    fails('(str/repeat "a")', 'test.slight:1:1: str/repeat takes 2 arguments, not 1');
    fails('(str/slice "a")', 'test.slight:1:1: str/slice takes 2 or 3 arguments, not 1');
    fails('(str/slice "a" 1 2 3)', 'test.slight:1:1: str/slice takes 2 or 3 arguments, not 4');
    fails('(str? "a" "b")', 'test.slight:1:1: str? takes 1 argument, not 2');
    fails('(defun str (x) x)', "test.slight:1:8: can't define str: it's a builtin");
    fails('(defun tty/write (x) x)', "test.slight:1:8: can't define tty/write: it's a builtin");
});

test('strings in quoted lists, and eq? on strings', () => {
    assert.match(compile('\'("a")'), /\.quad Lstring_0\+RT_TAG_BOXED, RT_NIL/);
    assert.ok(compile('(eq? "a" (list 1))').includes('bl   rt_equal'));
    assert.ok(compile('(eq? \'"a" (list 1))').includes('bl   rt_equal'));
});

// --- numbers ----------------------------------------------------------------

test('a float literal is a box holding the double', () => {
    const asm = compile('1.5');
    assert.match(asm, /Lfloat_0:\n {4}\.quad 8 << RT_BOX_SIZE_SHIFT \| RT_BOX_FLOAT\n {4}\.quad 0x3ff8000000000000 {4}\/\/ 1\.5/);
    assert.match(compile("'(2.5)"), /\.quad Lfloat_0\+RT_TAG_BOXED, RT_NIL/);
    assert.match(compile('math/PI'), /\.quad 0x400921fb54442d18/);
    assert.match(compile('math/E'), /\.quad 0x4005bf0a8b145769/);
    assert.match(compile('math/INF'), /\.quad 0x7ff0000000000000/);
});

test('+ and < on two integers are inline; anything else goes out of line', () => {
    const add = compile('(let a 1) (let b 2) (+ a b)');
    // b into the accumulator, a into the left operand, each from its slot (D197)
    assert.match(add, /ldr {2}x0, \[sp, #24\]    \/\/ b\n {4}ldr {2}x1, \[sp, #16\]\n {4}orr {2}x2, x0, x1\n {4}tst {2}x2, #1\n {4}b\.ne (Lslow_\d+)\n {4}adds x0, x1, x0/);
    const slow = /b\.ne (Lslow_\d+)/.exec(add)![1]!;
    // out of line: floats first (D198), then the runtime, with the left back
    // in x0, the right in x1, then the site
    assert.ok(add.includes(`${slow}_c:\n    mov  x2, x0\n    mov  x0, x1\n    mov  x1, x2\n    LOADADDR x2, Lsite_`));
    assert.ok(add.includes(`bl   rt_add\n    b    ${slow}_done`));
    assert.match(compile('(< 1 2)'), /mov {2}x2, #RT_CMP_LT\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_compare/);
});

test('+ or - an integer literal is an immediate, and only the other operand is checked', () => {
    const asm = compile('(let a 1) (+ a 2) (- a 3) (+ a -4) (- a 5000)');
    assert.match(asm, /tst {2}x0, #1\n {4}b\.ne (Lslow_\d+)\n {4}adds x0, x0, #4\n {4}b\.vs Lfault_/);
    assert.match(asm, /subs x0, x0, #6\n {4}b\.vs Lfault_/);
    assert.match(asm, /subs x0, x0, #8\n {4}b\.vs Lfault_/);      // + -4 is - 4
    assert.match(asm, /movz x2, #0x2710, lsl #0\n {4}subs x0, x0, x2/);  // past twelve bits
    // out of line, the literal is the right operand
    assert.match(asm, /movz x1, #0x4, lsl #0\n {4}LOADADDR x2, Lsite_\d+\n {4}bl {3}rt_add/);
    // * has no immediate, since it untags one side, but only a is checked
    assert.match(compile('(let a 1) (* a 2)'), /tst {2}x0, #1\n {4}b\.ne Lslow_\d+\n {4}mov {2}x1, x0\n {4}movz x0, #0x4, lsl #0\n {4}asr {2}x2, x0, #1\n {4}smulh/);
});

test('a comparison as a test branches, without making a boolean', () => {
    const body = (src: string): string => compile(src).split('fn_f:')[1]!.split('    ret')[0]!;
    const iff = (test: string, args = 'n', call = '(f 1)'): string => `(defun f (${args}) (cond (${test} 0) (#true 1))) ${call}`;
    const lit = body(iff('(< n 2)'));
    assert.match(lit, /tst {2}x0, #1\n {4}b\.ne (Lslow_\d+)\n {4}cmp {2}x0, #4\n {4}b\.ge Lcond_next_/);
    assert.doesNotMatch(lit, /csel/);
    // a literal on the left is turned round: 2 < n is n > 2
    assert.match(body(iff('(< 2 n)')), /cmp {2}x0, #4\n {4}b\.le Lcond_next_/);
    assert.match(body(iff('(>= n -3)')), /cmn {2}x0, #6\n {4}b\.lt Lcond_next_/);
    assert.match(body(iff('(== a b)', 'a b', '(f 1 2)')), /cmp {2}x1, x0\n {4}b\.ne Lcond_next_/);
    // out of line, rt_compare's answer is branched on
    const asm = compile(iff('(< n 2)'));
    assert.match(asm, /bl {3}rt_compare\n {4}cmp {2}x0, #RT_FALSE\n {4}b\.eq Lcond_next_\d+\n {4}b {4}Lslow_\d+_done/);
});

test('a cond whose last test is #true never runs out of clauses', () => {
    assert.doesNotMatch(compile('(defun f (n) (cond ((== n 0) 1) (#true 2))) (f 1)'), /RT_FAULT_NO_CLAUSE/);
    assert.match(compile('(defun f (n) (cond ((== n 0) 1))) (f 1)'), /RT_FAULT_NO_CLAUSE/);
});

test('+ - * and / on floats are inline, out of the integers\' way', () => {
    const asm = compile('(let a 1.5) (let b 2.5) (+ a b) (* a 3) (/ a b) (/ a 4) (/ a 0) (< a b)');
    // + two floats: both boxes' headers checked, a box allocated, fadd, stored
    assert.match(asm, /cmp {2}x16, #2050\n {4}b\.ne (Lslow_\d+)_c\n(.*\n){1,12}? {4}ldur d0, \[x1, #5\]\n {4}ldur d1, \[x0, #5\]\n {4}fadd d0, d0, d1\n {4}mov {2}x3, #2050\n {4}str {2}x3, \[x2\]\n {4}str {2}d0, \[x2, #8\]/);
    // * an integer literal: it becomes a double
    assert.match(asm, /movz x16, #0x3, lsl #0\n {4}scvtf d1, x16\n {4}fmul d0, d0, d1/);
    // / two floats: the divisor checked for zero, and no rt_divide on the way
    assert.match(asm, /fcmp d1, #0\.0\n {4}b\.eq Lslow_\d+\n(.*\n){1,12}? {4}fdiv d0, d0, d1/);
    // / by a literal integer that isn't 0 needs no check; by 0, always rt_divide
    assert.match(asm, /movz x16, #0x4, lsl #0\n {4}scvtf d1, x16\n {4}fdiv d0, d0, d1/);
    assert.equal(asm.match(/bl {3}rt_divide/g)!.length, 3);   // out of line, once for each
    // comparisons still ask rt_compare
    assert.match(asm, /bl {3}rt_compare/);
});

test('div and % are integers only, and check for zero', () => {
    const asm = compile('(div 7 2) (% 7 2)');
    assert.match(asm, /cbz {2}x0, Lfault_\d+\n {4}sdiv x2, x1, x0\n {4}adds x0, x2, x2/);
    assert.match(asm, /cbz {2}x0, Lfault_\d+\n {4}sdiv x2, x1, x0\n {4}msub x0, x2, x0, x1/);
    assert.match(asm, /RT_FAULT_DIV_ZERO/);
    assert.match(asm, /RT_FAULT_NOT_INT/);
});

test('the math builtins are builtins', () => {
    for (const name of ['/', 'div', '%', 'math/ceil', 'math/round', 'math/sqrt', 'math/pow', 'math/min', 'float?', 'num?']) {
        fails(`(defun ${name} (x) x)`, `test.slight:1:8: can't define ${name}: it's a builtin`);
    }
    fails('(math/sqrt 1 2)', 'test.slight:1:1: math/sqrt takes 1 argument, not 2');
    fails('(div 1)', 'test.slight:1:1: div takes 2 arguments, not 1');
});

// --- closures and the prelude -------------------------------------------------

const compileWith = (src: string, prelude: string): string =>
    compileProgram(read(src, 'test.slight'), read(prelude, 'prelude.slight'));

test('a lambda that captures nothing is a static closure', () => {
    const asm = compile('(lambda (x) x)');
    assert.match(asm, /Llambda_\d+_closure:\n {4}\.quad 0 << RT_BOX_SIZE_SHIFT \| RT_BOX_CLOSURE\n {4}\.quad Llambda_\d+, 1, Lname_\d+/);
    assert.doesNotMatch(asm, /RT_PROC_HEAP_PTR/);
    // a parameter hides an outer local of the same name
    assert.doesNotMatch(compile('(let x 1) (lambda (x) x)'), /RT_PROC_HEAP_PTR/);
});

test('a lambda that captures is allocated, with its values in order of first use', () => {
    const asm = compile('(let a 1) (let b 2) (lambda () (+ b a))');
    assert.match(asm, /mov {2}x3, #2 << RT_BOX_SIZE_SHIFT \| RT_BOX_CLOSURE/);
    assert.match(asm, /ldr {2}x3, \[sp, #24\] {4}\/\/ b\n {4}str {2}x3, \[x2, #32\]\n {4}ldr {2}x3, \[sp, #16\] {4}\/\/ a\n {4}str {2}x3, \[x2, #40\]/);
    // inside, they're copied out of the closure (x9) into the frame
    assert.match(asm, /ldur x16, \[x9, #RT_CLOSURE_FREE \+ 0\]\n {4}str {2}x16, \[sp, #0\] {4}\/\/ b/);
    assert.match(asm, /ldur x16, \[x9, #RT_CLOSURE_FREE \+ 8\]\n {4}str {2}x16, \[sp, #8\] {4}\/\/ a/);
});

test('a closure takes a multiple of 16 bytes, as every heap object does', () => {
    // 32 bytes of header, code, arity and name, then 8 per captured value
    assert.match(compile('(let a 1) (lambda () a)'), /add {2}x4, x2, #48\n/);
    assert.match(compile('(let a 1) (let b 2) (lambda () (+ a b))'), /add {2}x4, x2, #48\n/);
});

test('a nested lambda makes its outer lambda capture too', () => {
    const asm = compile('(let k 1) (lambda (a) (lambda (b) (+ a (+ b k))))');
    assert.match(asm, /mov {2}x3, #1 << RT_BOX_SIZE_SHIFT \| RT_BOX_CLOSURE/);    // the outer captures k
    assert.match(asm, /mov {2}x3, #2 << RT_BOX_SIZE_SHIFT \| RT_BOX_CLOSURE/);    // the inner a and k
});

test('a call through a value checks it, puts it in x9, and tail-calls with br', () => {
    const asm = compile('(defun f (g) (g 1)) (f f)');
    assert.match(asm, /cmp {2}x16, #RT_BOX_CLOSURE\n {4}b\.ne Lfault_\d+\n {4}ldur x16, \[x0, #RT_CLOSURE_ARITY\]\n {4}cmp {2}x16, #1/);
    assert.match(asm, /mov {2}x9, x0\n {4}ldr {2}x0, \[sp, #\d+\]\n {4}ldur x16, \[x9, #RT_CLOSURE_CODE\]\n {4}mov {2}sp, x29\n {4}ldp {2}x29, x30, \[sp\], #16\n {4}br {3}x16/);
    assert.match(compile('(defun f (g) (+ 1 (g 1))) (f f)'), /blr {2}x16/);
    fails('(5 1)', "test.slight:1:2: 5 isn't a function");
});

test('apply in tail position is a jump to rt_apply', () => {
    assert.match(compile('(apply car (list 1))'), /b {4}rt_apply {4}\/\/ tail call/);
    assert.match(compile('(+ 1 (apply car (list 1)))'), /bl {3}rt_apply/);
    fails('(apply car)', 'test.slight:1:1: apply takes 2 arguments, not 1');
});

test('a builtin used as a value is a small wrapper function', () => {
    const asm = compile('(let f car) (let g car) (f (list 1))');
    assert.match(asm, /bi_car:\n|bi_car: {4}\/\/ car/);
    assert.equal(asm.split('bi_car_closure:').length, 2);       // made once
    fails('(let f list)', "test.slight:1:8: list can't be used as a value: it takes a varying number of arguments");
    fails('(let f str)', "test.slight:1:8: str can't be used as a value: it takes a varying number of arguments");
    // with optional arguments, it takes the ones it needs (D219)
    assert.match(compile('(let f str/slice) (f "abc" 1)'), /mov {2}x2, #RT_NIL\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_str_slice/);
});

test('lambda is (lambda (params...) body...)', () => {
    fails('(lambda (x))', 'test.slight:1:1: lambda is (lambda (params...) body...)');
    fails('(lambda x x)', "test.slight:1:9: lambda's parameters must be a list, not x");
    fails('(lambda (x x) x)', 'test.slight:1:12: x is a parameter twice');
});

test("the prelude's functions are in their own namespace", () => {
    const prelude = '(defun twice (x) (* 2 x)) (defun quad (x) (twice (twice x)))';
    const asm = compileWith('(defun twice (x) x) (twice (quad 1))', prelude);
    assert.match(asm, /pf_twice: {4}\/\/ twice/);
    assert.match(asm, /fn_twice: {4}\/\/ twice/);
    // the prelude's quad calls the prelude's twice; the program calls its own
    assert.match(asm.slice(asm.indexOf('pf_quad:')), /bl {3}pf_twice/);
    assert.match(asm.slice(asm.indexOf('FUNC slight_main')), /bl {3}pf_quad[^]*b {4}fn_twice/);
    assert.throws(() => compileWith('(defun quad (x) x) (defun quad (x) x)', prelude),
        (e: unknown) => e instanceof CompileError && /quad is already defined/.test(e.message));
    assert.throws(() => compileWith('1', '(pprint 1)'),
        (e: unknown) => e instanceof CompileError && /the prelude can only define functions/.test(e.message));
});

test("the prelude's not is inline, as a value and as a test; a program's own not is called (D203)", () => {
    const prelude = '(defun not (a) (cond (a #false) (#true #true)))';
    const main  = (asm: string) => asm.slice(asm.indexOf('FUNC slight_main'));
    const calls = /\bbl? +pf_not\b/;                   // a call or a tail call; its stub and table entry follow main too
    assert.doesNotMatch(main(compileWith('(not (cons? 1))', prelude)), calls);
    const test = main(compileWith('(cond ((not (< 1 2)) 1) (#true 2))', prelude));
    assert.doesNotMatch(test, calls);
    assert.match(test, /b\.ge/);                      // (< 1 2)'s own compare and branch
    assert.match(main(compileWith('(defun not (a) a) (not #true)', prelude)), /b {4}fn_not/);
    assert.match(main(compileWith('(map not (list #true))', prelude + ' (defun map (f xs) xs)')), /pf_not/);
});

// --- processes ----------------------------------------------------------------

const W = '(defun w () (recv (m m))) ';

test('recv goes anywhere a tail call could (D167), but not in a lambda', () => {
    assert.doesNotThrow(() => compile('(spawn (recv (m m)))'));
    assert.doesNotThrow(() => compile('(defun f () (pprint 1) (recv (m m))) (spawn (f))'));
    assert.doesNotThrow(() => compile('(defun f () (cond (#true (recv (m m))))) (spawn (f))'));
    assert.doesNotThrow(() => compile('(defun f () (recv (m (recv (n n))))) (spawn (f))'));
    assert.doesNotThrow(() => compile('(defun f () (recv)) (spawn (f))'));
    assert.doesNotThrow(() => compile('(spawn (pprint (list (recv) (recv))))'));
    fails('(lambda () (recv (m m)))', "test.slight:1:12: recv can't be in a lambda: a lambda can't wait for messages");
    fails('(lambda () (fork 1) (pprint (recv)))', "test.slight:1:29: recv can't be in a lambda: a lambda can't wait for messages");
    fails('(defun f () (cond ((recv) 1)) 2) (f)',
        "test.slight:1:13: a recv can only be in a cond that's in tail position; take the message first, with (let m (recv ...))");
    fails('(defun f () (pprint (cond (#true (recv))))) (f)',
        "test.slight:1:21: a recv can only be in a cond that's in tail position; take the message first, with (let m (recv ...))");
    fails('(defun f () (do (let x (recv)) x) 2) (f)',
        "test.slight:1:13: a recv can only be in a do that's in tail position, or one with no lets; take the message first, with (let m (recv ...))");
    fails('(defun f () (yield (recv)) 2) (f)',
        "test.slight:1:13: a recv can only be in a yield that's in tail position; take the message first, with (let m (recv ...))");
    // a case or a match becomes a do that binds its topic, and is named as written
    const expanded = (src: string, message: string): void =>
        assert.throws(() => compileProgram(expand(read(src, 'test.slight'))), (e: unknown) => e instanceof CompileError && e.message === message);
    expanded('(defun f (x) (let v (case x (1 (recv (m m))) (#true 2))) v) (f 1)',
        "test.slight:1:21: a recv can only be in a case that's in tail position; take the message first, with (let m (recv ...))");
    expanded('(defun f (x) (match x (1 (recv (m m))) (_ 2)) 3) (f 1)',
        "test.slight:1:14: a recv can only be in a match that's in tail position; take the message first, with (let m (recv ...))");
    expanded('(defun f () (do (let x (recv)) x) 2) (f)',
        "test.slight:1:13: a recv can only be in a do that's in tail position, or one with no lets; take the message first, with (let m (recv ...))");
});

test('a function with a recv in it waits, so it can only be called in tail position', () => {
    fails('(defun f () (let m (recv)) m) (defun g () (pprint (f)))', 'test.slight:1:51: f waits for messages (it reaches a recv), so it can only be called in tail position');
});

test('a function that waits can only be called in tail position', () => {
    fails(W + '(defun f () (+ 1 (w)))', 'test.slight:1:44: w waits for messages (it reaches a recv), so it can only be called in tail position');
    fails(W + '(defun g () (w)) (defun h () (pprint (g)))', 'test.slight:1:64: g waits for messages (it reaches a recv), so it can only be called in tail position');
    fails(W + '(defun h () (cond ((w) 1)))', 'test.slight:1:46: w waits for messages (it reaches a recv), so it can only be called in tail position');
});

test('a lambda never waits, and a function that waits is never a value', () => {
    fails(W + '(lambda () (w))', "test.slight:1:38: a lambda can't call w, which waits for messages (it reaches a recv)");
    fails(W + '(let f w)', "test.slight:1:34: w waits for messages, so it can't be used as a value");
});

test('a function that waits can be the body of a spawn, the end of a connect, or after a yield', () => {
    assert.doesNotThrow(() => compile(W + '(spawn (w))'));
    assert.doesNotThrow(() => compile(W + '(spawn (do (pprint 1) (w)))'));
    assert.doesNotThrow(() => compile(W + '(connect :keypress (w))'));
    assert.doesNotThrow(() => compile(W + '(defun y () (yield (w))) (spawn (y))'));
    assert.doesNotThrow(() => compile(W + '(lambda () (spawn (w)))'));
});

test("a task, a fork's body or the top level, can't wait (D243)", () => {
    const task = "a task (a fork's body, or the top level) can't wait for messages";
    fails(W + '(fork (w))', `test.slight:1:33: ${task}, and w, which waits for messages (it reaches a recv), would: spawn an actor for it`);
    fails(W + '(pprint 1) (w)', `test.slight:1:38: ${task}, and w, which waits for messages (it reaches a recv), would: spawn an actor for it`);
    fails('(recv (m m))', `test.slight:1:1: ${task}, and this recv would: spawn an actor for it`);
    fails('(fork (do (pprint 1) (recv)))', `test.slight:1:22: ${task}, and this recv would: spawn an actor for it`);
    fails('(spawn (fork (recv)))', `test.slight:1:14: ${task}, and this recv would: spawn an actor for it`);
});

test('recv clauses and patterns', () => {
    fails('(defun f () (recv m))', 'test.slight:1:19: a recv clause is (pattern body...), not m');
    fails('(defun f () (pprint 1) (recv m))', 'test.slight:1:30: a recv clause is (pattern body...), not m');
    fails('(defun f () (recv (m)))', 'test.slight:1:19: a recv clause is (pattern body...), not (m)');
    fails('(defun f () (recv ((:a x x) 1)))', 'test.slight:1:26: x is in the pattern twice');
    fails('(defun f () (recv ((:a &) 1)))', 'test.slight:1:20: & can only come before the last pattern of a list, not ((quote a) &)');
    fails('(defun f () (recv ((:a $$) 1)))', "test.slight:1:24: can't bind $$");
    fails('(defun f () (recv ((:a cond) 1)))', "test.slight:1:24: can't bind cond");
    // patterns are match's (pattern.ts): literals anywhere, nesting, and a rest
    assert.doesNotThrow(() => compile('(defun f () (recv ((5 a) a) ((:a 1) 1) (#true 2) (((x y) & r) r)))'));
});

test('a receive function restarts at its own entry with its parameters', () => {
    const asm = compile('(defun loop (a b) (recv (m (loop b a))))');
    assert.match(asm, /Lrecv_\d+:\n {4}mov {2}x0, x28\n {4}mov {2}x1, sp\n {4}mov {2}x2, #2\n {4}LOADADDR x3, fn_loop\n {4}bl {3}rt_recv/);
    assert.match(asm, /bl {3}rt_dead_letter\n {4}b {4}Lrecv_\d+/);
});

test('fork compiles its body as a function of its own, taking the locals it uses', () => {
    const asm = compile('(let a 1) (let b "two") (fork (pprint (list b a)))');
    assert.match(asm, /Lfork_\d+: {4}\/\/ fork at test\.slight:1:25/);
    assert.match(asm, /mov {2}x0, x28\n {4}LOADADDR x1, Lfork_\d+\n {4}mov {2}x2, #2\n {4}add {2}x3, sp, #\d+\n {4}LOADADDR x4, Lsite_\d+\n {4}bl {3}rt_fork/);
    fails('(fork)', 'test.slight:1:1: fork takes 1 argument, not 0');
    fails('(let a 1) (let b 2) (let c 3) (let d 4) (let e 5) (let f 6) (let g 7) (let h 8) (let i 9) (fork (list a b c d e f g h i))',
          'test.slight:1:91: a fork can take at most 8 locals into the new process, and this one uses 9');
});

test('$$ and ^$$ are the process and its parent', () => {
    assert.match(compile('$$'), /ldr {2}x0, \[x28, #RT_PROC_PID\]/);
    assert.match(compile('^$$'), /ldr {2}x0, \[x28, #RT_PROC_PARENT\]/);
    fails('(let $$ 1)', "test.slight:1:6: can't bind $$");
});


test('join, monitor, kill and raise are builtins in the runtime, and can be values', () => {
    for (const [name, fn] of [['join', 'rt_join'], ['monitor', 'rt_monitor'], ['kill', 'rt_kill'], ['raise', 'rt_raise']]) {
        assert.match(compile(`(${name} 1)`), new RegExp(`LOADADDR x1, Lsite_\\d+\\n {4}bl {3}${fn}\\n`));
        assert.match(compile(`(let f ${name})`), new RegExp(`LOADADDR x0, bi_${name}_closure`));
        fails(`(${name})`, `test.slight:1:1: ${name} takes 1 argument, not 0`);
        fails(`(defun ${name} (x) x)`, `test.slight:1:8: can't define ${name}: it's a builtin`);
    }
});

test('connect is a fork whose process gets the keys, from :keypress', () => {
    assert.match(compile('(defun f (n) n) (let n 1) (connect :keypress (f n))'), /LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_connect\n/);
    assert.match(compile('(connect :keypress 1)'), /\.asciz "connect at test\.slight:1:1"/);
    fails('42\n  (connect :x 1)', "test.slight:2:12: connect's source can be :keypress, :system, :fs/read, :fs/write, :fs/append, :tcp, :tcp/listen, :unix, :unix/listen or :exec, or a device, not (quote x)");
    fails('(connect :keypress)', 'test.slight:1:1: connect takes 2 arguments, not 1');
    fails('(connect)', 'test.slight:1:1: connect takes 2 arguments, not 0');
    fails('(let a 1) (let b 2) (let c 3) (let d 4) (let e 5) (let f 6) (let g 7) (let h 8) (let i 9) (connect :keypress (list a b c d e f g h i))',
          'test.slight:1:91: a connect can take at most 8 locals into the new process, and this one uses 9');
});

test('connect :system is a fork whose process gets the :system device', () => {
    assert.match(compile('(defun f (n) n) (let n 1) (connect :system (f n))'), /LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_connect_system\n/);
    fails('(connect :system)', 'test.slight:1:1: connect takes 2 arguments, not 1');
});

test('tty/screen/rows and tty/screen/cols are builtins in the runtime', () => {
    assert.match(compile('(tty/screen/rows)'), /LOADADDR x0, Lsite_\d+\n {4}bl {3}rt_screen_rows\n/);
    assert.match(compile('(tty/screen/cols)'), /LOADADDR x0, Lsite_\d+\n {4}bl {3}rt_screen_cols\n/);
    fails('(tty/screen/rows 1)', 'test.slight:1:1: tty/screen/rows takes 0 arguments, not 1');
});

test('clock/us and hash are builtins in the runtime, and can be values', () => {
    assert.match(compile('(clock/us)'), /LOADADDR x0, Lsite_\d+\n {4}bl {3}rt_clock_us\n/);
    assert.match(compile('(hash 1)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_hash\n/);
    assert.match(compile('(let f hash)'), /LOADADDR x0, bi_hash_closure/);
    fails('(clock/us 1)', 'test.slight:1:1: clock/us takes 0 arguments, not 1');
    fails('(hash)', 'test.slight:1:1: hash takes 1 argument, not 0');
});

test('after, timer/cancel and sleep are builtins in the runtime, and can be values', () => {
    assert.match(compile('(after 10 $$ :tick)'), /LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_after\n/);
    assert.match(compile('(timer/cancel 1)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_timer_cancel\n/);
    assert.match(compile('(sleep 10)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_sleep\n/);
    assert.match(compile('(let f after)'), /LOADADDR x0, bi_after_closure/);
    assert.match(compile('(let f timer/cancel)'), /LOADADDR x0, bi_timer_2fcancel_closure/);
    assert.match(compile('(let f sleep)'), /LOADADDR x0, bi_sleep_closure/);
    fails('(after 10 $$)', 'test.slight:1:1: after takes 3 arguments, not 2');
    fails('(timer/cancel)', 'test.slight:1:1: timer/cancel takes 1 argument, not 0');
    assert.match(compile('(socket/peer $$)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_socket_peer\n/);
    fails('(socket/peer)', 'test.slight:1:1: socket/peer takes 1 argument, not 0');
    fails('(sleep)', 'test.slight:1:1: sleep takes 1 argument, not 0');
    fails('(defun sleep (ms) ms)', "test.slight:1:8: can't define sleep: it's a builtin");
});

test('connect to a file evaluates the path in the parent, and passes it and the mode', () => {
    const asm = compile('(defun f (n) n) (let n 1) (let p "x.txt") (connect :fs/write p (f n))');
    // the path goes in the first free slot (after @ARGV, %ENV, n and p), the fork's locals after it
    assert.match(asm, /\/\/ p\n {4}str {2}x0, \[sp, #32\]\n {4}ldr {2}x16, \[sp, #16\] {4}\/\/ n\n {4}str {2}x16, \[sp, #40\]/);
    assert.match(asm, /add {2}x2, sp, #40\n {4}LOADADDR x3, Lsite_\d+\n {4}ldr {2}x4, \[sp, #32\]\n {4}mov {2}x5, #RT_FS_WRITE\n {4}bl {3}rt_connect_fs\n/);
    assert.match(compile('(connect :fs/read "a" 1)'), /mov {2}x5, #RT_FS_READ\n/);
    assert.match(compile('(connect :fs/append "a" 1)'), /mov {2}x5, #RT_FS_APPEND\n/);
    fails('(connect :fs/read "a")', 'test.slight:1:1: connect takes 3 arguments, not 2');
    fails('(connect :fs "a" 1)', "test.slight:1:10: connect's source can be :keypress, :system, :fs/read, :fs/write, :fs/append, :tcp, :tcp/listen, :unix, :unix/listen or :exec, or a device, not (quote fs)");
});

test('disconnect is a builtin in the runtime', () => {
    assert.match(compile('(disconnect 1)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_disconnect\n/);
    fails('(disconnect)', 'test.slight:1:1: disconnect takes 1 argument, not 0');
    fails('(defun disconnect (x) x)', "test.slight:1:8: can't define disconnect: it's a builtin");
});

test('connect over TCP passes the address or the port, and the mode', () => {
    assert.match(compile('(connect :tcp "localhost:80" 1)'), /ldr {2}x4, \[sp, #\d+\]\n {4}mov {2}x5, #RT_TCP_CONNECT\n {4}bl {3}rt_connect_tcp\n/);
    assert.match(compile('(connect :tcp/listen 0 1)'), /mov {2}x5, #RT_TCP_LISTEN\n {4}bl {3}rt_connect_tcp\n/);
    assert.match(compile('(connect :unix "a.sock" 1)'), /mov {2}x5, #RT_UNIX_CONNECT\n {4}bl {3}rt_connect_tcp\n/);
    assert.match(compile('(connect :unix/listen "a.sock" 1)'), /mov {2}x5, #RT_UNIX_LISTEN\n {4}bl {3}rt_connect_tcp\n/);
    fails('(connect :tcp/listen 0)', 'test.slight:1:1: connect takes 3 arguments, not 2');
});

test('connect :exec passes the command, and no mode', () => {
    assert.match(compile('(connect :exec (list "ls") 1)'), /LOADADDR x3, Lsite_\d+\n {4}ldr {2}x4, \[sp, #\d+\]\n {4}bl {3}rt_connect_exec\n/);
    fails('(connect :exec (list "ls"))', 'test.slight:1:1: connect takes 3 arguments, not 2');
});

test('connect given anything but a :keyword hands that device to the new process', () => {
    const asm = compile('(let c 5) (connect c (pprint 1))');
    assert.match(asm, /\/\/ c\n {4}str {2}x0, \[sp, #24\]\n/);
    assert.match(asm, /LOADADDR x3, Lsite_\d+\n {4}ldr {2}x4, \[sp, #24\]\n {4}bl {3}rt_connect_device\n/);
    fails('(let c 5) (connect c)', 'test.slight:1:11: connect takes 2 arguments, not 1');
    fails('(let c 5) (connect c 1 2)', 'test.slight:1:11: connect takes 2 arguments, not 3');
});

test('a long body compiles: its forms are a loop, not a recursion that runs Node out of stack', () => {
    const forms = Array.from({ length: 50000 }, (_, i) => `(pprint ${i})`).join(' ');
    assert.match(compile(forms), / {4}\/\/ 49999\n/);
    assert.match(compile(`(defun f () ${forms} 0) (f)`), /^fn_f:/m);
});

test('a function with more locals than its frame can hold is a compile error', () => {
    const lets = (n: number): string => Array.from({ length: n }, (_, i) => `(let x${i} ${i})`).join(' ');
    assert.match(compile(`${lets(500)} x0`), /sub {2}sp, sp, #4016\n/);
    assert.throws(() => compile(`${lets(600)} x0`), /the top level needs too many frame slots \(602\)/);
});
