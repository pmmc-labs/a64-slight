// Code generation: s-expressions to assembly text, for a target (target.ts:
// AArch64 in aarch64.ts, x86-64 in x86_64.ts).
//
// Ghuloum-style: every expression leaves its value in the accumulator, and
// values that have to wait (arguments, a binary op's left operand, a let's
// value) go to slots in the function's frame. No register allocation, and
// nothing lives in a register across a call. But an operand that needs no
// code to work out (a local, or a literal integer, (), #true or #false)
// doesn't wait: it's loaded where it's wanted when it's wanted; and an
// integer literal added, subtracted or compared with is an immediate when
// the target has one (D197). + - * and / on floats are done inline, out
// of the integers' way, with the result's box allocated inline (D198).
//
// Functions take up to eight arguments in registers and return their result
// in the accumulator. The registers are the platform C calling convention's
// (on x86-64, its six, then two of ours), so compiled functions and the
// runtime's C functions are called the same way.
// A call in tail position takes down the caller's frame and jumps, so loops
// run in constant stack.
//
// The output is a .S file that includes the target's header in runtime/
// (asm_aarch64.h, asm_x86_64.h), so the differences between Mach-O and ELF stay
// there, and rt.h's constants (RT_TRUE, RT_FAULT_OVERFLOW, ...) can be used
// by name.
//
// What it compiles is what expand.ts and split.ts leave: the special
// forms (SPECIAL_FORMS, below), calls and tail calls, closures, and the
// builtins, inline or as calls to the runtime's C (docs/LANGUAGE.md has
// them all), with the recv rule (classify.ts) enforced here. Cons cells,
// tuples and closures are bump-allocated in the process's heap, inline;
// floats, strings, closures and tuples are boxes; literals, and closures
// that capture nothing, are static data. The top-level forms that aren't defuns are the body of
// slight_main, whose parameters are @ARGV and %ENV, the program's
// arguments and environment: so only they can see them (D144, D186).
//
// The prelude (lib/prelude.slight) is compiled with every program. Its
// functions are in their own namespace: a program can define a function
// with a prelude name, and then its code uses its own definition while the
// prelude keeps using the prelude's (D80).

import { CompileError } from './errors.ts';
import { float, form, isForm, list, NIL, posOf, show, str, sym, toArray, TUPLE, type Int, type Pair, type Pos, type Sexp, type Sym } from './sexp.ts';
import { intWord, MAX_ARGS, RESERVED_SYMBOLS, RUNTIME_SYMBOLS, symbolWord, TOP_PARAMS, tupleHeader } from './values.ts';
import { isReceiveBody, stateFunctions } from './classify.ts';
import { BUILTIN, lowerPattern, NO_MATCH, ON_CONSES, RECV_MESSAGE, TUPLE_OF, TUPLE_REF } from './pattern.ts';
import { splitProgram } from './split.ts';
import { eraseTypes } from './check.ts';
import { displayName, PRELUDE_SUFFIX } from './modules.ts';
import { AARCH64 } from './aarch64.ts';
import { ACC, addr, FP, FRAME, imm, inSlot, LEFT, PROC, slotAddr, word, type BitOp, type Code, type Cond, type FloatOp, type Operand, type Target } from './target.ts';

// The locals in scope, innermost first: an association list from name to
// frame slot.
type Env = { readonly name: string; readonly slot: number; readonly next: Env } | null;

// The top-level functions: an association list from name to label and arity.
type Module = 'user' | 'prelude';
// state: it can reach a recv (classify.ts), so the recv rule applies to it.
type Fn  = { readonly name: string; readonly label: string; readonly arity: number; readonly module: Module; readonly state: boolean };
type Fns = { readonly fn: Fn; readonly next: Fns } | null;

// Where an expression is being compiled: the functions and locals it can
// see, the first free frame slot (anything below si belongs to someone),
// whether it's in tail position, whether it's in a lambda's body, and, for
// the recv that is the whole body of a receive function, that function's
// entry and number of parameters (to start it again when a message comes).
type Cx = {
    readonly fns: Fns;
    readonly env: Env;
    readonly si: number;
    readonly tail: boolean;
    readonly inLambda: boolean;
    readonly task: boolean;          // a fork's body, or the top level: it can't wait (D243)
    readonly receive: { readonly entry: string; readonly params: number } | null;
};

// The symbols seen so far, newest first: an association list from name to
// id. Ids are handed out in order, starting with the reserved symbols.
type Syms = { readonly name: string; readonly id: number; readonly next: Syms } | null;

// What compiling accumulates: how many labels have been made, how many
// frame slots the current function needs, whether it calls a compiled
// function other than in tail position (so a collection can start above
// its frame, which then has to hold nothing stale), the symbols, the
// out-of-line code (fault calls, preemption) to emit after all the
// functions, and the read-only data: strings (data) and quoted lists
// (consts). It also carries the target, which never changes.
type St = {
    readonly t: Target;
    readonly labels: number;
    readonly slots: number;
    readonly calls: boolean;
    readonly symbols: Syms;
    readonly stubs: Code;
    readonly data: Code;
    readonly consts: Code;
    readonly lambdas: Code;                 // functions compiled along the way: lambdas, builtin wrappers
    readonly closures: readonly string[];   // functions whose static closure has been emitted
    readonly functions: Code;               // every function's entry and name, for the runtime (D171)
};

// The special forms, and those the expander turns into others (expand.ts),
// which it leaves none of: neither kind can be a name.
const SPECIAL_FORMS: readonly string[] = [
    'defun', 'lambda', 'let', 'cond', 'do', 'quote', 'fork', 'spawn', 'connect', 'recv', 'yield',
    'if', 'when', 'case', 'and', 'or', 'match', '@include', 'deftype', 'sig', 'the',
];

// --- the program ------------------------------------------------------------

export function compileProgram(typed: Sexp, typedPrelude: Sexp = NIL, t: Target = AARCH64): string {
    // types say nothing to the code (D247): deftype and sig go, (the T e) is e
    const forms   = eraseTypes(typed);
    const prelude = eraseTypes(typedPrelude);
    const preludeForms = toArray(prelude)!;
    preludeForms.forEach((f) => {
        if (!isForm(f, 'defun')) throw new CompileError(`the prelude can only define functions, not ${show(f)}`, posOf(f));
    });
    const preludeDefuns = preludeForms.map((f) => checkDefun(f as Pair));
    const preludeFns    = preludeDefuns.reduce<Fns>((fns, d) => declare(fns, d, 'prelude', false), null);

    const all    = toArray(splitProgram(forms))!;     // every recv the whole body of a receive function (D167)
    const defuns = all.filter((f) => isForm(f, 'defun')).map((f) => checkDefun(f as Pair));
    const state  = stateFunctions(defuns.map((d) => ({ name: d.name.name, params: d.params.map((p) => p.name), body: d.body })));
    const fns    = defuns.reduce<Fns>((acc, d) => declare(acc, d, 'user', state.includes(d.name.name)), preludeFns);
    const top    = all.filter((f) => !isForm(f, 'defun'));
    const empty: St = { t, labels: 0, slots: 0, calls: false, symbols: null, stubs: [], data: [], consts: [], lambdas: [], closures: [], functions: [] };
    const st0 = [...RESERVED_SYMBOLS, ...RUNTIME_SYMBOLS].reduce((st, name) => symbolId(st, name)[1], empty);

    const compileAll = (ds: readonly Defun[], visible: Fns, module: Module, st: St): [Code, St] =>
        ds.reduce<[Code, St]>(([acc, s], d) => {
            const [c, s1] = compileFunction(findFn(visible, d.name.name, module)!.label, d, [], visible, 'defun', s);
            return [[acc, c], s1];
        }, [[], st]);
    const [preludeCode, st1] = compileAll(preludeDefuns, preludeFns, 'prelude', st0);
    const [code, st2]        = compileAll(defuns, fns, 'user', st1);
    const params: Sym[] = TOP_PARAMS.map((name) => ({ t: 'sym', name, pos: null }));
    const main: Defun   = { name: { t: 'sym', name: 'the top level', pos: null }, params, body: list(...top, ...(top.length === 0 ? [NIL] : [])), pos: null };
    const [mainCode, st3] = compileFunction('slight_main', main, [], fns, 'task', st2);

    return flatten([
        '// generated by slightc -- do not edit',
        t.fileStart,
        preludeCode,
        code,
        st3.lambdas,
        mainCode,
        st3.stubs,
        '',
        // the runtime shares what's between these labels instead of copying it
        '    RODATA',
        '    .globl slight_rodata_start',
        'slight_rodata_start:',
        st3.data,
        symbolTable(st3.symbols),
        '    .globl slight_rodata_end',
        'slight_rodata_end:',
        '',
        '    CONSTDATA',
        '    .p2align 4',
        '    .globl slight_const_start',
        'slight_const_start:',
        st3.consts,
        '    .globl slight_const_end',
        'slight_const_end:',
        '',
        // where :system and the deadlock message find a process's function's name
        '    .p2align 3',
        '    .globl slight_function_count',
        'slight_function_count:',
        `    .quad ${countFunctions(st3.functions)}`,
        '    .globl slight_functions',
        'slight_functions:',
        st3.functions,
    ]).join('\n') + '\n';
}

function countFunctions(functions: Code): number {
    return flatten(functions).length;
}

// Every symbol's name, in id order, for the runtime's printer.
function symbolTable(symbols: Syms): Code {
    const names: string[] = [];
    for (; symbols !== null; symbols = symbols.next) names.unshift(symbols.name);
    return [
        '    .p2align 3',
        '    .globl slight_symbol_count',
        'slight_symbol_count:',
        `    .quad ${names.length}`,
        '    .globl slight_symbol_names',
        'slight_symbol_names:',
        names.map((name) => `    .asciz ${asmString(name)}`),
    ];
}

// --- functions --------------------------------------------------------------

type Defun = { readonly name: Sym; readonly params: readonly Sym[]; readonly body: Sexp; readonly pos: Pos | null };

function checkDefun(x: Pair): Defun {
    const items = toArray(x)!;
    const [, name, params] = items;
    if (items.length < 4 || name === undefined || params === undefined) {
        throw new CompileError('defun is (defun name (params...) body...)', x.pos);
    }
    if (name.t !== 'sym') throw new CompileError(`defun needs a name, not ${show(name)}`, posOf(name) ?? x.pos);
    checkBindable(name);
    if (isBuiltin(name.name)) throw new CompileError(`can't define ${name.name}: it's a builtin`, name.pos);
    return { name, params: checkParams(name.name, params, x, name.pos), body: list(...items.slice(3)), pos: x.pos };
}

