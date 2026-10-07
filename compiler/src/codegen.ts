// Code generation: s-expressions to assembly text, for a target (target.ts:
// AArch64 in aarch64.ts, x86-64 in x86_64.ts).
//
// Ghuloum-style: every expression leaves its value in the accumulator, and
// values that have to wait (arguments, a binary op's left operand, a let's
// value) go to slots in the function's frame. No register allocation, and
// nothing lives in a register across a call.
//
// Functions take up to eight arguments in registers and return their result
// in the accumulator, as the platform's C calling convention does, so
// compiled functions and the runtime's C functions are called the same way.
// A call in tail position takes down the caller's frame and jumps, so loops
// run in constant stack.
//
// The output is a .S file that includes the target's header in runtime/
// (asm_aarch64.h, asm_x86_64.h), so the differences between Mach-O and ELF stay
// there, and rt.h's constants (RT_TRUE, RT_FAULT_OVERFLOW, ...) can be used
// by name.
//
// So far (docs/PLAN.md, steps 1-7): integers, floats, #true/#false, (),
// symbols, lists (cons cells bump-allocated in the process's heap, or
// static data when quoted), strings, closures, pids, the numeric, list and
// string primitives, eq?/ne?, type predicates, cond, let, do, pprint,
// defun, lambda, apply, calls, and processes: fork, send, recv, yield, $$
// and ^$$, with the recv rule (classify.ts) enforced here. Floats, strings and closures are boxes;
// literals, and closures that capture nothing, are static data. The
// top-level forms that aren't defuns are the body of slight_main, whose one
// parameter is @ARGV, the program's arguments: so only they can see it
// (D144).
//
// The prelude (lib/prelude.slight) is compiled with every program. Its
// functions are in their own namespace: a program can define a function
// with a prelude name, and then its code uses its own definition while the
// prelude keeps using the prelude's (D80).

import { CompileError } from './errors.ts';
import { float, list, NIL, posOf, show, str, sym, toArray, type Pair, type Pos, type Sexp, type Sym } from './sexp.ts';
import { intWord, RESERVED_SYMBOLS, RUNTIME_SYMBOLS, symbolWord } from './values.ts';
import { isReceiveBody, patternNames, stateFunctions } from './classify.ts';
import { AARCH64 } from './aarch64.ts';
import { ACC, addr, FRAME, imm, inSlot, LEFT, PROC, slotAddr, type Code, type Cond, type Operand, type Target } from './target.ts';

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
    readonly receive: { readonly entry: string; readonly params: number } | null;
};

// The symbols seen so far, newest first: an association list from name to
// id. Ids are handed out in order, starting with the reserved symbols.
type Syms = { readonly name: string; readonly id: number; readonly next: Syms } | null;

// What compiling accumulates: how many labels have been made, how many
// frame slots the current function needs, the symbols, the out-of-line
// code (fault calls, preemption) to emit after all the functions, and the
// read-only data: strings (data) and quoted lists (consts). It also carries
// the target, which never changes.
type St = {
    readonly t: Target;
    readonly labels: number;
    readonly slots: number;
    readonly symbols: Syms;
    readonly stubs: Code;
    readonly data: Code;
    readonly consts: Code;
    readonly lambdas: Code;                 // functions compiled along the way: lambdas, builtin wrappers
    readonly closures: readonly string[];   // functions whose static closure has been emitted
};

const MAX_ARGS = 8;     // in registers, on every target

// The special forms, and those the expander turns into others (expand.ts),
// which it leaves none of: neither kind can be a name.
const SPECIAL_FORMS: readonly string[] = [
    'defun', 'lambda', 'let', 'cond', 'do', 'quote', 'fork', 'connect', 'recv', 'yield',
    'if', 'when', 'case', 'and', 'or', '@include',
];

// --- the program ------------------------------------------------------------

