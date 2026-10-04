// Tests for code generation. What the generated code *does* is tested by
// the golden tests in t/; these check the pieces that are easy to get
// subtly wrong.

import test from 'node:test';
import assert from 'node:assert/strict';

import { asmString, compileProgram, functionLabel, loadWord } from '../src/codegen.ts';
import { CompileError } from '../src/errors.ts';
import { read } from '../src/reader.ts';
import { RESERVED_SYMBOLS, RUNTIME_SYMBOLS } from '../src/values.ts';

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

test('a local that shadows a builtin is called as a closure', () => {
    assert.match(compile('(let pprint (lambda (x) x)) (pprint 2)'), /ldur x16, \[x9, #RT_CLOSURE_CODE\]/);
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
    assert.match(compile('(defun f () 1) f'), /LOADADDR x0, fn_f_closure/);
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

// Every program has these, in order; its own symbols come after.
const BUILT_IN = [...RESERVED_SYMBOLS, ...RUNTIME_SYMBOLS];
const firstWord = ((BUILT_IN.length << 3) | 5).toString(16);    // the first program symbol's id, shifted and tagged

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

// --- numbers ----------------------------------------------------------------

test('a float literal is a box holding the double', () => {
    const asm = compile('1.5');
    assert.match(asm, /Lfloat_0:\n {4}\.quad 8 << RT_BOX_SIZE_SHIFT \| RT_BOX_FLOAT\n {4}\.quad 0x3ff8000000000000 {4}\/\/ 1\.5/);
    assert.match(compile("'(2.5)"), /\.quad Lfloat_0\+RT_TAG_BOXED, RT_NIL/);
    assert.match(compile('PI'), /\.quad 0x400921fb54442d18/);
});

test('+ and < on two integers are inline; anything else goes out of line', () => {
    const add = compile('(+ 1 2)');
    assert.match(add, /orr {2}x2, x0, x1\n {4}tst {2}x2, #1\n {4}b\.ne (Lslow_\d+)\n {4}adds x0, x1, x0/);
    const slow = /b\.ne (Lslow_\d+)/.exec(add)![1]!;
    // out of line: left back in x0, right in x1, then the site
    assert.ok(add.includes(`${slow}:\n    mov  x2, x0\n    mov  x0, x1\n    mov  x1, x2\n    LOADADDR x2, Lsite_`));
    assert.ok(add.includes(`bl   rt_add\n    b    ${slow}_done`));
    assert.match(compile('(< 1 2)'), /mov {2}x2, #RT_CMP_LT\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_compare/);
});

test('div and % are integers only, and check for zero', () => {
    const asm = compile('(div 7 2) (% 7 2)');
    assert.match(asm, /cbz {2}x0, Lfault_\d+\n {4}sdiv x2, x1, x0\n {4}adds x0, x2, x2/);
    assert.match(asm, /cbz {2}x0, Lfault_\d+\n {4}sdiv x2, x1, x0\n {4}msub x0, x2, x0, x1/);
    assert.match(asm, /RT_FAULT_DIV_ZERO/);
    assert.match(asm, /RT_FAULT_NOT_INT/);
});

test('the math builtins are builtins', () => {
    for (const name of ['/', 'div', '%', 'ceil', 'round', 'sqrt', 'pow', 'min', 'float?', 'num?']) {
        fails(`(defun ${name} (x) x)`, `test.slight:1:8: can't define ${name}: it's a builtin`);
    }
    fails('(sqrt 1 2)', 'test.slight:1:1: sqrt takes 1 argument, not 2');
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
    assert.match(asm, /ldr {2}x3, \[sp, #8\] {4}\/\/ b\n {4}str {2}x3, \[x2, #32\]\n {4}ldr {2}x3, \[sp, #0\] {4}\/\/ a\n {4}str {2}x3, \[x2, #40\]/);
    // inside, they're copied out of the closure (x9) into the frame
    assert.match(asm, /ldur x16, \[x9, #RT_CLOSURE_FREE \+ 0\]\n {4}str {2}x16, \[sp, #0\] {4}\/\/ b/);
    assert.match(asm, /ldur x16, \[x9, #RT_CLOSURE_FREE \+ 8\]\n {4}str {2}x16, \[sp, #8\] {4}\/\/ a/);
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
    fails('(let f format-num)', "test.slight:1:8: format-num can't be used as a value: it takes a varying number of arguments");
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

// --- processes ----------------------------------------------------------------

const W = '(defun w () (recv (m m))) ';

test('recv can only be the whole body of a defun', () => {
    fails('(recv (m m))', 'test.slight:1:1: recv can only be the whole body of a defun');
    fails('(defun f () (pprint 1) (recv (m m)))', 'test.slight:1:24: recv can only be the whole body of a defun');
    fails('(defun f () (cond (#true (recv (m m)))))', 'test.slight:1:26: recv can only be the whole body of a defun');
    fails('(lambda () (recv (m m)))', 'test.slight:1:12: recv can only be the whole body of a defun');
    fails('(fork (recv (m m)))', 'test.slight:1:7: recv can only be the whole body of a defun');
    fails('(defun f () (recv (m (recv (n n)))))', 'test.slight:1:22: recv can only be the whole body of a defun');
});

test('a function that waits can only be called in tail position', () => {
    fails(W + '(defun f () (+ 1 (w)))', 'test.slight:1:44: w waits for messages (it reaches a recv), so it can only be called in tail position');
    fails(W + '(defun g () (w)) (pprint (g))', 'test.slight:1:52: g waits for messages (it reaches a recv), so it can only be called in tail position');
    fails(W + '(cond ((w) 1))', 'test.slight:1:34: w waits for messages (it reaches a recv), so it can only be called in tail position');
});

test('a lambda never waits, and a function that waits is never a value', () => {
    fails(W + '(lambda () (w))', "test.slight:1:38: a lambda can't call w, which waits for messages (it reaches a recv)");
    fails(W + '(let f w)', "test.slight:1:34: w waits for messages, so it can't be used as a value");
});

test('a function that waits can be the body of a fork, the end of the top level, or after a yield', () => {
    assert.doesNotThrow(() => compile(W + '(fork (w))'));
    assert.doesNotThrow(() => compile(W + '(pprint 1) (w)'));
    assert.doesNotThrow(() => compile(W + '(defun y () (yield (w))) (y)'));
    assert.doesNotThrow(() => compile(W + '(lambda () (fork (w)))'));
});

test('recv clauses and patterns', () => {
    fails('(defun f () (recv))', 'test.slight:1:13: recv needs at least one clause');
    fails('(defun f () (recv m))', 'test.slight:1:19: a recv clause is (pattern body...), not m');
    fails('(defun f () (recv (m)))', 'test.slight:1:19: a recv clause is (pattern body...), not (m)');
    fails('(defun f () (recv ((5 a) 1)))', 'test.slight:1:20: a recv pattern is a name, a :keyword or (:keyword names...), not (5 a)');
    fails('(defun f () (recv ((:a 1) 1)))', 'test.slight:1:20: a recv pattern is a name, a :keyword or (:keyword names...), not ((quote a) 1)');
    fails('(defun f () (recv ((:a x x) 1)))', 'test.slight:1:20: x is in the pattern twice');
    fails('(defun f () (recv (#true 1)))', "test.slight:1:20: can't bind #true");
});

test('a receive function restarts at its own entry with its parameters', () => {
    const asm = compile('(defun loop (a b) (recv (m (loop b a))))');
    assert.match(asm, /Lrecv_\d+:\n {4}mov {2}x0, sp\n {4}mov {2}x1, #2\n {4}LOADADDR x2, fn_loop\n {4}bl {3}rt_recv/);
    assert.match(asm, /bl {3}rt_dead_letter\n {4}b {4}Lrecv_\d+/);
});

test('fork compiles its body as a function of its own, taking the locals it uses', () => {
    const asm = compile('(let a 1) (let b "two") (fork (pprint (list b a)))');
    assert.match(asm, /Lfork_\d+: {4}\/\/ fork at test\.slight:1:25/);
    assert.match(asm, /LOADADDR x0, Lfork_\d+\n {4}mov {2}x1, #2\n {4}add {2}x2, sp, #\d+\n {4}LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_fork/);
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
    fails('42\n  (connect :x 1)', "test.slight:2:12: connect's source can be :keypress, :fs/read, :fs/write, :fs/append, :tcp or :tcp/listen, or a device, not (quote x)");
    fails('(connect :keypress)', 'test.slight:1:1: connect takes 2 arguments, not 1');
    fails('(connect)', 'test.slight:1:1: connect takes 2 arguments, not 0');
    fails('(let a 1) (let b 2) (let c 3) (let d 4) (let e 5) (let f 6) (let g 7) (let h 8) (let i 9) (connect :keypress (list a b c d e f g h i))',
          'test.slight:1:91: a connect can take at most 8 locals into the new process, and this one uses 9');
});

test('tty/screen/rows and tty/screen/cols are builtins in the runtime', () => {
    assert.match(compile('(tty/screen/rows)'), /LOADADDR x0, Lsite_\d+\n {4}bl {3}rt_screen_rows\n/);
    assert.match(compile('(tty/screen/cols)'), /LOADADDR x0, Lsite_\d+\n {4}bl {3}rt_screen_cols\n/);
    fails('(tty/screen/rows 1)', 'test.slight:1:1: tty/screen/rows takes 0 arguments, not 1');
});

test('after and sleep are builtins in the runtime, and can be values', () => {
    assert.match(compile('(after 10 $$ :tick)'), /LOADADDR x3, Lsite_\d+\n {4}bl {3}rt_after\n/);
    assert.match(compile('(sleep 10)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_sleep\n/);
    assert.match(compile('(let f after)'), /LOADADDR x0, bi_after_closure/);
    assert.match(compile('(let f sleep)'), /LOADADDR x0, bi_sleep_closure/);
    fails('(after 10 $$)', 'test.slight:1:1: after takes 3 arguments, not 2');
    fails('(sleep)', 'test.slight:1:1: sleep takes 1 argument, not 0');
    fails('(defun sleep (ms) ms)', "test.slight:1:8: can't define sleep: it's a builtin");
});

test('connect to a file evaluates the path in the parent, and passes it and the mode', () => {
    const asm = compile('(defun f (n) n) (let n 1) (let p "x.txt") (connect :fs/write p (f n))');
    // the path goes in the first free slot, the fork's locals after it
    assert.match(asm, /\/\/ p\n {4}str {2}x0, \[sp, #16\]\n {4}ldr {2}x16, \[sp, #0\] {4}\/\/ n\n {4}str {2}x16, \[sp, #24\]/);
    assert.match(asm, /add {2}x2, sp, #24\n {4}LOADADDR x3, Lsite_\d+\n {4}ldr {2}x4, \[sp, #16\]\n {4}mov {2}x5, #RT_FS_WRITE\n {4}bl {3}rt_connect_fs\n/);
    assert.match(compile('(connect :fs/read "a" 1)'), /mov {2}x5, #RT_FS_READ\n/);
    assert.match(compile('(connect :fs/append "a" 1)'), /mov {2}x5, #RT_FS_APPEND\n/);
    fails('(connect :fs/read "a")', 'test.slight:1:1: connect takes 3 arguments, not 2');
    fails('(connect :fs "a" 1)', "test.slight:1:10: connect's source can be :keypress, :fs/read, :fs/write, :fs/append, :tcp or :tcp/listen, or a device, not (quote fs)");
});

test('disconnect is a builtin in the runtime', () => {
    assert.match(compile('(disconnect 1)'), /LOADADDR x1, Lsite_\d+\n {4}bl {3}rt_disconnect\n/);
    fails('(disconnect)', 'test.slight:1:1: disconnect takes 1 argument, not 0');
    fails('(defun disconnect (x) x)', "test.slight:1:8: can't define disconnect: it's a builtin");
});

test('connect over TCP passes the address or the port, and the mode', () => {
    assert.match(compile('(connect :tcp "localhost:80" 1)'), /ldr {2}x4, \[sp, #\d+\]\n {4}mov {2}x5, #RT_TCP_CONNECT\n {4}bl {3}rt_connect_tcp\n/);
    assert.match(compile('(connect :tcp/listen 0 1)'), /mov {2}x5, #RT_TCP_LISTEN\n {4}bl {3}rt_connect_tcp\n/);
    fails('(connect :tcp/listen 0)', 'test.slight:1:1: connect takes 3 arguments, not 2');
});

test('connect given anything but a :keyword hands that device to the new process', () => {
    const asm = compile('(let c 5) (connect c (pprint 1))');
    assert.match(asm, /\/\/ c\n {4}str {2}x0, \[sp, #8\]\n/);
    assert.match(asm, /LOADADDR x3, Lsite_\d+\n {4}ldr {2}x4, \[sp, #8\]\n {4}bl {3}rt_connect_device\n/);
    fails('(let c 5) (connect c)', 'test.slight:1:11: connect takes 2 arguments, not 1');
    fails('(let c 5) (connect c 1 2)', 'test.slight:1:11: connect takes 2 arguments, not 3');
});