function checkParams(owner: string, params: Sexp, x: Pair, ownerPos: Pos | null): readonly Sym[] {
    const ps = toArray(params);
    if (ps === null) throw new CompileError(`${owner}'s parameters must be a list, not ${show(params)}`, posOf(params) ?? x.pos);
    const syms = ps.map((p) => {
        if (p.t !== 'sym') throw new CompileError(`a parameter must be a name, not ${show(p)}`, posOf(p) ?? x.pos);
        checkBindable(p);
        return p;
    });
    syms.forEach((p, i) => {
        if (syms.findIndex((q) => q.name === p.name) !== i) throw new CompileError(`${p.name} is a parameter twice`, p.pos);
    });
    if (syms.length > MAX_ARGS) {
        throw new CompileError(`${owner} has ${syms.length} parameters; the most is ${MAX_ARGS}`, ownerPos ?? x.pos);
    }
    return syms;
}

// A program's functions are declared on top of the prelude's, so they
// shadow them; only a second definition in the same module is an error.
function declare(fns: Fns, d: Defun, module: Module, state: boolean): Fns {
    const existing = lookupFn(fns, d.name.name);
    if (existing !== null && existing.module === module) throw new CompileError(`${d.name.name} is already defined`, d.name.pos);
    const label = module === 'user' ? functionLabel(d.name.name) : `pf_${functionLabel(d.name.name).slice(3)}`;
    return { fn: { name: d.name.name, label, arity: d.params.length, module, state }, next: fns };
}

function findFn(fns: Fns, name: string, module: Module): Fn | null {
    for (; fns !== null; fns = fns.next) if (fns.fn.name === name && fns.fn.module === module) return fns.fn;
    return null;
}

// The frame: the linkage on top, then the slots. The parameters go to the
// first slots straight away, then a closure's captured values (`free`), so
// inside the body they're all just locals. Then two checks, which every
// call and every tail call passes through: is there room on the stack, and
// are this process's reductions used up? rt_preempt may collect (D166):
// its roots are the locals so far, and the frames below. A function that
// calls another, not in tail position, can be one of those, so it zeroes
// the rest of its slots first.
// kind: only a defun can be a receive function, a lambda's body is held
// to the recv rule (D11), and a task's (a fork's body, or the top level)
// can't wait at all (D243). at: where the stack fault says it happened, which
// a lambda or fork leaves out, since its name already says.
type Kind = 'defun' | 'lambda' | 'task' | 'actor' | 'other';

function compileFunction(entry: string, d: Defun, free: readonly string[], fns: Fns, kind: Kind, st: St,
                         at: Pos | null = d.pos): [Code, St] {
    const locals = [...d.params.map((p) => p.name), ...free];
    const env = locals.reduce<Env>((e, name, i) => ({ name, slot: i, next: e }), null);
    const receive = kind === 'defun' && isReceiveBody(d.body) ? { entry, params: d.params.length } : null;
    const cx: Cx = { fns, env, si: locals.length, tail: true, inLambda: kind === 'lambda', task: kind === 'task', receive };
    const [body, st1] = compileBody(d.body, cx, { ...st, slots: locals.length, calls: false });
    const size = 16 * Math.ceil(st1.slots / 2);
    if (size > 4095) throw new CompileError(`${d.name.name} needs too many frame slots (${st1.slots})`, d.pos);
    const [overflow, st2] = faultLabel(st1, 'RT_FAULT_STACK', displayName(d.name.name), at);
    const [preempt, st3]  = label(st2, 'preempt');
    const [name, st4]     = cString(displayName(d.name.name), st3);
    const t    = st.t;
    const stub = [`${preempt}:`, t.call('rt_preempt', [PROC, FRAME, imm(locals.length), FP, addr(entry)], false), t.jump(`${preempt}_done`)];
    return [[
        '',
        t.functionStart(entry, d.name.name),
        t.prologue(size, overflow, d.params.map((p) => p.name), free, preempt, st1.calls),
        body,
        t.ret,
    ], { ...st4, calls: st.calls, stubs: [st4.stubs, stub], functions: [st4.functions, `    .quad ${entry}, ${name}`] }];
}

// A call: each argument waits in a slot while the rest are computed, then
// they all go to the argument registers. In tail position the caller's
// frame comes down first, and the call is a jump.
function compileCall(x: Pair, fn: Fn, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, fn.name, args, fn.arity);
    if (isPreludeNot(x, cx)) return compileCond(x, [list(args[0]!, sym('#false')), list(sym('#true'), sym('#true'))], cx, st);
    if (fn.state && cx.inLambda) {
        throw new CompileError(`a lambda can't call ${fn.name}, which waits for messages (it reaches a recv)`, x.pos);
    }
    if (fn.state && cx.task) throw new CompileError(taskWaits(fn.name), x.pos);
    if (fn.state && !cx.tail) {
        throw new CompileError(`${fn.name} waits for messages (it reaches a recv), so it can only be called in tail position`, x.pos);
    }
    const [code, ops, st1] = callOperands(args, { ...cx, tail: false }, st);
    return [[code, st.t.call(fn.label, ops, cx.tail)], called(st1, cx)];
}

// A task (a fork's body, or the top level) that would wait: in a recv of
// its own, which split.ts made a function named for it, or by calling a
// function that waits.
function taskWaits(name: string): string {
    const what = name.includes(' (recv at ') ? 'this recv' : `${name}, which waits for messages (it reaches a recv),`;
    return `a task (a fork's body, or the top level) can't wait for messages, and ${what} would: spawn an actor for it`;
}

// A call's arguments, as operands for the target's call: each simple one
// as it is, read when the call is made; the others worked out in order,
// each waiting in a slot from si while the rest are, except the last of
// them, which stays in the accumulator.
function callOperands(args: readonly Sexp[], cx: Cx, st: St): [Code, readonly Operand[], St] {
    const last = args.reduce((at, arg, i) => (isSimple(arg, cx) ? at : i), -1);
    const code: Code[] = [];
    const ops: Operand[] = [];
    let si = cx.si;
    args.forEach((arg, i) => {
        const s = simple(arg, cx, st);
        if (s !== null) {
            ops.push(s[0]);
            st = s[1];
            return;
        }
        const [c, st1] = compileExpr(arg, { ...cx, si }, st);
        if (i === last) {
            code.push(c);
            ops.push(ACC);
            st = st1;
            return;
        }
        code.push(c, st.t.storeSlot(si));
        ops.push(inSlot(si));
        st = useSlot(st1, si);
        si += 1;
    });
    return [code, ops, st];
}

// The assembler label for a function: fn_ and the name, with anything but
// letters and digits spelled out (_ as __, other bytes as _ and two hex
// digits), so that different names never get the same label.
export function functionLabel(name: string): string {
    const isAlnum = (b: number): boolean => (b >= 0x30 && b <= 0x39) || (b >= 0x41 && b <= 0x5a) || (b >= 0x61 && b <= 0x7a);
    const spell   = (b: number): string =>
        isAlnum(b) ? String.fromCharCode(b) : b === 0x5f ? '__' : `_${b.toString(16).padStart(2, '0')}`;
    return `fn_${[...new TextEncoder().encode(name)].map(spell).join('')}`;
}

// --- bodies and expressions -------------------------------------------------

// A body: forms evaluated in order, leaving the last one's value in the
// accumulator.
// (let name expr) binds name for the rest of the body; as the last form,
// its value is expr's (D42). Only the last form can be in tail
// position. A loop over the forms, where slight would tail-recurse (D39),
// so that a long body doesn't run Node out of stack.
function compileBody(forms: Sexp, cx: Cx, st: St): [Code, St] {
    const code: Code[] = [];
    for (; forms.t === 'pair'; forms = forms.cdr) {
        const last = forms.cdr.t === 'nil';
        const here: Cx = { ...cx, tail: cx.tail && last };
        const form = forms.car;
        if (!isForm(form, 'let')) {
            const [c, st1] = compileExpr(form, here, st);
            code.push(c);
            st = st1;
            continue;
        }
        const [name, expr] = checkLet(form as Pair);
        const [c, st1] = compileExpr(expr, here, st);
        if (last) return [[...code, c], st1];
        code.push(c, st.t.storeSlot(cx.si, name.name));
        st = useSlot(st1, cx.si);
        cx = { ...cx, env: { name: name.name, slot: cx.si, next: cx.env }, si: cx.si + 1 };
    }
    return [code, st];
}

function checkLet(form: Pair): [Sym, Sexp] {
    const items = toArray(form) ?? [];
    const [, name, expr] = items;
    if (items.length !== 3 || name === undefined || expr === undefined) {
        throw new CompileError(`let is (let name expr), not ${show(form)}`, form.pos);
    }
    if (name.t !== 'sym') throw new CompileError(`let needs a name, not ${show(name)}`, posOf(name) ?? form.pos);
    checkBindable(name);
    return [name, expr];
}

// Names that can't be bound or defined: #true, #false, $$, ^$$, @ARGV,
// %ENV and the special forms.
function checkBindable(name: Sym): void {
    if (RESERVED_SYMBOLS.includes(name.name) || SPECIAL_FORMS.includes(name.name) || ['$$', '^$$', ...TOP_PARAMS].includes(name.name)) {
        throw new CompileError(`can't bind ${name.name}`, name.pos);
    }
}

function compileExpr(x: Sexp, cx: Cx, st: St): [Code, St] {
    switch (x.t) {
        case 'int':
            return [st.t.loadWord(intWord(x.v), `${x.v}`), st];
        case 'nil':
            return [st.t.loadConst('RT_NIL'), st];
        case 'str':
            return loadString(x.v, st);
        case 'float':
            return loadFloat(x.v, st);
        case 'sym':
            return compileName(x, cx, st);
        case 'pair':
            return compileForm(x, cx, st);
        default:
            throw notYet(x);
    }
}

// Names for constants: control characters, and math/PI, math/E and
// math/INF. A local of the same name hides them.
const CONSTANTS: Readonly<Record<string, Sexp>> = {
    '\\n': str('\n'), '\\r': str('\r'), '\\t': str('\t'), '\\e': str('\x1b'),
    'math/PI': float(Math.PI), 'math/E': float(Math.E), 'math/INF': float(Infinity),
};

function compileName(x: Sym, cx: Cx, st: St): [Code, St] {
    if (x.name === '$$') return [st.t.loadProc('RT_PROC_PID'), st];
    if (x.name === '^$$') return [st.t.loadProc('RT_PROC_PARENT'), st];
    if (x.name === '#true') return [st.t.loadConst('RT_TRUE'), st];
    if (x.name === '#false') return [st.t.loadConst('RT_FALSE'), st];
    const si = lookup(cx.env, x.name);
    if (si !== null) return [st.t.loadSlot(si, x.name), st];
    if (TOP_PARAMS.includes(x.name)) throw new CompileError(`${x.name} is only seen at the top level: pass it to the functions that need it`, x.pos);
    const constant = CONSTANTS[x.name];
    if (constant !== undefined) return compileExpr(constant, cx, st);
    const fn = lookupFn(cx.fns, x.name);
    if (fn !== null && fn.state) throw new CompileError(`${fn.name} waits for messages, so it can't be used as a value`, x.pos);
    if (fn !== null) return loadClosure(staticClosure(fn.label, fn.arity, displayName(fn.name), st));
    if (isBuiltin(x.name)) return loadClosure(builtinWrapper(x, st));
    throw new CompileError(`unknown name '${x.name}'`, x.pos);
}