export function compileProgram(forms: Sexp, prelude: Sexp = NIL, t: Target = AARCH64): string {
    const preludeForms = toArray(prelude)!;
    preludeForms.forEach((f) => {
        if (!isForm(f, 'defun')) throw new CompileError(`the prelude can only define functions, not ${show(f)}`, posOf(f));
    });
    const preludeDefuns = preludeForms.map((f) => checkDefun(f as Pair));
    const preludeFns    = preludeDefuns.reduce<Fns>((fns, d) => declare(fns, d, 'prelude', false), null);

    const all    = toArray(forms)!;
    const defuns = all.filter((f) => isForm(f, 'defun')).map((f) => checkDefun(f as Pair));
    const state  = stateFunctions(defuns.map((d) => ({ name: d.name.name, params: d.params.map((p) => p.name), body: d.body })));
    const fns    = defuns.reduce<Fns>((acc, d) => declare(acc, d, 'user', state.includes(d.name.name)), preludeFns);
    const top    = all.filter((f) => !isForm(f, 'defun'));
    const empty: St = { t, labels: 0, slots: 0, symbols: null, stubs: [], data: [], consts: [], lambdas: [], closures: [] };
    const st0 = [...RESERVED_SYMBOLS, ...RUNTIME_SYMBOLS].reduce((st, name) => symbolId(st, name)[1], empty);

    const compileAll = (ds: readonly Defun[], visible: Fns, module: Module, st: St): [Code, St] =>
        ds.reduce<[Code, St]>(([acc, s], d) => {
            const [c, s1] = compileFunction(findFn(visible, d.name.name, module)!.label, d, [], visible, 'defun', s);
            return [[acc, c], s1];
        }, [[], st]);
    const [preludeCode, st1] = compileAll(preludeDefuns, preludeFns, 'prelude', st0);
    const [code, st2]        = compileAll(defuns, fns, 'user', st1);
    const argv: Sym   = { t: 'sym', name: '@ARGV', pos: null };
    const main: Defun = { name: { t: 'sym', name: 'the top level', pos: null }, params: [argv], body: list(...top, ...(top.length === 0 ? [NIL] : [])), pos: null };
    const [mainCode, st3] = compileFunction('slight_main', main, [], fns, 'other', st2);

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
    ]).join('\n') + '\n';
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
// are this process's reductions used up?
// kind: only a defun can be a receive function, and a lambda's body is held
// to the recv rule (D11).
type Kind = 'defun' | 'lambda' | 'other';

function compileFunction(entry: string, d: Defun, free: readonly string[], fns: Fns, kind: Kind, st: St): [Code, St] {
    const locals = [...d.params.map((p) => p.name), ...free];
    const env = locals.reduce<Env>((e, name, i) => ({ name, slot: i, next: e }), null);
    const receive = kind === 'defun' && isReceiveBody(d.body) ? { entry, params: d.params.length } : null;
    const cx: Cx = { fns, env, si: locals.length, tail: true, inLambda: kind === 'lambda', receive };
    const [body, st1] = compileBody(d.body, cx, { ...st, slots: locals.length });
    const size = 16 * Math.ceil(st1.slots / 2);
    if (size > 4095) throw new CompileError(`${d.name.name} needs too many frame slots (${st1.slots})`, d.pos);
    const [overflow, st2] = faultLabel(st1, 'RT_FAULT_STACK', d.name.name, d.pos);
    const [preempt, st3]  = label(st2, 'preempt');
    const t    = st.t;
    const stub = [`${preempt}:`, t.call('rt_preempt', [PROC], false), t.jump(`${preempt}_done`)];
    return [[
        '',
        t.functionStart(entry, d.name.name),
        t.prologue(size, overflow, d.params.map((p) => p.name), free, preempt),
        body,
        t.ret,
    ], { ...st3, stubs: [st3.stubs, stub] }];
}