// A string literal is a box in the read-only data: the header (its length
// in bytes, the string type, and RT_BOX_ASCII if every byte is below 0x80,
// D219), the bytes, and a NUL.
function staticString(s: string, st: St): [string, St] {
    const [name, st1] = label(st, 'string');
    const bytes = new TextEncoder().encode(s);
    const ascii = bytes.every((b) => b < 0x80) ? ' | RT_BOX_ASCII' : '';
    const box = ['    .p2align 4', `${name}:`, `    .quad ${bytes.length} << RT_BOX_SIZE_SHIFT | RT_BOX_STRING${ascii}`, `    .asciz ${asmString(s)}`];
    return [name, { ...st1, data: [st1.data, box] }];
}

function loadString(s: string, st: St): [Code, St] {
    const [name, st1] = staticString(s, st);
    return [st.t.loadTagged(name, 'RT_TAG_BOXED'), st1];
}

// A float literal is a box too: the header, then the double's bits.
function staticFloat(v: number, st: St): [string, St] {
    const [name, st1] = label(st, 'float');
    const bits = new DataView(new Float64Array([v]).buffer).getBigUint64(0, true);
    const box  = ['    .p2align 4', `${name}:`, '    .quad 8 << RT_BOX_SIZE_SHIFT | RT_BOX_FLOAT', `    .quad 0x${bits.toString(16)}    // ${v}`];
    return [name, { ...st1, data: [st1.data, box] }];
}

function loadFloat(v: number, st: St): [Code, St] {
    const [name, st1] = staticFloat(v, st);
    return [st.t.loadTagged(name, 'RT_TAG_BOXED'), st1];
}

function compileForm(x: Pair, cx: Cx, st: St): [Code, St] {
    const head = x.car;
    const args = toArray(x.cdr)!;
    if (head.t === 'pair') return compileClosureCall(x, head, args, cx, st);
    if (head.t !== 'sym') throw new CompileError(`${show(head)} isn't a function`, posOf(head) ?? x.pos);
    switch (head.name) {
        case 'cond':
            return compileCond(x, args, cx, st);
        case 'do':
            if (args.length === 0) throw new CompileError('do needs at least one form', x.pos);
            return compileBody(x.cdr, cx, st);
        case 'let':
            throw new CompileError('let can only be a form of a body (a function, do or cond clause)', x.pos);
        case 'defun':
            throw new CompileError('defun is only allowed at the top level', x.pos);
        case 'quote':
            return compileQuote(x, args, cx, st);
        case 'lambda':
            return compileLambda(x, cx, st);
        case 'recv':
            return compileRecv(x, args, cx, st);
        case 'fork': case 'spawn':
            checkArity(x, head.name, args, 1);
            return compileFork(x, head.name, args[0]!, head.name === 'fork' ? 'rt_fork' : 'rt_spawn', cx, st);
        case 'connect':
            return compileConnect(x, args, cx, st);
        case 'yield': {
            checkArity(x, 'yield', args, 1);
            const [code, st1] = compileExpr(args[0]!, cx, st);
            return [[st.t.call('rt_yield', [], false), code], st1];
        }
        case NO_MATCH: {
            const [code, st1]  = compileExpr(args[0]!, { ...cx, tail: false }, st);
            const [fault, st2] = faultLabel(st1, 'RT_FAULT_NO_MATCH', 'match', x.pos);
            return [[code, st.t.jump(fault)], st2];
        }
        // A tuple pattern's parts (pattern.ts): an element, one load, on a
        // tuple its test has passed; and the test, which compileTest makes
        // a branch, as a boolean here, by the builtins, should it be one.
        case TUPLE_REF: {
            const [code, st1] = compileExpr(args[0]!, { ...cx, tail: false }, st);
            return [[code, st.t.tupleRef(Number((args[1] as Int).v))], st1];
        }
        case TUPLE_OF: {
            const prim = (name: string): Sexp => sym(`${name}${BUILTIN}`, x.pos);
            const [v, n] = args as [Sexp, Sexp];
            return compileExpr(form(x.pos, sym('cond', x.pos),
                list(list(prim('tuple?'), v), list(prim('eq?'), list(prim('tuple/len'), v), n)),
                list(sym('#true'), sym('#false'))), cx, st);
        }
    }
    if (SPECIAL_FORMS.includes(head.name)) throw notYet(x);
    // A builtin by a name no local can hide (pattern.ts), or by its own.
    const known  = head.name.endsWith(ON_CONSES);
    const hidden = known || head.name.endsWith(BUILTIN);
    const name   = hidden ? head.name.slice(0, -(known ? ON_CONSES : BUILTIN).length) : head.name;
    if (!hidden && lookup(cx.env, name) !== null) return compileClosureCall(x, head, args, cx, st);
    if (name === 'apply') return compileApply(x, args, cx, st);
    const operand: Cx = { ...cx, tail: false };
    if (name === 'pprint') {
        checkArity(x, 'pprint', args, 1);
        const [code, st1] = compileExpr(args[0]!, operand, st);
        return [[code, st.t.call('rt_pprint', [ACC], false)], st1];
    }
    if (PREDICATES.includes(name)) {
        checkArity(x, name, args, 1);
        const [code, st1] = compileExpr(args[0]!, operand, st);
        return [[code, st.t.predicate(name)], st1];
    }
    const builtin = C_BUILTINS[name];
    if (builtin !== undefined) return compileCBuiltin(x, name, builtin, args, operand, st);
    if (name === 'eq?' || name === 'ne?') return compileEquality(x, name, args, operand, st);
    if (name === 'cons') return compileCons(x, args, operand, st);
    if (name === 'list') return compileList(x, args, operand, st);
    if (name === 'tuple') return compileTuple(x, args, operand, st);
    if (name === '/') return compileDivide(x, args, operand, st);
    const path = cxrPath(name);
    if (path !== null) return compileCxr(x, name, path, args, operand, st, !known);
    const arith = ARITH[name];
    if (arith !== undefined) {
        const op = name as '+' | '-' | '*';
        const float = (s: St, notFloats: string, done: string, n?: bigint): [Code, St] => floatPath(x, op, notFloats, done, n, s);
        // * has no immediate form, since it untags one side: the literal
        // goes in a register, but still only the other operand is checked
        const withWord = (s: St, w: bigint): [Code, St] => {
            const [overflow, s1] = faultLabel(s, 'RT_FAULT_OVERFLOW', name, x.pos);
            return [op === '*' ? [s.t.accToLeft, s.t.load('acc', word(w)), s.t.arith('*', overflow)] : s.t.arithWord(op, w, overflow), s1];
        };
        return compileNumeric(x, name, args, operand, st, arith, [], (s) => {
            const [overflow, s1] = faultLabel(s, 'RT_FAULT_OVERFLOW', name, x.pos);
            return [s.t.arith(op, overflow), s1];
        }, withWord, float);
    }
    const cmp = COMPARE[name];
    if (cmp !== undefined) {
        const [cond, op] = cmp;
        return compileNumeric(x, name, args, operand, st, 'rt_compare', [imm(op)], (s) => [s.t.compareBool(cond), s]);
    }
    const bit = BITS[name];
    if (bit !== undefined) {
        return compileBinary(x, name, args, operand, st, (s) => {
            if (bit !== 'shl' && bit !== 'shr') return [s.t.bits(bit, ''), s];
            const [range, s1] = faultLabel(s, 'RT_FAULT_RANGE', name, x.pos);
            return [s.t.bits(bit, range), s1];
        });
    }
    if (name === 'bit/not') {
        checkArity(x, name, args, 1);
        const [notInt, st1] = faultLabel(st, 'RT_FAULT_NOT_INT', name, x.pos);
        const [code, st2]   = compileExpr(args[0]!, operand, st1);
        return [[code, st.t.branchUnlessInt(notInt), st.t.bitNot], st2];
    }
    if (DIVISION.includes(name)) {
        const op = name as 'div' | '%';
        return compileBinary(x, name, args, operand, st, (s) => {
            const [zero, s1]     = faultLabel(s, 'RT_FAULT_DIV_ZERO', name, x.pos);
            const [overflow, s2] = faultLabel(s1, 'RT_FAULT_OVERFLOW', name, x.pos);
            return [s.t.divide(op, zero, overflow), s2];
        });
    }
    const fn = lookupFn(cx.fns, name);
    if (fn !== null) return compileCall(x, fn, args, cx, st);
    throw new CompileError(`unknown function '${name}'`, head.pos);
}

// --- processes ----------------------------------------------------------------

// (recv clause...), the whole body of a receive function. rt_recv returns
// the next message; when there's none, it remembers this function and its
// parameters (the first slots of the frame) and gives up the stack, and
// the process starts this function again from the top when one comes. So
// the stack is empty here: nothing has run yet but the prologue. Clause
// bodies are in tail position. A message no clause matches goes to the
// dead letters, and recv takes the next.
function compileRecv(x: Pair, clauses: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (cx.receive === null) throw new CompileError('recv can only be the whole body of a defun', x.pos);
    if (clauses.length === 0) throw new CompileError('recv needs at least one clause', x.pos);
    const { entry, params } = cx.receive;
    const msg = cx.si;
    const [top, st1]  = label(st, 'recv');
    const [site, st2] = siteLabel(st1, 'recv', x.pos);
    const inner: Cx   = { ...cx, si: msg + 1, receive: null };
    const [code, st3] = clauses.reduce<[Code, St]>(([acc, s], clause) => {
        const [c, s1] = compileRecvClause(x, clause, msg, `${top}_done`, inner, s);
        return [[acc, c], s1];
    }, [[], useSlot(st2, msg)]);
    const t = st.t;
    return [[
        `${top}:`,
        t.call('rt_recv', [FRAME, imm(params), addr(entry)], false),
        t.storeSlot(msg),
        code,
        t.call('rt_dead_letter', [inSlot(msg), addr(site)], false),
        t.jump(top),
        `${top}_done:`,
    ], st3];
}

// (pattern body...): the pattern's tests (pattern.ts) on the message,
// which is in slot msg, branching to the next clause when one fails; then
// its bindings, each in a slot of its own, except that a name for the
// whole message is that slot.
function compileRecvClause(x: Pair, clause: Sexp, msg: number, done: string, cx: Cx, st: St): [Code, St] {
    if (clause.t !== 'pair' || clause.cdr.t !== 'pair') {
        throw new CompileError(`a recv clause is (pattern body...), not ${show(clause)}`, posOf(clause) ?? x.pos);
    }
    const message = sym(RECV_MESSAGE, x.pos);
    const { tests, binds } = lowerPattern(clause.car, message);
    const [next, st1] = label(st, 'recv_next');
    const here: Cx = { ...cx, env: { name: RECV_MESSAGE, slot: msg, next: cx.env }, tail: false };
    const [code, st2] = tests.reduce<[Code, St]>(([acc, s], test) => {
        const [c, s1] = compileTest(test, next, here, s);
        return [[acc, c], s1];
    }, [[], st1]);
    let env = cx.env;
    let si  = cx.si;
    let st3 = st2;
    const bound: Code[] = [];
    for (const [name, part] of binds) {
        checkBindable(name);
        if (part === message) {
            env = { name: name.name, slot: msg, next: env };
            continue;
        }
        const [c, s1] = compileExpr(part, { ...here, si }, st3);
        bound.push(c, st.t.storeSlot(si, name.name));
        env = { name: name.name, slot: si, next: env };
        st3 = useSlot(s1, si);
        si += 1;
    }
    const [body, st4] = compileBody(clause.cdr, { ...cx, env, si }, st3);
    return [[code, bound, body, st.t.jump(done), `${next}:`], st4];
}

// (fork expr) and (spawn expr): expr becomes the body of a function of its
// own, whose parameters are the locals it uses; the new process starts
// there, with those values deep-copied into its heap. The body is at the
// bottom of the new process's stack, so in an actor it can tail-call a
// function that waits; a task can't wait (D243). `form` and `fn` say
// which: fork (rt_fork, a task), spawn (rt_spawn, an actor), or connect
// (rt_connect, rt_connect_fs, an actor). `extra` is evaluated first, in the parent, and passed
// after the site, then `flags` (assembler constants).
function compileFork(x: Pair, form: string, expr: Sexp, fn: string, cx: Cx, st: St,
                     extra: readonly Sexp[] = [], flags: readonly string[] = []): [Code, St] {
    const free = freeVars(list(expr), [], cx.env);
    if (free.length > MAX_ARGS) {
        throw new CompileError(`a ${form} can take at most ${MAX_ARGS} locals into the new process, and this one uses ${free.length}`, x.pos);
    }
    const [pre, st0] = extra.reduce<[Code, St]>(([acc, s], e, i) => {
        const [c, s1] = compileExpr(e, { ...cx, si: cx.si + i, tail: false }, s);
        return [[acc, c, s.t.storeSlot(cx.si + i)], useSlot(s1, cx.si + i)];
    }, [[], st]);
    const si = cx.si + extra.length;
    const where = x.pos === null ? form : `${form} at ${x.pos.file}:${x.pos.line}:${x.pos.col}`;
    const [entry, st1] = label(st0, form);
    const d: Defun = { name: sym(where, x.pos), params: free.map((n) => sym(n, x.pos)), body: list(expr), pos: x.pos };
    const [code, st2]  = compileFunction(entry, d, [], cx.fns, form === 'fork' ? 'task' : 'actor', st1, null);
    const st3: St = { ...st2, slots: st0.slots, lambdas: [st2.lambdas, code] };
    const [site, st4]  = siteLabel(st3, form, x.pos);
    const st5 = free.length > 0 ? useSlot(st4, si + free.length - 1) : st4;
    const t = st.t;
    return [[
        pre,
        free.map((name, i) => t.copySlot(lookup(cx.env, name)!, si + i, name)),
        t.call(fn, [addr(entry), imm(free.length), slotAddr(si), addr(site),
                    ...extra.map((_, i) => inSlot(cx.si + i)), ...flags.map(imm)], false),
    ], st5];
}

// connect's sources, past :keypress (every key typed, runtime/tty.c):
// each opens a device, with the runtime's function and its mode, if it
// has one.
const SOURCES: Readonly<Record<string, readonly [string, ...string[]]>> = {
    'fs/read':     ['rt_connect_fs', 'RT_FS_READ'],
    'fs/write':    ['rt_connect_fs', 'RT_FS_WRITE'],
    'fs/append':   ['rt_connect_fs', 'RT_FS_APPEND'],
    'tcp':         ['rt_connect_tcp', 'RT_TCP_CONNECT'],
    'tcp/listen':  ['rt_connect_tcp', 'RT_TCP_LISTEN'],
    'unix':        ['rt_connect_tcp', 'RT_UNIX_CONNECT'],
    'unix/listen': ['rt_connect_tcp', 'RT_UNIX_LISTEN'],
    'exec':        ['rt_connect_exec'],
};

// (connect :keypress expr), (connect :fs/read path expr) and the other
// sources, or (connect dev expr): a fork whose new process is connected
// to the source, or is handed the device dev (an expression; anything
// that isn't a :keyword is one).
function compileConnect(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (args.length === 0) checkArity(x, 'connect', args, 2);
    const source = args[0]!;
    const quoted = isForm(source, 'quote');
    const items  = quoted ? toArray(source) : null;
    const name   = items !== null && items.length === 2 && items[1]!.t === 'sym' ? (items[1] as Sym).name : null;
    if (name === 'keypress' || name === 'system') {
        checkArity(x, 'connect', args, 2);
        return compileFork(x, 'connect', args[1]!, name === 'keypress' ? 'rt_connect' : 'rt_connect_system', cx, st);
    }
    if (name !== null && name in SOURCES) {
        checkArity(x, 'connect', args, 3);
        const [fn, ...mode] = SOURCES[name]!;
        return compileFork(x, 'connect', args[2]!, fn, cx, st, [args[1]!], mode);
    }
    if (quoted) {
        throw new CompileError(`connect's source can be :keypress, :system, :fs/read, :fs/write, :fs/append, :tcp, :tcp/listen, :unix, :unix/listen or :exec, or a device, not ${show(source)}`,
                               posOf(source) ?? x.pos);
    }
    checkArity(x, 'connect', args, 2);
    return compileFork(x, 'connect', args[1]!, 'rt_connect_device', cx, st, [source]);
}

// --- closures -----------------------------------------------------------------

// A closure is a box: the header (how many values it captured, and the
// closure type), the code, the arity, the name (for printing), then the
// captured values.

// A closure that captures nothing, in the constant data: one per function.
function staticClosure(code: string, arity: number, name: string, st: St): [string, St] {
    const closure = `${code}_closure`;
    if (st.closures.includes(closure)) return [closure, st];
    const [nameLabel, st1] = cString(name, st);
    const box = [
        '    .p2align 4',
        `${closure}:`,
        '    .quad 0 << RT_BOX_SIZE_SHIFT | RT_BOX_CLOSURE',
        `    .quad ${code}, ${arity}, ${nameLabel}`,
    ];
    return [closure, { ...st1, consts: [st1.consts, box], closures: [...st1.closures, closure] }];
}

function loadClosure([closure, st]: [string, St]): [Code, St] {
    return [st.t.loadTagged(closure, 'RT_TAG_BOXED'), st];
}

function cString(text: string, st: St): [string, St] {
    const [name, st1] = label(st, 'name');
    return [name, { ...st1, data: [st1.data, `${name}:`, `    .asciz ${asmString(text)}`] }];
}

// (lambda (params...) body...): the body becomes a function of its own,
// and the lambda's value is a closure over it, carrying the values of the
// enclosing locals the body uses. Lambdas capture by value, and can't
// refer to themselves (D11).
function compileLambda(x: Pair, cx: Cx, st: St): [Code, St] {
    const items = toArray(x)!;
    if (items.length < 3) throw new CompileError('lambda is (lambda (params...) body...)', x.pos);
    const params = checkParams('lambda', items[1]!, x, x.pos);
    const body   = list(...items.slice(2));
    const free   = freeVars(body, params.map((p) => p.name), cx.env);
    const where  = x.pos === null ? 'lambda' : `lambda at ${x.pos.file}:${x.pos.line}:${x.pos.col}`;
    const [entry, st1] = label(st, 'lambda');
    const d: Defun = { name: { t: 'sym', name: where, pos: x.pos }, params, body, pos: x.pos };
    const [code, st2]  = compileFunction(entry, d, free, cx.fns, 'lambda', st1, null);
    const st3: St = { ...st2, slots: st.slots, lambdas: [st2.lambdas, code] };
    if (free.length === 0) return loadClosure(staticClosure(entry, params.length, where, st3));

    // Every heap object is a multiple of 16 bytes, as rt_alloc makes them and
    // as copying and the collector take them to be (box_bytes in process.c).
    const [nameLabel, st4] = cString(where, st3);
    const [allocate, st5]  = alloc(16 * Math.ceil((32 + 8 * free.length) / 16), 'lambda', x.pos, st4);
    const captured = free.map((name) => [lookup(cx.env, name)!, name] as const);
    return [[allocate, st.t.makeClosure(entry, params.length, nameLabel, captured)], st5];
}

// The enclosing locals (outer) that a lambda's body uses, in order of
// first use: everything its closure has to carry. bound are the names the
// body binds itself, which hide outer ones.
function freeVars(body: Sexp, bound: readonly string[], outer: Env): readonly string[] {
    const found: string[] = [];
    const use = (name: string, b: readonly string[]): void => {
        if (!b.includes(name) && !found.includes(name) && lookup(outer, name) !== null) found.push(name);
    };
    const scanBody = (forms: Sexp, b: readonly string[]): void => {
        for (; forms.t === 'pair'; forms = forms.cdr) {
            const form = forms.car;
            if (isForm(form, 'let') && form.t === 'pair') {
                const [, name, expr] = toArray(form) ?? [];
                if (expr !== undefined) scan(expr, b);
                if (name !== undefined && name.t === 'sym') b = [...b, name.name];
            } else {
                scan(form, b);
            }
        }
    };
    const scan = (x: Sexp, b: readonly string[]): void => {
        if (x.t === 'sym') return use(x.name, b);
        if (x.t !== 'pair') return;
        const head = x.car;
        if (head.t === 'sym' && head.name === 'quote') return;
        if (head.t === 'sym' && head.name === 'lambda') {
            const params = x.cdr.t === 'pair' ? toArray(x.cdr.car) ?? [] : [];
            const names  = params.filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
            if (x.cdr.t === 'pair') scanBody(x.cdr.cdr, [...b, ...names]);
            return;
        }
        if (head.t === 'sym' && head.name === 'cond') {
            for (let c = x.cdr; c.t === 'pair'; c = c.cdr) {
                if (c.car.t === 'pair') {
                    scan(c.car.car, b);
                    scanBody(c.car.cdr, b);
                }
            }
            return;
        }
        if (head.t === 'sym' && head.name === 'do') return scanBody(x.cdr, b);
        for (let e: Sexp = x; e.t === 'pair'; e = e.cdr) scan(e.car, b);
    };
    scanBody(body, bound);
    return found;
}