// A call: each argument waits in a slot while the rest are computed, then
// they all go to the argument registers. In tail position the caller's
// frame comes down first, and the call is a jump.
function compileCall(x: Pair, fn: Fn, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, fn.name, args, fn.arity);
    if (fn.state && cx.inLambda) {
        throw new CompileError(`a lambda can't call ${fn.name}, which waits for messages (it reaches a recv)`, x.pos);
    }
    if (fn.state && !cx.tail) {
        throw new CompileError(`${fn.name} waits for messages (it reaches a recv), so it can only be called in tail position`, x.pos);
    }
    const [code, st1] = args.reduce<[Code, St]>(([acc, s], arg, i) => {
        const [c, s1] = compileExpr(arg, { ...cx, si: cx.si + i, tail: false }, s);
        return [[acc, c, s.t.storeSlot(cx.si + i)], useSlot(s1, cx.si + i)];
    }, [[], st]);
    return [[code, st.t.call(fn.label, args.map((_, i) => inSlot(cx.si + i)), cx.tail)], st1];
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
// its value is expr's, as in ts-slight. Only the last form can be in tail
// position.
function compileBody(forms: Sexp, cx: Cx, st: St): [Code, St] {
    if (forms.t !== 'pair') return [[], st];
    const last = forms.cdr.t === 'nil';
    const here: Cx = { ...cx, tail: cx.tail && last };
    const form = forms.car;
    if (isForm(form, 'let')) {
        const [name, expr] = checkLet(form as Pair);
        const [code, st1] = compileExpr(expr, here, st);
        if (last) return [code, st1];
        const inner: Cx = { ...cx, env: { name: name.name, slot: cx.si, next: cx.env }, si: cx.si + 1 };
        const [rest, st2] = compileBody(forms.cdr, inner, useSlot(st1, cx.si));
        return [[code, st.t.storeSlot(cx.si, name.name), rest], st2];
    }
    const [code, st1] = compileExpr(form, here, st);
    const [rest, st2] = compileBody(forms.cdr, cx, st1);
    return [[code, rest], st2];
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

// Names that can't be bound or defined: #true, #false, $$, ^$$, @ARGV and
// the special forms.
function checkBindable(name: Sym): void {
    if (RESERVED_SYMBOLS.includes(name.name) || SPECIAL_FORMS.includes(name.name) || ['$$', '^$$', '@ARGV'].includes(name.name)) {
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

// Names for constants, from ts-slight: control characters, and PI.
const CONSTANTS: Readonly<Record<string, Sexp>> = {
    '\\n': str('\n'), '\\r': str('\r'), '\\t': str('\t'), '\\e': str('\x1b'), 'PI': float(Math.PI),
};

function compileName(x: Sym, cx: Cx, st: St): [Code, St] {
    if (x.name === '$$') return [st.t.loadProc('RT_PROC_PID'), st];
    if (x.name === '^$$') return [st.t.loadProc('RT_PROC_PARENT'), st];
    if (x.name === '#true') return [st.t.loadConst('RT_TRUE'), st];
    if (x.name === '#false') return [st.t.loadConst('RT_FALSE'), st];
    const si = lookup(cx.env, x.name);
    if (si !== null) return [st.t.loadSlot(si, x.name), st];
    if (x.name === '@ARGV') throw new CompileError('@ARGV is only seen at the top level: pass it to the functions that need it', x.pos);
    const constant = CONSTANTS[x.name];
    if (constant !== undefined) return compileExpr(constant, cx, st);
    const fn = lookupFn(cx.fns, x.name);
    if (fn !== null && fn.state) throw new CompileError(`${fn.name} waits for messages, so it can't be used as a value`, x.pos);
    if (fn !== null) return loadClosure(staticClosure(fn.label, fn.arity, fn.name, st));
    if (isBuiltin(x.name)) return loadClosure(builtinWrapper(x, st));
    throw new CompileError(`unknown name '${x.name}'`, x.pos);
}

// A string literal is a box in the read-only data: the header (its length
// in bytes and the string type), the bytes, and a NUL.
function staticString(s: string, st: St): [string, St] {
    const [name, st1] = label(st, 'string');
    const length = new TextEncoder().encode(s).length;
    const box = ['    .p2align 4', `${name}:`, `    .quad ${length} << RT_BOX_SIZE_SHIFT | RT_BOX_STRING`, `    .asciz ${asmString(s)}`];
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
        case 'fork':
            checkArity(x, 'fork', args, 1);
            return compileFork(x, 'fork', args[0]!, 'rt_fork', cx, st);
        case 'connect':
            return compileConnect(x, args, cx, st);
        case 'yield': {
            checkArity(x, 'yield', args, 1);
            const [code, st1] = compileExpr(args[0]!, cx, st);
            return [[st.t.call('rt_yield', [], false), code], st1];
        }
    }
    if (SPECIAL_FORMS.includes(head.name)) throw notYet(x);
    if (lookup(cx.env, head.name) !== null) return compileClosureCall(x, head, args, cx, st);
    if (head.name === 'apply') return compileApply(x, args, cx, st);
    const operand: Cx = { ...cx, tail: false };
    if (head.name === 'pprint') {
        checkArity(x, 'pprint', args, 1);
        const [code, st1] = compileExpr(args[0]!, operand, st);
        return [[code, st.t.call('rt_pprint', [ACC], false)], st1];
    }
    if (PREDICATES.includes(head.name)) {
        checkArity(x, head.name, args, 1);
        const [code, st1] = compileExpr(args[0]!, operand, st);
        return [[code, st.t.predicate(head.name)], st1];
    }
    const builtin = C_BUILTINS[head.name];
    if (builtin !== undefined) return compileCBuiltin(x, head.name, builtin, args, operand, st);
    if (head.name === 'eq?' || head.name === 'ne?') return compileEquality(x, head.name, args, operand, st);
    if (head.name === 'cons') return compileCons(x, args, operand, st);
    if (head.name === 'list') return compileList(x, args, operand, st);
    const path = cxrPath(head.name);
    if (path !== null) return compileCxr(x, head.name, path, args, operand, st);
    const arith = ARITH[head.name];
    if (arith !== undefined) {
        const op = head.name as '+' | '-' | '*';
        return compileNumeric(x, head.name, args, operand, st, arith, [], (s) => {
            const [overflow, s1] = faultLabel(s, 'RT_FAULT_OVERFLOW', head.name, x.pos);
            return [s.t.arith(op, overflow), s1];
        });
    }
    const cmp = COMPARE[head.name];
    if (cmp !== undefined) {
        const [cond, op] = cmp;
        return compileNumeric(x, head.name, args, operand, st, 'rt_compare', [imm(op)], (s) => [s.t.compareBool(cond), s]);
    }
    if (DIVISION.includes(head.name)) {
        const op = head.name as 'div' | '%';
        return compileBinary(x, head.name, args, operand, st, (s) => {
            const [zero, s1]     = faultLabel(s, 'RT_FAULT_DIV_ZERO', head.name, x.pos);
            const [overflow, s2] = faultLabel(s1, 'RT_FAULT_OVERFLOW', head.name, x.pos);
            return [s.t.divide(op, zero, overflow), s2];
        });
    }
    const fn = lookupFn(cx.fns, head.name);
    if (fn !== null) return compileCall(x, fn, args, cx, st);
    throw new CompileError(`unknown function '${head.name}'`, head.pos);
}

const isForm = (x: Sexp, name: string): boolean => x.t === 'pair' && x.car.t === 'sym' && x.car.name === name;

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

// (pattern body...). A pattern is a name (binds the whole message; _
// binds nothing), a :keyword (matches that symbol), or (:keyword names...),
// which matches a list of exactly that length headed by the keyword, and
// binds the names to the rest by position.
function compileRecvClause(x: Pair, clause: Sexp, msg: number, done: string, cx: Cx, st: St): [Code, St] {
    if (clause.t !== 'pair' || clause.cdr.t !== 'pair') {
        throw new CompileError(`a recv clause is (pattern body...), not ${show(clause)}`, posOf(clause) ?? x.pos);
    }
    const pattern = clause.car;
    const bad = (): CompileError =>
        new CompileError(`a recv pattern is a name, a :keyword or (:keyword names...), not ${show(pattern)}`, posOf(pattern) ?? x.pos);
    const names = patternNames(pattern);
    names.forEach((n, i) => {
        if (names.indexOf(n) !== i) throw new CompileError(`${n} is in the pattern twice`, posOf(pattern));
    });
    const [next, st1] = label(st, 'recv_next');
    const t = st.t;
    const keyword = (p: Sexp, s: St): [Code, St] => {
        const items = toArray(p);
        if (!isForm(p, 'quote') || items === null || items.length !== 2 || items[1]!.t !== 'sym') throw bad();
        const [id, s1] = symbolId(s, (items[1] as Sym).name);
        return [t.branchUnlessLeftIs(symbolWord(id), next), s1];
    };

    let test: Code = [];
    let st2 = st1;
    let env = cx.env;
    let si  = cx.si;
    if (pattern.t === 'sym') {
        checkBindable(pattern);
        if (pattern.name !== '_') env = { name: pattern.name, slot: msg, next: env };
    } else if (isForm(pattern, 'quote')) {
        const [match, s] = keyword(pattern, st2);
        test = [t.loadLeft(msg), match];
        st2  = s;
    } else {
        const items = toArray(pattern);
        if (items === null || items.length === 0) throw bad();
        const [match, s] = keyword(items[0]!, st2);
        st2 = s;
        const rest = items.slice(1).map((p) => {
            if (p.t !== 'sym') throw bad();
            checkBindable(p);
            const bind = p.name === '_' ? [] : t.storeCar(si, p.name);
            if (p.name !== '_') {
                env = { name: p.name, slot: si, next: env };
                st2 = useSlot(st2, si);
                si += 1;
            }
            return [t.accFromCdr, t.branchUnlessCons(next), bind];
        });
        test = [
            t.loadSlot(msg),
            t.branchUnlessCons(next),
            t.leftFromCar,
            match,
            rest,
            t.accFromCdr,
            t.branchUnlessNil(next),
        ];
    }
    const [body, st3] = compileBody(clause.cdr, { ...cx, env, si }, st2);
    return [[test, body, t.jump(done), `${next}:`], st3];
}

// (fork expr): expr becomes the body of a function of its own, whose
// parameters are the locals it uses; the new process starts there, with
// those values deep-copied into its heap. The body is at the bottom of the
// new process's stack, so it can tail-call a function that waits. `form`
// and `fn` say which: fork (rt_fork), or connect (rt_connect,
// rt_connect_fs). `extra` is evaluated first, in the parent, and passed
// after the site, then `flags` (assembler constants).
function compileFork(x: Pair, form: string, expr: Sexp, fn: string, cx: Cx, st: St,
                     extra: readonly Sexp[] = [], flags: readonly string[] = []): [Code, St] {
    const free = freeVars(list(expr), [], cx.env);
    if (free.length > MAX_ARGS) {
        throw new CompileError(`a ${form} can take at most ${MAX_ARGS} locals into the new process, and this one uses ${free.length}`, x.pos);
    }
    const [pre, st0] = extra.reduce<[Code, St]>(([acc, s], e, i) => {
        const [c, s1] = compileExpr(e, { ...cx, si: cx.si + i }, s);
        return [[acc, c, s.t.storeSlot(cx.si + i)], useSlot(s1, cx.si + i)];
    }, [[], st]);
    const si = cx.si + extra.length;
    const where = x.pos === null ? form : `${form} at ${x.pos.file}:${x.pos.line}:${x.pos.col}`;
    const [entry, st1] = label(st0, form);
    const d: Defun = { name: sym(where, x.pos), params: free.map((n) => sym(n, x.pos)), body: list(expr), pos: x.pos };
    const [code, st2]  = compileFunction(entry, d, [], cx.fns, 'other', st1);
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
// each opens a device, with the runtime's function and its mode.
const SOURCES: Readonly<Record<string, readonly [string, string]>> = {
    'fs/read':    ['rt_connect_fs', 'RT_FS_READ'],
    'fs/write':   ['rt_connect_fs', 'RT_FS_WRITE'],
    'fs/append':  ['rt_connect_fs', 'RT_FS_APPEND'],
    'tcp':        ['rt_connect_tcp', 'RT_TCP_CONNECT'],
    'tcp/listen': ['rt_connect_tcp', 'RT_TCP_LISTEN'],
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
    if (name === 'keypress') {
        checkArity(x, 'connect', args, 2);
        return compileFork(x, 'connect', args[1]!, 'rt_connect', cx, st);
    }
    if (name !== null && name in SOURCES) {
        checkArity(x, 'connect', args, 3);
        const [fn, mode] = SOURCES[name]!;
        return compileFork(x, 'connect', args[2]!, fn, cx, st, [args[1]!], [mode]);
    }
    if (quoted) {
        throw new CompileError(`connect's source can be :keypress, :fs/read, :fs/write, :fs/append, :tcp or :tcp/listen, or a device, not ${show(source)}`,
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
    const [code, st2]  = compileFunction(entry, d, free, cx.fns, 'lambda', st1);
    const st3: St = { ...st2, slots: st.slots, lambdas: [st2.lambdas, code] };
    if (free.length === 0) return loadClosure(staticClosure(entry, params.length, where, st3));

    const [nameLabel, st4] = cString(where, st3);
    const [allocate, st5]  = alloc(32 + 8 * free.length, 'lambda', x.pos, st4);
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
    ], st4];
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
    ], st3];
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
    if (name in ARITH || name in COMPARE || DIVISION.includes(name) || ['eq?', 'ne?', 'cons', 'apply'].includes(name)) return 2;
    if (PREDICATES.includes(name) || name === 'pprint' || cxrPath(name) !== null) return 1;
    const c = C_BUILTINS[name];
    return c !== undefined && !c.variadic && c.min === c.max ? c.min : null;
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

const RT_TAG_LIST = 1;

// Type predicates, inline (the target's predicate). #true and #false are
// symbols too, so sym? says yes to them.
const PREDICATES: readonly string[] = ['int?', 'nil?', 'cons?', 'pid?', 'sym?', 'bool?'];

// Builtins written in C (runtime/strings.c): the C function, and how many
// arguments it takes. Missing optional arguments are passed as (). The
// call's site goes in the register after the arguments, for the faults
// the C code raises. A variadic builtin gets its arguments as one list.
type CBuiltin = { readonly fn: string; readonly min: number; readonly max: number; readonly variadic: boolean };

const fixed    = (fn: string, min: number, max = min): CBuiltin => ({ fn, min, max, variadic: false });
const variadic = (fn: string): CBuiltin => ({ fn, min: 0, max: 1, variadic: true });

const C_BUILTINS: Readonly<Record<string, CBuiltin>> = {
    'str?':           fixed('rt_is_str', 1),
    'str-len':        fixed('rt_str_len', 1),
    'substring':      fixed('rt_substring', 3),
    'concat':         variadic('rt_concat'),
    '~':              fixed('rt_concat2', 2),
    'index-of':       fixed('rt_index_of', 2),
    'str-split':      fixed('rt_str_split', 2),
    'str-join':       fixed('rt_str_join', 2),
    'string->int':    fixed('rt_string_to_int', 1),
    'symbol->string': fixed('rt_symbol_to_string', 1),
    'string->symbol': fixed('rt_string_to_symbol', 1),
    'byte-at':        fixed('rt_byte_at', 2),
    'bytes->string':  fixed('rt_bytes_to_string', 1),
    'format-num':     fixed('rt_format_num', 2, 3),
    'tty/write':      variadic('rt_tty_write'),
    '/':              fixed('rt_divide', 2),
    'float?':         fixed('rt_is_flt', 1),
    'num?':           fixed('rt_is_num', 1),
    'ceil':           fixed('rt_ceil', 1),
    'floor':          fixed('rt_floor', 1),
    'round':          fixed('rt_round', 1),
    'trunc':          fixed('rt_trunc', 1),
    'abs':            fixed('rt_abs', 1),
    'min':            fixed('rt_min', 2),
    'max':            fixed('rt_max', 2),
    'pow':            fixed('rt_pow', 2),
    'sqrt':           fixed('rt_sqrt', 1),
    'sin':            fixed('rt_sin', 1),
    'cos':            fixed('rt_cos', 1),
    'tan':            fixed('rt_tan', 1),
    'exp':            fixed('rt_exp', 1),
    'lambda?':        fixed('rt_is_lambda', 1),
    'send':           fixed('rt_send', 2),
    'join':           fixed('rt_join', 1),
    'monitor':        fixed('rt_monitor', 1),
    'kill':           fixed('rt_kill', 1),
    'raise':          fixed('rt_raise', 1),
    'after':          fixed('rt_after', 3),
    'disconnect':     fixed('rt_disconnect', 1),
    'sleep':          fixed('rt_sleep', 1),
    'tty/screen/rows': fixed('rt_screen_rows', 0),
    'tty/screen/cols': fixed('rt_screen_cols', 0),
};

// The names a defun can't take.
const BUILTINS: readonly string[] = [
    'pprint', 'eq?', 'ne?', 'cons', 'list', 'apply',
    ...DIVISION, ...PREDICATES,
    ...[ARITH, COMPARE, C_BUILTINS].flatMap((table) => Object.keys(table)),
];

const isBuiltin = (name: string): boolean => BUILTINS.includes(name) || cxrPath(name) !== null;

// Arithmetic and comparisons. The left operand waits in slot si while the
// right one is computed. Two integers (both tag bits clear) take the inline
// path; anything else goes out of line to the runtime's fn(left, right,
// ...extra, site), which promotes to float or faults "not a number".
function compileNumeric(x: Pair, name: string, args: readonly Sexp[], cx: Cx, st: St,
                        fn: string, extra: readonly Operand[], inline: (st: St) => [Code, St]): [Code, St] {
    checkArity(x, name, args, 2);
    const [left, st1]  = compileExpr(args[0]!, cx, st);
    const [right, st2] = compileExpr(args[1]!, { ...cx, si: cx.si + 1 }, useSlot(st1, cx.si));
    const [slow, st3]  = label(st2, 'slow');
    const [site, st4]  = siteLabel(st3, name, x.pos);
    const [fast, st5]  = inline(st4);
    const t    = st.t;
    const stub = [`${slow}:`, t.call(fn, [LEFT, ACC, ...extra, addr(site)], false), t.jump(`${slow}_done`)];
    return [[
        left, t.storeSlot(cx.si),
        right, t.loadLeft(cx.si),
        t.branchUnlessInts(slow),
        fast,
        `${slow}_done:`,
    ], { ...st5, stubs: [st5.stubs, stub] }];
}

// The left operand waits in slot si while the right one is computed; then
// op combines them (left and the accumulator). With ints, both must be
// integers.
function compileBinary(x: Pair, name: string, args: readonly Sexp[], cx: Cx, st: St,
                       op: (st: St) => [Code, St], ints = true): [Code, St] {
    checkArity(x, name, args, 2);
    const [left, st1]    = compileExpr(args[0]!, cx, st);
    const [right, st2]   = compileExpr(args[1]!, { ...cx, si: cx.si + 1 }, useSlot(st1, cx.si));
    const [notInt, st3]  = ints ? faultLabel(st2, 'RT_FAULT_NOT_INT', name, x.pos) : ['', st2];
    const [combine, st4] = op(st3);
    const t     = st.t;
    const isInt = ints ? t.branchUnlessInt(notInt) : [];
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
    const [code, st1] = args.reduce<[Code, St]>(([acc, s], arg, i) => {
        const [c, s1] = compileExpr(arg, { ...cx, si: cx.si + i }, s);
        return [[acc, c, s.t.storeSlot(cx.si + i)], useSlot(s1, cx.si + i)];
    }, [[], st]);
    const [site, st2] = siteLabel(st1, name, x.pos);
    const operands = [...Array(b.max).keys()].map((i) => (i < args.length ? inSlot(cx.si + i) : imm('RT_NIL')));
    return [[code, st.t.call(b.fn, [...operands, addr(site)], false)], st2];
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

// (list a b ...): the elements wait in slots, then one allocation holds
// all the cells, side by side.
function compileList(x: Pair, args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (args.length === 0) return [st.t.loadConst('RT_NIL'), st];
    const [code, st1] = args.reduce<[Code, St]>(([acc, s], arg, i) => {
        const [c, s1] = compileExpr(arg, { ...cx, si: cx.si + i }, s);
        return [[acc, c, s.t.storeSlot(cx.si + i)], useSlot(s1, cx.si + i)];
    }, [[], st]);
    const [allocate, st2] = alloc(16 * args.length, 'list', x.pos, st1);
    return [[code, allocate, st.t.listCells(cx.si, args.length)], st2];
}

// car, cdr, and c[ad]{2,4}r: the letters between c and r, applied right
// to left. Each step needs a cons. Returns null for any other name.
function cxrPath(name: string): readonly string[] | null {
    const middle = name.slice(1, -1);
    const isCxr = name.length >= 3 && name.length <= 6 && name.startsWith('c') && name.endsWith('r')
        && [...middle].every((ch) => ch === 'a' || ch === 'd');
    return isCxr ? [...middle].reverse() : null;
}

function compileCxr(x: Pair, name: string, path: readonly string[], args: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    checkArity(x, name, args, 1);
    const [code, st1]    = compileExpr(args[0]!, cx, st);
    const [notCons, st2] = faultLabel(st1, 'RT_FAULT_NOT_CONS', name, x.pos);
    const steps = path.map((step) => [st.t.branchUnlessCons(notCons), st.t.cxr(step as 'a' | 'd')]);
    return [[code, steps], st2];
}

// --- cond -------------------------------------------------------------------

// Each test must be #true or #false; anything else faults, and so does
// running out of clauses. A test that is literally #true needs no check.
// The clause bodies are in tail position if the cond is.
function compileCond(x: Pair, clauses: readonly Sexp[], cx: Cx, st: St): [Code, St] {
    if (clauses.length === 0) throw new CompileError('cond needs at least one clause', x.pos);
    const [end, st1]     = label(st, 'cond_end');
    const [noMatch, st2] = faultLabel(st1, 'RT_FAULT_NO_CLAUSE', 'cond', x.pos);
    const [code, st3]    = clauses.reduce<[Code, St]>(([acc, s], clause) => {
        const [c, s1] = compileClause(x, clause, end, cx, s);
        return [[acc, c], s1];
    }, [[], st2]);
    return [[code, st.t.jump(noMatch), `${end}:`], st3];
}

function compileClause(x: Pair, clause: Sexp, end: string, cx: Cx, st: St): [Code, St] {
    if (clause.t !== 'pair' || clause.cdr.t !== 'pair') {
        throw new CompileError(`a cond clause is (test body...), not ${show(clause)}`, posOf(clause) ?? x.pos);
    }
    const test = clause.car;
    if (test.t === 'sym' && test.name === '#true') {
        const [body, st1] = compileBody(clause.cdr, cx, st);
        return [[body, st.t.jump(end)], st1];
    }
    const [code, st1]    = compileExpr(test, { ...cx, tail: false }, st);
    const [body, st2]    = compileBody(clause.cdr, cx, st1);
    const [next, st3]    = label(st2, 'cond_next');
    const [notBool, st4] = faultLabel(st3, 'RT_FAULT_NOT_BOOL', 'cond test', posOf(test) ?? clause.pos);
    return [[code, st.t.testBool(next, notBool), body, st.t.jump(end), `${next}:`], st4];
}

// --- helpers ------------------------------------------------------------------

const useSlot = (st: St, si: number): St => (si < st.slots ? st : { ...st, slots: si + 1 });

function lookup(env: Env, name: string): number | null {
    for (; env !== null; env = env.next) if (env.name === name) return env.slot;
    return null;
}

function lookupFn(fns: Fns, name: string): Fn | null {
    for (; fns !== null; fns = fns.next) if (fns.fn.name === name) return fns.fn;
    return null;
}

function label(st: St, hint: string): [string, St] {
    return [`L${hint}_${st.labels}`, { ...st, labels: st.labels + 1 }];
}

type Fault = 'RT_FAULT_NOT_INT' | 'RT_FAULT_OVERFLOW' | 'RT_FAULT_NOT_BOOL' | 'RT_FAULT_NO_CLAUSE' | 'RT_FAULT_STACK'
           | 'RT_FAULT_NOT_CONS' | 'RT_FAULT_NOT_LIST' | 'RT_FAULT_HEAP' | 'RT_FAULT_DIV_ZERO'
           | 'RT_FAULT_NOT_FUNC' | 'RT_FAULT_ARITY';

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