// A call through a value: the function (head) waits in slot si and the
// arguments above it. It must be a closure that takes this many
// arguments; its code finds its captured values in it. In tail position
// the frame comes down first, as for any call.
function compileClosureCall(x: Pair, head: Sexp, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (args.length > MAX_ARGS) throw new CompileError(`a call can pass at most ${MAX_ARGS} arguments`, x.pos);
    const [fn, st1] = compileExpr(head, { ...cx, tail: false }, st);
    const [code, st2] = args.reduce<[Code, St]>(([acc, s], arg, i) => {
        const [c, s1] = compileExpr(arg, { ...cx, si: cx.si + 1 + i, tail: false }, s);
        return [[acc, c, s.t.storeSlot(cx.si + 1 + i)], useSlot(s1, cx.si + 1 + i)];
    }, [[], useSlot(st1, cx.si)]);
    const plural = args.length === 1 ? '' : 's';
    const [notFn, st3] = faultLabel(st2, 'RT_FAULT_NOT_FUNC', 'call', x.pos);
    const [arity, st4] = faultLabel(st3, 'RT_FAULT_ARITY', `call with ${args.length} argument${plural}`, x.pos);
    return [[
        fn, st.t.storeSlot(cx.si),
        code,
        st.t.closureCall(cx.si, args.map((_, i) => cx.si + 1 + i), notFn, arity, cx.tail),
    ], called(st4, cx)];
}

// (apply f xs): rt_apply spreads xs into the argument registers and jumps
// to f, so in tail position this is a tail call too.
function compileApply(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, 'apply', args, 2);
    const [f, st1]    = compileExpr(args[0]!, { ...cx, tail: false }, st);
    const [xs, st2]   = compileExpr(args[1]!, { ...cx, si: cx.si + 1, tail: false }, useSlot(st1, cx.si));
    const [site, st3] = siteLabel(st2, 'apply', x.pos);
    return [[
        f, st.t.storeSlot(cx.si),
        xs,
        st.t.call('rt_apply', [inSlot(cx.si), ACC, addr(site)], cx.tail),
    ], called(st3, cx)];
}

// A builtin used as a value, like (map car xs): a small function that
// calls it, compiled the first time it's needed, and its closure.
function builtinWrapper(x: Sym, st: St): [string, St] {
    const arity = builtinArity(x.name);
    if (arity === null) throw new CompileError(`${x.name} can't be used as a value: it takes a varying number of arguments`, x.pos);
    const entry = `bi_${functionLabel(x.name).slice(3)}`;
    if (st.closures.includes(`${entry}_closure`)) return [`${entry}_closure`, st];
    const params = [...Array(arity).keys()].map((i) => sym(`a${i}`, x.pos));
    const d: Defun = { name: x, params, body: list(list(x, ...params)), pos: x.pos };
    const [code, st1] = compileFunction(entry, d, [], null, 'other', st);
    return staticClosure(entry, arity, x.name, { ...st1, slots: st.slots, lambdas: [st1.lambdas, code] });
}

function builtinArity(name: string): number | null {
    if (name in ARITH || name in COMPARE || name in BITS || DIVISION.includes(name) || ['eq?', 'ne?', 'cons', 'apply', '/'].includes(name)) return 2;
    if (PREDICATES.includes(name) || name === 'pprint' || name === 'bit/not' || cxrPath(name) !== null) return 1;
    // with optional arguments, as a value it takes just the ones it needs (D219)
    const c = C_BUILTINS[name];
    return c !== undefined && !c.variadic ? c.min : null;
}

// A constant. Symbols are their compile-time ids; integers and () quote
// to themselves; a quoted list is static data, shared by every process and
// never copied or collected. Strings and floats need boxes (step 5).
function compileQuote(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, 'quote', args, 1);
    const datum = args[0]!;
    if (datum.t === 'int' || datum.t === 'nil' || datum.t === 'str' || datum.t === 'float') return compileExpr(datum, cx, st);
    if (datum.t === 'pair') {
        const [name, st1] = staticList(x, datum, st);
        return [st.t.loadTagged(name, 'RT_TAG_LIST'), st1];
    }
    if (datum.t !== 'sym') throw notYet(x);
    const [id, st1] = symbolId(st, datum.name);
    return [st.t.loadWord(symbolWord(id), `'${datum.name}`), st1];
}

// A quoted list's cells, side by side in the constant data, each one's
// cdr pointing at the next; nested lists first, so their words are known.
// Returns the label of the first cell.
function staticList(x: Pair, list: Pair, st: St): [string, St] {
    if (isForm(list, TUPLE)) throw new CompileError(`a tuple pattern can't be quoted: #(...) is only a pattern, not ${show(list)}`, posOf(list) ?? x.pos);
    const [words, st1] = toArray(list)!.reduce<[readonly string[], St]>(([ws, s], item) => {
        const [w, s1] = staticWord(x, item, s);
        return [[...ws, w], s1];
    }, [[], st]);
    const [name, st2] = label(st1, 'quoted');
    const cells = words.map((w, i) => `    .quad ${w}, ${i + 1 < words.length ? `${name}+${16 * (i + 1) + RT_TAG_LIST}` : 'RT_NIL'}`);
    return [name, { ...st2, consts: [st2.consts, '    .p2align 4', `${name}:`, cells] }];
}

function staticWord(x: Pair, item: Sexp, st: St): [string, St] {
    switch (item.t) {
        case 'int':
            return [`0x${BigInt.asUintN(64, intWord(item.v)).toString(16)}`, st];
        case 'nil':
            return ['RT_NIL', st];
        case 'str': {
            const [name, st1] = staticString(item.v, st);
            return [`${name}+RT_TAG_BOXED`, st1];
        }
        case 'float': {
            const [name, st1] = staticFloat(item.v, st);
            return [`${name}+RT_TAG_BOXED`, st1];
        }
        case 'sym': {
            const [id, st1] = symbolId(st, item.name);
            return [`0x${symbolWord(id).toString(16)}`, st1];
        }
        case 'pair': {
            const [name, st1] = staticList(x, item, st);
            return [`${name}+${RT_TAG_LIST}`, st1];
        }
        default:
            throw notYet(x);
    }
}

function symbolId(st: St, name: string): [number, St] {
    for (let s = st.symbols; s !== null; s = s.next) if (s.name === name) return [s.id, st];
    const id = st.symbols === null ? 0 : st.symbols.id + 1;
    return [id, { ...st, symbols: { name, id, next: st.symbols } }];
}

// --- primitives ---------------------------------------------------------------

// + - * on any two numbers: inline for two integers (the target's arith),
// anything else is the runtime's function.
const ARITH: Readonly<Record<string, string>> = { '+': 'rt_add', '-': 'rt_sub', '*': 'rt_mul' };

// Comparisons on any two numbers: the condition for two integers (tagging
// keeps the order, so the words compare like the integers), and what to
// ask rt_compare otherwise.
const COMPARE: Readonly<Record<string, readonly [Cond, string]>> = {
    '==': ['eq', 'RT_CMP_EQ'], '!=': ['ne', 'RT_CMP_NE'],
    '<':  ['lt', 'RT_CMP_LT'], '<=': ['le', 'RT_CMP_LE'],
    '>':  ['gt', 'RT_CMP_GT'], '>=': ['ge', 'RT_CMP_GE'],
};

// div and %: integers only, truncating toward zero (D68).
const DIVISION: readonly string[] = ['div', '%'];

// The bit operations on two integers, inline (D221); bit/not is one's, and
// bit/count is in C.
const BITS: Readonly<Record<string, BitOp>> = {
    'bit/and': 'and', 'bit/or': 'or', 'bit/xor': 'xor', 'bit/shl': 'shl', 'bit/shr': 'shr',
};

const RT_TAG_LIST = 1;

// Type predicates, inline (the target's predicate). #true and #false are
// symbols too, so sym? says yes to them.
const PREDICATES: readonly string[] = ['int?', 'nil?', 'cons?', 'pid?', 'sym?', 'bool?'];

// Builtins written in C (runtime/strings.c, tuples.c, json.c, ...): the C
// function, and how many arguments it takes. Missing optional arguments
// are passed as (). The call's site goes in the register after the
// arguments, for the faults the C code raises. A variadic builtin gets its
// arguments as one list.
type CBuiltin = { readonly fn: string; readonly min: number; readonly max: number; readonly variadic: boolean };

const fixed    = (fn: string, min: number, max = min): CBuiltin => ({ fn, min, max, variadic: false });
const variadic = (fn: string): CBuiltin => ({ fn, min: 0, max: 1, variadic: true });

const C_BUILTINS: Readonly<Record<string, CBuiltin>> = {
    'str?':               fixed('rt_is_str', 1),
    'str':                variadic('rt_str'),
    '~':                  variadic('rt_cat'),
    'str/len':            fixed('rt_str_len', 1),
    'str/at':             fixed('rt_str_at', 2),
    'str/code-at':        fixed('rt_str_code_at', 2),
    'str/slice':          fixed('rt_str_slice', 2, 3),
    'str/index-of':       fixed('rt_str_index_of', 2, 3),
    'str/last-index-of':  fixed('rt_str_last_index_of', 2, 3),
    'str/includes?':      fixed('rt_str_includes', 2),
    'str/starts-with?':   fixed('rt_str_starts_with', 2),
    'str/ends-with?':     fixed('rt_str_ends_with', 2),
    'str/split':          fixed('rt_str_split', 2, 3),
    'str/join':           fixed('rt_str_join', 2),
    'str/repeat':         fixed('rt_str_repeat', 2),
    'str/pad-start':      fixed('rt_str_pad_start', 2, 3),
    'str/pad-end':        fixed('rt_str_pad_end', 2, 3),
    'str/trim':           fixed('rt_str_trim', 1),
    'str/trim-start':     fixed('rt_str_trim_start', 1),
    'str/trim-end':       fixed('rt_str_trim_end', 1),
    'str/upper':          fixed('rt_str_upper', 1),
    'str/lower':          fixed('rt_str_lower', 1),
    'str/replace':        fixed('rt_str_replace', 3),
    'str/replace-all':    fixed('rt_str_replace_all', 3),
    'str/compare':        fixed('rt_str_compare', 2),
    'str/<':              fixed('rt_str_less', 2),
    'str/chars':          fixed('rt_str_chars', 1),
    'str/codes':          fixed('rt_str_codes', 1),
    'str/well-formed?':   fixed('rt_str_well_formed', 1),
    'str/to-well-formed': fixed('rt_str_to_well_formed', 1),
    'codes->str':         fixed('rt_codes_to_str', 1),
    'str->int':           fixed('rt_str_to_int', 1, 2),
    'int->str':           fixed('rt_int_to_str', 1, 2),
    'str->float':         fixed('rt_str_to_float', 1),
    'str->sym':           fixed('rt_str_to_sym', 1),
    'sym->str':           fixed('rt_sym_to_str', 1),
    'str->bytes':         fixed('rt_str_to_bytes', 1),
    'bytes->str':         fixed('rt_bytes_to_str', 1),
    'bytes/len':          fixed('rt_bytes_len', 1),
    'bytes/at':           fixed('rt_bytes_at', 2),
    'bytes/slice':        fixed('rt_bytes_slice', 2, 3),
    'bytes/index-of':     fixed('rt_bytes_index_of', 2, 3),
    'tty/write':          variadic('rt_tty_write'),
    'json/parse':         fixed('rt_json_parse', 1),
    'json/print':         fixed('rt_json_print', 1),
    'sexp/parse':         fixed('rt_sexp_parse', 1),
    'sexp/print':         fixed('rt_sexp_print', 1),
    'tuple?':             fixed('rt_is_tuple_p', 1),
    'tuple/len':          fixed('rt_tuple_len_of', 1),
    'tuple/get':          fixed('rt_tuple_ref', 2),
    'tuple/set':          fixed('rt_tuple_set', 3),
    'tuple/make':         fixed('rt_tuple_make', 2),
    'tuple/insert':       fixed('rt_tuple_insert', 3),
    'tuple/remove':       fixed('rt_tuple_remove', 2),
    'tuple/slice':        fixed('rt_tuple_slice', 3),
    'list->tuple':        fixed('rt_tuple', 1),
    'tuple->list':        fixed('rt_tuple_to_list', 1),
    'float?':             fixed('rt_is_flt', 1),
    'num?':               fixed('rt_is_num', 1),
    'math/ceil':          fixed('rt_ceil', 1),
    'math/floor':         fixed('rt_floor', 1),
    'math/round':         fixed('rt_round', 1),
    'math/trunc':         fixed('rt_trunc', 1),
    'math/abs':           fixed('rt_abs', 1),
    'math/min':           fixed('rt_min', 2),
    'math/max':           fixed('rt_max', 2),
    'math/pow':           fixed('rt_pow', 2),
    'math/sqrt':          fixed('rt_sqrt', 1),
    'math/sin':           fixed('rt_sin', 1),
    'math/cos':           fixed('rt_cos', 1),
    'math/tan':           fixed('rt_tan', 1),
    'math/exp':           fixed('rt_exp', 1),
    'math/asin':          fixed('rt_asin', 1),
    'math/acos':          fixed('rt_acos', 1),
    'math/atan':          fixed('rt_atan', 1),
    'math/sinh':          fixed('rt_sinh', 1),
    'math/cosh':          fixed('rt_cosh', 1),
    'math/tanh':          fixed('rt_tanh', 1),
    'math/asinh':         fixed('rt_asinh', 1),
    'math/acosh':         fixed('rt_acosh', 1),
    'math/atanh':         fixed('rt_atanh', 1),
    'math/log':           fixed('rt_log', 1),
    'math/log2':          fixed('rt_log2', 1),
    'math/log10':         fixed('rt_log10', 1),
    'math/log1p':         fixed('rt_log1p', 1),
    'math/expm1':         fixed('rt_expm1', 1),
    'math/cbrt':          fixed('rt_cbrt', 1),
    'math/atan2':         fixed('rt_atan2', 2),
    'math/hypot':         fixed('rt_hypot', 2),
    'math/mod':           fixed('rt_mod', 2),
    'math/nan?':          fixed('rt_is_nan', 1),
    'int->float':         fixed('rt_int_to_float', 1),
    'float->str':         fixed('rt_float_to_str', 1, 2),
    'bit/count':          fixed('rt_bit_count', 1),
    'rand/int':           fixed('rt_rand_int', 1),
    'rand/float':         fixed('rt_rand_float', 0),
    'rand/seed':          fixed('rt_rand_seed', 1),
    'lambda?':            fixed('rt_is_lambda', 1),
    'send':               fixed('rt_send', 2),
    'join':               fixed('rt_join', 1),
    'join/race':          fixed('rt_join_race', 1),
    'address':            fixed('rt_address', 0),
    'read':               fixed('rt_read', 1),
    'read/any':           fixed('rt_read_any', 1),
    'open':               fixed('rt_open', 1, 2),
    'monitor':            fixed('rt_monitor', 1),
    'kill':               fixed('rt_kill', 1),
    'raise':              fixed('rt_raise', 1),
    'after':              fixed('rt_after', 3),
    'timer/cancel':       fixed('rt_timer_cancel', 1),
    'disconnect':         fixed('rt_disconnect', 1),
    'socket/peer':        fixed('rt_socket_peer', 1),
    'sleep':              fixed('rt_sleep', 1),
    'clock/us':           fixed('rt_clock_us', 0),
    'clock/now':          fixed('rt_clock_now', 0),
    'fs/list':            fixed('rt_fs_list', 1),
    'fs/stat':            fixed('rt_fs_stat', 1),
    'fs/delete':          fixed('rt_fs_delete', 1),
    'fs/rename':          fixed('rt_fs_rename', 2),
    'fs/mkdir':           fixed('rt_fs_mkdir', 1),
    'bytes/pack':         fixed('rt_bytes_pack', 2),
    'bytes/unpack':       fixed('rt_bytes_unpack', 3),
    'log':                fixed('rt_log_line', 3),
    'hash':               fixed('rt_hash', 1),
    'tty/screen/rows':    fixed('rt_screen_rows', 0),
    'tty/screen/cols':    fixed('rt_screen_cols', 0),
};

// The names a defun can't take.
const BUILTINS: readonly string[] = [
    'pprint', 'eq?', 'ne?', 'cons', 'list', 'tuple', 'apply', '/',
    ...DIVISION, ...PREDICATES, 'bit/not',
    ...[ARITH, COMPARE, BITS, C_BUILTINS].flatMap((table) => Object.keys(table)),
];

const isBuiltin = (name: string): boolean => BUILTINS.includes(name) || cxrPath(name) !== null;

// Arithmetic and comparisons: the left operand into LEFT and the right
// into the accumulator (operands). Two integers (both tag bits clear) take
// the inline path; anything else goes out of line to the runtime's
// fn(left, right, ...extra, site), which promotes to float or faults "not a
// number". With withWord, a right operand that's an integer literal is a
// word: only the left needs checking, and withWord makes the inline path.
// With float, the out-of-line path first tries floats (floatPath) before
// the runtime: float(st, notFloats, done, n) gets n when the right operand
// is the integer literal n.
function compileNumeric(x: Pair, name: string, args: readonly Sexp[], cx: Cx, st: St,
                        fn: string, extra: readonly Operand[], inline: (st: St) => [Code, St],
                        withWord?: (st: St, w: bigint) => [Code, St],
                        float?: (st: St, notFloats: string, done: string, n?: bigint) => [Code, St]): [Code, St] {
    checkArity(x, name, args, 2);
    const t = st.t;
    const [l, r] = args as [Sexp, Sexp];
    if (withWord !== undefined && r.t === 'int') {
        const w = intWord(r.v);
        const [left, st1] = compileExpr(l, cx, st);
        const [slow, st2] = label(st1, 'slow');
        const [site, st3] = siteLabel(st2, name, x.pos);
        const [fast, st4] = withWord(st3, w);
        const [floats, st5] = float === undefined ? [[], st4] : float(st4, `${slow}_c`, `${slow}_done`, r.v);
        const stub = [`${slow}:`, floats, `${slow}_c:`, t.call(fn, [ACC, word(w), ...extra, addr(site)], false), t.jump(`${slow}_done`)];
        return [[left, t.branchUnlessInt(slow), fast, `${slow}_done:`], { ...st5, stubs: [st5.stubs, stub] }];
    }
    const [both, st1] = operands(l, r, cx, st);
    const [slow, st2] = label(st1, 'slow');
    const [site, st3] = siteLabel(st2, name, x.pos);
    const [fast, st4] = inline(st3);
    const [floats, st5] = float === undefined ? [[], st4] : float(st4, `${slow}_c`, `${slow}_done`);
    const stub = [`${slow}:`, floats, `${slow}_c:`, t.call(fn, [LEFT, ACC, ...extra, addr(site)], false), t.jump(`${slow}_done`)];
    return [[both, t.branchUnlessInts(slow), fast, `${slow}_done:`], { ...st5, stubs: [st5.stubs, stub] }];
}

// Floats, inline (D198): unless the operands are floats (the left and the
// accumulator, or, given n, the accumulator, n being the right operand's
// integer), on to notFloats; then a box allocated, the operation done, and
// on to done. The result is the double C would have made, to the bit: the
// same IEEE operation, on the same doubles.
function floatPath(x: Pair, op: FloatOp, notFloats: string, done: string, n: bigint | undefined, st: St): [Code, St] {
    const t = st.t;
    const [allocate, st1] = alloc(16, op, x.pos, st);
    return [[
        n === undefined ? t.branchUnlessFloats(notFloats) : t.branchUnlessFloat(notFloats),
        allocate,
        n === undefined ? t.floatArith(op) : t.floatArithInt(op, n),
        t.jump(done),
    ], st1];
}

// (/ a b), always a float: two floats, or a float and a literal integer
// that isn't 0, inline (D198), the divisor checked for zero first; anything
// else, a zero included, is rt_divide's, which faults on it.
function compileDivide(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, '/', args, 2);
    const t = st.t;
    const [l, r] = args as [Sexp, Sexp];
    const [slow, st1] = label(st, 'slow');
    const [site, st2] = siteLabel(st1, '/', x.pos);
    const literal = r.t === 'int' && r.v !== 0n;
    const [first, st3] = literal ? compileExpr(l, cx, st2) : operands(l, r, cx, st2);
    const [allocate, st4] = alloc(16, '/', x.pos, st3);
    const stub = [`${slow}:`, t.call('rt_divide', literal ? [ACC, word(intWord((r as Int).v)), addr(site)] : [LEFT, ACC, addr(site)], false),
                  t.jump(`${slow}_done`)];
    return [[
        first,
        literal ? t.branchUnlessFloat(slow) : [t.branchUnlessFloats(slow), t.branchIfZeroFloat(slow)],
        allocate,
        literal ? t.floatArithInt('/', (r as Int).v) : t.floatArith('/'),
        `${slow}_done:`,
    ], { ...st4, stubs: [st4.stubs, stub] }];
}

// Whether x needs no code to work out (simple, below), without anything
// simple would add to St.
function isSimple(x: Sexp, cx: Cx): boolean {
    if (x.t === 'int' || x.t === 'nil') return true;
    if (x.t === 'sym') return x.name === '#true' || x.name === '#false' || (x.name !== '$$' && x.name !== '^$$' && lookup(cx.env, x.name) !== null);
    return isForm(x, 'quote') && isImmediateLiteral(x);
}

// An operand that needs no code to work out, so it can be read where it's
// wanted when it's wanted: a local, in its slot (which nothing changes);
// an integer, (), #true or #false, or a quoted symbol, as its word. Not
// $$ or ^$$, which are loads from the process; and not a name that isn't
// a local (a constant, a function).
function simple(x: Sexp, cx: Cx, st: St): [Operand, St] | null {
    if (!isSimple(x, cx)) return null;
    if (x.t === 'int') return [word(intWord(x.v)), st];
    if (x.t === 'nil') return [imm('RT_NIL'), st];
    if (x.t === 'sym' && x.name === '#true') return [imm('RT_TRUE'), st];
    if (x.t === 'sym' && x.name === '#false') return [imm('RT_FALSE'), st];
    if (x.t === 'sym') return [inSlot(lookup(cx.env, x.name)!), st];
    const [w, st1] = immediateWord(x, st);
    return [word(w), st1];
}

// A binary operation's operands: the left into LEFT and the right into the
// accumulator, the left worked out first. A simple one is read when it's
// wanted, so it needn't wait in a slot; otherwise the left waits in slot
// si while the right is worked out.
function operands(left: Sexp, right: Sexp, cx: Cx, st: St): [Code, St] {
    const t = st.t;
    const l = simple(left, cx, st);
    if (l !== null) {
        const [rc, st1] = compileExpr(right, cx, l[1]);
        return [[rc, t.load('left', l[0])], st1];
    }
    const [lc, st1] = compileExpr(left, cx, st);
    const r = simple(right, cx, st1);
    if (r !== null) return [[lc, t.accToLeft, t.load('acc', r[0])], r[1]];
    const [rc, st2] = compileExpr(right, { ...cx, si: cx.si + 1 }, useSlot(st1, cx.si));
    return [[lc, t.storeSlot(cx.si), rc, t.loadLeft(cx.si)], st2];
}

// The left operand, then the right, each checked as it comes when ints
// says both must be integers (so a left that isn't faults before the
// right is worked out); then op combines them (left and the accumulator).
// A simple right operand is read when it's wanted; a left one only
// without ints, since reading it later would check it after the right.
function compileBinary(x: Pair, name: string, args: readonly Sexp[], cx: Cx, st: St,
                       op: (st: St) => [Code, St], ints = true): [Code, St] {
    checkArity(x, name, args, 2);
    const [notInt, st1]  = ints ? faultLabel(st, 'RT_FAULT_NOT_INT', name, x.pos) : ['', st];
    const t     = st.t;
    const isInt = ints ? t.branchUnlessInt(notInt) : [];
    const [l, r] = args as [Sexp, Sexp];
    const rs = simple(r, cx, st1);
    if (!ints || rs !== null) {
        if (!ints && rs === null) {
            const [both, st2]    = operands(l, r, cx, st1);
            const [combine, st3] = op(st2);
            return [[both, combine], st3];
        }
        const [left, st2]    = compileExpr(l, cx, rs![1]);
        const [combine, st3] = op(st2);
        return [[left, isInt, t.accToLeft, t.load('acc', rs![0]), isInt, combine], st3];
    }
    const [left, st2]    = compileExpr(l, cx, st1);
    const [right, st3]   = compileExpr(r, { ...cx, si: cx.si + 1 }, useSlot(st2, cx.si));
    const [combine, st4] = op(st3);
    return [[
        left, isInt, t.storeSlot(cx.si),
        right, isInt, t.loadLeft(cx.si),
        combine,
    ], st4];
}

// eq? and ne?. The same word is equal. Otherwise two lists can still be
// equal, which rt_equal works out; but when either side is a literal
// immediate (an integer, a symbol, ()), the words decide.
function compileEquality(x: Pair, name: string, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    const t      = st.t;
    const negate = name === 'ne?' ? t.notBool : [];
    if (args.some(isImmediateLiteral)) {
        return compileBinary(x, name, args, cx, st, (s) => [[t.compareBool('eq'), negate], s], false);
    }
    return compileBinary(x, name, args, cx, st, (s) => {
        const [same, s1] = label(s, 'same');
        const [done, s2] = label(s1, 'equal_done');
        return [[
            t.branchIfSame(same),
            t.call('rt_equal', [ACC, LEFT], false),
            t.jump(done),
            `${same}:`,
            t.loadConst('RT_TRUE'),
            `${done}:`,
            negate,
        ], s2];
    }, false);
}

function isImmediateLiteral(x: Sexp): boolean {
    if (x.t === 'int' || x.t === 'nil') return true;
    if (x.t === 'sym') return RESERVED_SYMBOLS.includes(x.name);
    if (!isForm(x, 'quote') || x.t !== 'pair' || x.cdr.t !== 'pair') return false;
    const datum = x.cdr.car;
    return datum.t === 'sym' || datum.t === 'int' || datum.t === 'nil';
}

function compileCBuiltin(x: Pair, name: string, b: CBuiltin, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (b.variadic) {
        const [list, st1] = compileList(x, args, cx, st);
        const [site, st2] = siteLabel(st1, name, x.pos);
        return [[list, st.t.call(b.fn, [ACC, addr(site)], false)], st2];
    }
    if (args.length < b.min || args.length > b.max) {
        const n = b.min === b.max ? `${b.min}` : `${b.min} or ${b.max}`;
        throw new CompileError(`${name} takes ${n} argument${n === '1' ? '' : 's'}, not ${args.length}`, x.pos);
    }
    const [code, ops, st1] = callOperands(args, cx, st);
    const [site, st2] = siteLabel(st1, name, x.pos);
    const missing = [...Array(b.max - args.length).keys()].map(() => imm('RT_NIL'));
    return [[code, st.t.call(b.fn, [...ops, ...missing, addr(site)], false)], st2];
}

function checkArity(x: Pair, name: string, args: readonly Sexp[], n: number): void {
    if (args.length !== n) {
        throw new CompileError(`${name} takes ${n} argument${n === 1 ? '' : 's'}, not ${args.length}`, x.pos);
    }
}

// --- lists ------------------------------------------------------------------

// Allocates `bytes` from the process's heap (the target's allocate), for
// the shape that follows to fill. When the chunk hasn't room, an
// out-of-line call to rt_heap_grow makes a new one (or faults, past the
// heap's limit) and the allocation goes again.
function alloc(bytes: number, what: string, pos: Pos | null, st: St): [Code, St] {
    const [again, st1] = label(st, 'alloc');
    const [site, st2]  = siteLabel(st1, what, pos);
    const [code, stub] = st.t.allocate(bytes, again, site);
    return [code, { ...st2, stubs: [st2.stubs, stub] }];
}

// (cons x xs): xs must be a list, so every list stays proper.
function compileCons(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, 'cons', args, 2);
    const [head, st1]    = compileExpr(args[0]!, cx, st);
    const [tail, st2]    = compileExpr(args[1]!, { ...cx, si: cx.si + 1 }, useSlot(st1, cx.si));
    const [notList, st3]  = faultLabel(st2, 'RT_FAULT_NOT_LIST', 'cons', x.pos);
    const [allocate, st4] = alloc(16, 'cons', x.pos, st3);
    const t = st.t;
    return [[
        head, t.storeSlot(cx.si),
        tail,
        t.branchUnlessList(notList),
        allocate,
        t.consCell(cx.si),
    ], st4];
}

// Each of args in turn into the slots from si, each waiting there while
// the rest are computed.
function intoSlots(args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    return args.reduce<[Code, St]>(([acc, s], arg, i) => {
        const [c, s1] = compileExpr(arg, { ...cx, si: cx.si + i }, s);
        return [[acc, c, s.t.storeSlot(cx.si + i)], useSlot(s1, cx.si + i)];
    }, [[], st]);
}

// (list a b ...): the elements wait in slots, then one allocation holds
// all the cells, side by side.
function compileList(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (args.length === 0) return [st.t.loadConst('RT_NIL'), st];
    const [code, st1] = intoSlots(args, cx, st);
    const [allocate, st2] = alloc(16 * args.length, 'list', x.pos, st1);
    return [[code, allocate, st.t.listCells(cx.si, args.length)], st2];
}

// (tuple a b ...): the elements wait in slots, then one allocation holds
// the header and the elements, as tuples.c lays a tuple out, rounded up to
// 16 bytes as every heap object is. Not through a list and rt_tuple, which
// made a list to throw away (D195).
function compileTuple(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    const [code, st1] = intoSlots(args, cx, st);
    const [allocate, st2] = alloc(16 * Math.ceil((8 + 8 * args.length) / 16), 'tuple', x.pos, st1);
    return [[code, allocate, st.t.tupleCells(tupleHeader(args.length), cx.si, args.length)], st2];
}

// car, cdr, and c[ad]{2,4}r: the letters between c and r, applied right
// to left. Each step needs a cons. Returns null for any other name.
function cxrPath(name: string): readonly string[] | null {
    const middle = name.slice(1, -1);
    const isCxr = name.length >= 3 && name.length <= 6 && name.startsWith('c') && name.endsWith('r')
        && [...middle].every((ch) => ch === 'a' || ch === 'd');
    return isCxr ? [...middle].reverse() : null;
}

// checked: unless each step is known to be on a cons (pattern.ts's paths).
function compileCxr(x: Pair, name: string, path: readonly string[], args: readonly Sexp[], cx: Cx, st: St,
                    checked = true): [Code, St] {
    checkArity(x, name, args, 1);
    const [code, st1] = compileExpr(args[0]!, cx, st);
    if (!checked) return [[code, path.map((step) => st.t.cxr(step as 'a' | 'd'))], st1];
    const [notCons, st2] = faultLabel(st1, 'RT_FAULT_NOT_CONS', name, x.pos);
    const steps = path.map((step) => [st.t.branchUnlessCons(notCons), st.t.cxr(step as 'a' | 'd')]);
    return [[code, steps], st2];
}

// --- cond -------------------------------------------------------------------

// Each test must be #true or #false; anything else faults, and so does
// running out of clauses. A test that is literally #true needs no check.
// The clause bodies are in tail position if the cond is.
// When the last test is #true, as an if's is, nothing runs out of clauses,
// and the last body runs on into the end.
function compileCond(x: Pair, clauses: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (clauses.length === 0) throw new CompileError('cond needs at least one clause', x.pos);
    const lastClause = clauses[clauses.length - 1]!;
    const always     = lastClause.t === 'pair' && isSym(lastClause.car, '#true');
    const [end, st1]     = label(st, 'cond_end');
    const [noMatch, st2] = always ? ['', st1] : faultLabel(st1, 'RT_FAULT_NO_CLAUSE', 'cond', x.pos);
    const [code, st3]    = clauses.reduce<[Code, St]>(([acc, s], clause, i) => {
        const [c, s1] = compileClause(x, clause, end, cx, s, always && i === clauses.length - 1);
        return [[acc, c], s1];
    }, [[], st2]);
    return [[code, always ? [] : st.t.jump(noMatch), `${end}:`], st3];
}

function compileClause(x: Pair, clause: Sexp, end: string, cx: Cx, st: St, last = false): [Code, St] {
    if (clause.t !== 'pair' || clause.cdr.t !== 'pair') {
        throw new CompileError(`a cond clause is (test body...), not ${show(clause)}`, posOf(clause) ?? x.pos);
    }
    if (last) return compileBody(clause.cdr, cx, st);
    const [next, st1] = label(st, 'cond_next');
    const [code, st2] = compileTest(clause.car, next, { ...cx, tail: false }, st1, posOf(clause.car) ?? clause.pos);
    const [body, st3] = compileBody(clause.cdr, cx, st2);
    return [[code, body, st.t.jump(end), `${next}:`], st3];
}

// A test, for where it branches: on to the code after it when it's #true,
// and to ifFalse when it's #false. Anything else faults (:not-a-bool), but
// some tests can only give a boolean, and branch without making one:
// #true itself; cons?, nil?, and eq? with an immediate literal (an
// integer, a symbol, ()), the shapes of pattern.ts's tests; and what and
// becomes, (cond (a b) (#true #false)), which is a then b. site is where
// a test that isn't a boolean is said to be: for b there, the and's.
function compileTest(test: Sexp, ifFalse: string, cx: Cx, st: St, site: Pos | null = posOf(test)): [Code, St] {
    if (test.t === 'sym' && test.name === '#true') return [[], st];
    if (isPreludeNot(test, cx)) {
        const [passed, st1] = label(st, 'not');
        const [code, st2]   = compileTest(toArray(test)![1]!, passed, cx, st1);
        return [[code, st.t.jump(ifFalse), `${passed}:`], st2];
    }
    const items = test.t === 'pair' ? toArray(test) : null;
    const head  = items !== null && items[0]!.t === 'sym' ? builtinName(items[0] as Sym, cx) : null;
    if ((head === 'cons?' || head === 'nil?') && items!.length === 2) {
        const [code, st1] = compileExpr(items![1]!, cx, st);
        return [[code, head === 'cons?' ? st.t.branchUnlessCons(ifFalse) : st.t.branchUnlessNil(ifFalse)], st1];
    }
    if (head === 'eq?' && items!.length === 3 && items!.slice(1).some(isImmediateLiteral)) {
        const [a, b]       = items!.slice(1) as [Sexp, Sexp];
        const [lit, other] = isImmediateLiteral(b) ? [b, a] : [a, b];
        const [word, st1]  = immediateWord(lit, st);
        const [code, st2]  = compileExpr(other, cx, st1);
        return [[code, st.t.branchUnlessIs(word, ifFalse)], st2];
    }
    if (head !== null && Object.hasOwn(COMPARE, head) && items!.length === 3) return compileCompareTest(head, items!.slice(1), ifFalse, cx, st, posOf(test));
    if (isForm(test, TUPLE_OF) && items!.length === 3) {
        const [code, st1] = compileExpr(items![1]!, cx, st);
        return [[code, st.t.branchUnlessBox(tupleHeader(Number((items![2] as Int).v)), ifFalse)], st1];
    }
    if (isForm(test, 'cond') && items!.length === 3) {
        const [first, second] = [toArray(items![1]!), toArray(items![2]!)];
        const isAnd = first !== null && first.length === 2 && second !== null && second.length === 2
            && isSym(second[0]!, '#true') && isSym(second[1]!, '#false');
        if (isAnd) {
            const [a, st1] = compileTest(first[0]!, ifFalse, cx, st);
            const [b, st2] = compileTest(first[1]!, ifFalse, cx, st1, site);
            return [[a, b], st2];
        }
    }
    const [code, st1]    = compileExpr(test, cx, st);
    const [notBool, st2] = faultLabel(st1, 'RT_FAULT_NOT_BOOL', 'cond test', site);
    return [[code, st.t.testBool(ifFalse, notBool)], st2];
}

// The comparison that holds with its operands the other way round.
const MIRROR: Readonly<Record<string, string>> = { '==': '==', '!=': '!=', '<': '>', '<=': '>=', '>': '<', '>=': '<=' };

// A comparison as a test: for two integers, a compare and a branch to
// ifFalse unless it holds, with no boolean made; anything else asks
// rt_compare out of line, which gives one (or faults), and branches on it.
// An integer literal is a word, on the right: (< 0 x) is (> x 0).
function compileCompareTest(name: string, args: readonly Sexp[], ifFalse: string, cx: Cx, st: St, pos: Pos | null): [Code, St] {
    const [a, b]  = args as [Sexp, Sexp];
    const swap    = a.t === 'int' && b.t !== 'int';
    const op      = swap ? MIRROR[name]! : name;
    const [cond, rtOp] = COMPARE[op]!;
    const [l, r]  = swap ? [b, a] : [a, b];
    const t = st.t;
    const [slow, st1] = label(st, 'slow');
    const [site, st2] = siteLabel(st1, name, pos);
    const done = [t.branchIfFalse(ifFalse), t.jump(`${slow}_done`)];
    if (r.t === 'int') {
        const w = intWord(r.v);
        const [left, st3] = compileExpr(l, cx, st2);
        const stub = [`${slow}:`, t.call('rt_compare', [ACC, word(w), imm(rtOp), addr(site)], false), done];
        return [[left, t.branchUnlessInt(slow), t.branchUnlessCompareWord(cond, w, ifFalse), `${slow}_done:`],
                { ...st3, stubs: [st3.stubs, stub] }];
    }
    const [both, st3] = operands(l, r, cx, st2);
    const stub = [`${slow}:`, t.call('rt_compare', [LEFT, ACC, imm(rtOp), addr(site)], false), done];
    return [[both, t.branchUnlessInts(slow), t.branchUnlessCompare(cond, ifFalse), `${slow}_done:`],
            { ...st3, stubs: [st3.stubs, stub] }];
}

// A call to the prelude's not, with its one argument, which compiles inline
// (D203): as a value, the cond the prelude says it is; as a test, its
// argument's, with the branches the other way round. Not a program's own
// not (D80), nor a local called not.
function isPreludeNot(x: Sexp, cx: Cx): boolean {
    const head = isForm(x, `not${PRELUDE_SUFFIX}`) ? `not${PRELUDE_SUFFIX}` : 'not';
    if (!isForm(x, head) || lookup(cx.env, head) !== null || toArray(x)?.length !== 2) return false;
    const fn = lookupFn(cx.fns, head);
    return fn !== null && fn.module === 'prelude';
}

// The builtin a call's head names, if it names one: by a name no local can
// hide, or by its own when no local hides it.
function builtinName(head: Sym, cx: Cx): string | null {
    if (head.name.endsWith(BUILTIN)) return head.name.slice(0, -BUILTIN.length);
    return lookup(cx.env, head.name) === null && isBuiltin(head.name) ? head.name : null;
}

const isSym = (x: Sexp, name: string): boolean => x.t === 'sym' && x.name === name;

// The word of an immediate literal (isImmediateLiteral).
function immediateWord(x: Sexp, st: St): [bigint, St] {
    const datum = isForm(x, 'quote') && x.t === 'pair' && x.cdr.t === 'pair' ? x.cdr.car : x;
    if (datum.t === 'int') return [intWord(datum.v), st];
    if (datum.t === 'nil') return [BigInt(RT_TAG_LIST), st];
    const [id, st1] = symbolId(st, (datum as Sym).name);
    return [symbolWord(id), st1];
}

// --- helpers ------------------------------------------------------------------

const useSlot = (st: St, si: number): St => (si < st.slots ? st : { ...st, slots: si + 1 });

// After a call to compiled code: one not in tail position comes back to
// this frame.
const called = (st: St, cx: Cx): St => (cx.tail || st.calls ? st : { ...st, calls: true });

function lookup(env: Env, name: string): number | null {
    for (; env !== null; env = env.next) if (env.name === name) return env.slot;
    return null;
}

// A library's code names the prelude's f as `f (prelude)` (modules.ts,
// D248), so the program's own f doesn't take its place.
function lookupFn(fns: Fns, name: string): Fn | null {
    if (name.endsWith(PRELUDE_SUFFIX)) return findFn(fns, name.slice(0, -PRELUDE_SUFFIX.length), 'prelude');
    for (; fns !== null; fns = fns.next) if (fns.fn.name === name) return fns.fn;
    return null;
}

function label(st: St, hint: string): [string, St] {
    return [`L${hint}_${st.labels}`, { ...st, labels: st.labels + 1 }];
}

type Fault = 'RT_FAULT_NOT_INT' | 'RT_FAULT_OVERFLOW' | 'RT_FAULT_NOT_BOOL' | 'RT_FAULT_NO_CLAUSE' | 'RT_FAULT_STACK' | 'RT_FAULT_NO_MATCH'
           | 'RT_FAULT_NOT_CONS' | 'RT_FAULT_NOT_LIST' | 'RT_FAULT_HEAP' | 'RT_FAULT_DIV_ZERO'
           | 'RT_FAULT_NOT_FUNC' | 'RT_FAULT_ARITY' | 'RT_FAULT_RANGE';

// A label to branch to when `what`, at `pos`, goes wrong: an out-of-line
// call to rt_fault, with the offending value (if any) in the accumulator.
function faultLabel(st: St, fault: Fault, what: string, pos: Pos | null): [string, St] {
    const [name, st1] = label(st, 'fault');
    const [site, st2] = siteLabel(st1, what, pos);
    const stub = [`${name}:`, st.t.call('rt_fault', [imm(fault), ACC, addr(site)], false)];
    return [name, { ...st2, stubs: [st2.stubs, stub] }];
}

// A string saying what is happening where, like "+ at t/x.slight:2:1",
// for the runtime's fault messages.
function siteLabel(st: St, what: string, pos: Pos | null): [string, St] {
    const [name, st1] = label(st, 'site');
    const text = pos === null ? what : `${what} at ${pos.file}:${pos.line}:${pos.col}`;
    return [name, { ...st1, data: [st1.data, `${name}:`, `    .asciz ${asmString(text)}`] }];
}

const notYet = (x: Sexp): CompileError => new CompileError(`not supported yet: ${show(x)}`, posOf(x));

// A string as an assembler literal: printable ASCII as is, everything else
// (and " and \) as octal escapes of its UTF-8 bytes.
export function asmString(s: string): string {
    const bytes = [...new TextEncoder().encode(s)];
    const esc   = (b: number): string =>
        b >= 0x20 && b < 0x7f && b !== 0x22 && b !== 0x5c ? String.fromCharCode(b) : `\\${b.toString(8).padStart(3, '0')}`;
    return `"${bytes.map(esc).join('')}"`;
}

function flatten(code: Code): string[] {
    const out: string[] = [];
    const todo: Code[] = [code];
    while (todo.length > 0) {
        const c = todo.pop()!;
        if (typeof c === 'string') out.push(c);
        else for (let i = c.length - 1; i >= 0; i--) todo.push(c[i]!);
    }
    return out;
}
