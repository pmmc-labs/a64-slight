// The checker (docs/TYPES.md, D247): what types say about a program,
// before it's compiled, as warnings. It never changes what's compiled.
//
// It runs on the forms expand.ts gives with match kept (expandProgram's
// keepMatch), so a pattern is still a pattern, before split.ts, so its
// places are the source's. By default it warns only about what will go
// wrong if that code runs: a value that can't be what's asked of it (a
// fault), a message no recv of the actor can take (a dead letter), a
// match no arm of which can match, an arm nothing reaches. With strict,
// also where it might: a type wider than the one wanted.
//
// What it knows of values:
//
//   - literals, the builtins (builtins.ts), and the program's and the
//     prelude's functions: a sig's types, or else, for a function's
//     result, what its body gives, worked out over all of them at once,
//     from never up, till nothing changes (a few rounds, then any);
//   - a parameter is what its function's sig says, or any;
//   - a match's or recv's arm narrows its value by its pattern, and the
//     names the pattern binds get the types of their parts; a cond's
//     test narrows a local it tests with a type predicate, eq? to a
//     literal, not, and the and and or made of them (refine);
//   - a process: fork gives a task<T>, spawn and connect an actor<M>, M
//     being what the recvs of the function it starts, and of every
//     function that one can tail-call that waits, take (mailboxes).
//
// The warnings are collected as they're found (report), in the last
// round only; the rounds before it only work out results.

import { stateFunctions } from './classify.ts';
import { CompileError } from './errors.ts';
import { expand } from './expand.ts';
import { displayName, PRELUDE_SUFFIX } from './modules.ts';
import { REST } from './pattern.ts';
import { NIL, cons, isForm, isSym, list, posOf, show, toArray, TUPLE, type Pair, type Pos, type Sexp } from './sexp.ts';
import { BUILTINS, CONSTANTS, PREDICATES, RESULT_ANY, cxr, cxrSteps, faultOf, resultOf } from './builtins.ts';
import {
    ANY, BOOL, FALSE_T, FLOAT, FUN_ANY, INT, LIST_ANY, NEVER, NIL_T, PID, STR, SYM, TRUE_T, TUPLE_ANY,
    BASE_TYPES, actorOf, addressOf, consOf, datumType, elementOf, findDef, fits, headOf, lit, listOf, meet, members,
    overlaps, readType, same, shape, showType, slyName, subst, tailOf, taskOf, tupleElement, tupleOf, union, unfold, widen,
    type Defs, type Ty,
} from './types.ts';

export type Warning = { readonly pos: Pos | null; readonly message: string; readonly certain: boolean };

// prelude: check the prelude's functions too, against their sigs (its
// tests do; a program's check needn't, as nothing changes them).
// modules: files whose --strict warnings are their own checks' (D248):
// a module is checked strictly as a program of its own.
export type CheckOptions = { readonly strict?: boolean; readonly prelude?: boolean; readonly modules?: readonly string[] };

// --- the program's functions ---------------------------------------------------------

type Module = 'user' | 'prelude';

// A function's sig: its type variables, its parameters' types and its
// result's, and what its actor takes, if it says.
type Sig = {
    readonly vars: readonly string[];
    readonly params: readonly Ty[];
    readonly result: Ty;
    readonly recv: Ty | null;
};

type Fn = {
    readonly name: string;
    readonly module: Module;
    readonly params: readonly string[];
    readonly body: Sexp;            // its forms, as a list
    readonly pos: Pos | null;
    readonly sig: Sig | null;
    readonly state: boolean;        // it waits for messages, so it runs in an actor
};

// The functions a module's code can call, by name: the prelude's, and a
// program's over them (D80).
type Fns = Readonly<Record<string, Fn>>;

// What every function's check shares.
type G = {
    readonly defs: Defs;
    readonly results: Readonly<Record<string, Ty>>;     // module:name, worked out
    readonly mailboxes: Readonly<Record<string, Ty>>;   // a state function: what its actor takes from there on
    readonly strict: boolean;
    readonly report: ((w: Warning) => void) | null;     // null: a round that only works out results
    readonly prelude: Fns;                              // the prelude's, for `f (prelude)`
};

// The locals: an association list from name to type. id says which
// binding an entry is: a narrowed copy of one keeps its id, so two
// branches' narrowings of it can be put together, and a let of a local
// (case's topic, say) is an alias, so narrowing it narrows that too.
// count is how many bindings the list has, the next one's id.
type Env = {
    readonly name: string;
    readonly ty: Ty;
    readonly id: number;
    readonly alias: number | null;
    readonly count: number;
    readonly next: Env;
} | null;

type Cx = {
    readonly g: G;
    readonly fns: Fns;
    readonly module: Module;
    readonly env: Env;
    readonly vars: readonly string[];       // type variables in scope, a generic sig's
    readonly kind: 'task' | 'actor' | 'unknown';
    readonly self: Ty;                      // $$
    readonly mailbox: Ty;                   // what a recv here can be sent
};

function lookup(env: Env, name: string): Env {
    for (; env !== null; env = env.next) if (env.name === name) return env;
    return null;
}

function byId(env: Env, id: number): Env {
    for (; env !== null; env = env.next) if (env.id === id) return env;
    return null;
}

const bind = (env: Env, name: string, ty: Ty, alias: number | null = null): Env =>
    ({ name, ty, id: env?.count ?? 0, alias, count: (env?.count ?? 0) + 1, next: env });

// --- the entry ---------------------------------------------------------------------------

// The warnings for a program, prelude being the prelude's forms and
// program the program's, as expandProgram gives them with keepMatch.
export function checkProgram(prelude: Sexp, program: Sexp, opts: CheckOptions = {}): readonly Warning[] {
    const pforms = toArray(prelude) ?? [];
    const uforms = toArray(program) ?? [];
    const defs   = readDeftypes(pforms, uforms);
    const pfns   = readFunctions(pforms, 'prelude', defs, []);
    const states = stateFunctions(defunsOf(uforms).map((d) => {
        const lowered = toArray(eraseTypes(expand(d, false)))!;
        return { name: (lowered[1] as { name: string }).name, params: paramNames(lowered[2]!), body: list(...lowered.slice(3)) };
    }), (body) => waits(body));
    const ufns  = readFunctions(uforms, 'user', defs, states);
    const fnsP: Fns = Object.fromEntries(pfns.map((f) => [f.name, f]));
    const fnsU: Fns = { ...fnsP, ...Object.fromEntries(ufns.map((f) => [f.name, f])) };
    const mailboxes = mailboxesOf(ufns);
    const strict = opts.strict ?? false;

    // the functions' results, from never up: each round puts each one's
    // to use at once, so a chain of calls settles in a round or two; one
    // that's still changing after a few is any from then on, and the
    // rest go on settling around it
    const unsigned = [...pfns, ...ufns].filter((f) => f.sig === null);
    const results: Record<string, Ty> = Object.fromEntries(unsigned.map((f) => [key(f), NEVER]));
    const capped: string[] = [];
    for (let round = 0; ; round++) {
        let changed = 0;
        for (const f of unsigned) {
            const k = key(f);
            if (capped.includes(k)) continue;
            const g: G = { defs, results, mailboxes, strict, report: null, prelude: fnsP };
            const raw = checkFn(f, g, f.module === 'user' ? fnsU : fnsP);
            if (same(raw, results[k]!)) continue;
            // a result that's still changing is a loop's, which a list
            // it builds could keep growing: from the third round, a list
            const t = round >= 2 ? widen(raw, defs) : raw;
            if (same(t, results[k]!)) continue;
            results[k] = round >= 6 ? ANY : t;
            if (round >= 6) capped.push(k);
            changed++;
        }
        if (changed === 0) break;
        if (round >= 30) {
            unsigned.forEach((f) => { results[key(f)] = ANY; });
            break;
        }
    }

    const found: Warning[] = [];
    const report = (w: Warning): void => {
        if (!w.certain && (!strict || (w.pos !== null && (opts.modules ?? []).includes(w.pos.file)))) return;
        if (found.some((x) => x.message === w.message && samePos(x.pos, w.pos))) return;
        found.push(w);
    };
    const g: G = { defs, results, mailboxes, strict, report, prelude: fnsP };
    if (opts.prelude === true) pfns.forEach((f) => checkFn(f, g, fnsP));
    ufns.forEach((f) => checkFn(f, g, fnsU));
    const top = uforms.filter((f) => !isForm(f, 'defun') && !isForm(f, 'deftype') && !isForm(f, 'sig'));
    const cx: Cx = { g, fns: fnsU, module: 'user', env: null, vars: [], kind: 'task', self: taskOf(ANY), mailbox: ANY };
    bodyType(list(...top), cx);

    const files = [...new Set(found.map((w) => w.pos?.file ?? ''))];
    return [...found].sort((a, b) =>
        files.indexOf(a.pos?.file ?? '') - files.indexOf(b.pos?.file ?? '')
        || (a.pos?.line ?? 0) - (b.pos?.line ?? 0) || (a.pos?.col ?? 0) - (b.pos?.col ?? 0));
}

const key = (f: Fn): string => `${f.module}:${f.name}`;
const samePos = (a: Pos | null, b: Pos | null): boolean =>
    a === b || (a !== null && b !== null && a.file === b.file && a.line === b.line && a.col === b.col);

export function formatWarning(w: Warning): string {
    const at = w.pos === null ? '' : `${w.pos.file}:${w.pos.line}:${w.pos.col}: `;
    return `${at}warning: ${w.message}`;
}

const defunsOf = (forms: readonly Sexp[]): readonly Pair[] => forms.filter((f) => isForm(f, 'defun')) as Pair[];

const paramNames = (params: Sexp): readonly string[] =>
    (toArray(params) ?? []).filter((p) => p.t === 'sym').map((p) => (p as { name: string }).name);

// A function's body has a recv, outside a lambda or a new process's.
function waits(x: Sexp): boolean {
    if (x.t !== 'pair') return false;
    if (x.car.t === 'sym') {
        switch (x.car.name) {
            case 'recv': return true;
            case 'quote': case 'lambda': case 'fork': case 'spawn': case 'connect': return false;
        }
    }
    for (let xs: Sexp = x; xs.t === 'pair'; xs = xs.cdr) if (waits(xs.car)) return true;
    return false;
}

// The forms with the types taken out, for code generation and split.ts:
// deftype and sig at the top level go, and (the T e) is e.
export function eraseTypes(forms: Sexp): Sexp {
    const items = toArray(forms);
    if (items === null) return forms;
    return list(...items.filter((f) => !isForm(f, 'deftype') && !isForm(f, 'sig')).map(erase));
}

function erase(x: Sexp): Sexp {
    if (x.t !== 'pair' || isForm(x, 'quote')) return x;
    if (isForm(x, 'the')) {
        const items = toArray(x);
        if (items !== null && items.length === 3) return erase(items[2]!);
        throw new CompileError(`the is (the type expr), not ${show(x)}`, x.pos);
    }
    // a loop down the list (D39), so a long body doesn't run Node out of stack
    const cells: Pair[] = [];
    let end: Sexp = x;
    for (; end.t === 'pair'; end = end.cdr) cells.push(end);
    const cars = cells.map((c) => erase(c.car));
    if (cars.every((c, i) => c === cells[i]!.car)) return x;
    return cells.reduceRight<Sexp>((tail, cell, i) => cons(cars[i]!, tail, cell.pos), end);
}

// --- declarations -------------------------------------------------------------------------

// (deftype Name T) and (deftype (Name A B) T), the prelude's and then
// the program's. Every name first, so one can mention another, or itself.
function readDeftypes(pforms: readonly Sexp[], uforms: readonly Sexp[]): Defs {
    const decls = [...pforms.map((f) => [f, 'prelude'] as const), ...uforms.map((f) => [f, 'user'] as const)]
        .filter(([f]) => isForm(f, 'deftype'));
    const heads = decls.map(([f, module]) => {
        const items = toArray(f)!;
        if (items.length !== 3) throw new CompileError(`deftype is (deftype Name type), not ${show(f)}`, posOf(f));
        const head = items[1]!;
        const parts = head.t === 'sym' ? [head] : toArray(head);
        if (parts === null || parts.length === 0 || parts.some((p) => p.t !== 'sym')) {
            throw new CompileError(`a type's name is a name, or (Name A B) for one with parameters, not ${show(head)}`, posOf(head) ?? posOf(f));
        }
        const [name, ...params] = parts.map((p) => (p as { name: string }).name);
        if (BASE_TYPES.includes(name!) || ['list', 'shape', 'or', 'task', 'actor', 'address'].includes(name!)) {
            throw new CompileError(`${name} is a type already`, posOf(head) ?? posOf(f));
        }
        return { name: name!, params, body: items[2]!, module, pos: posOf(f) };
    });
    heads.forEach((h, i) => {
        const first = heads.findIndex((o) => o.name === h.name);
        if (first !== i) {
            const what = heads[first]!.module === 'prelude' && h.module === 'user' ? 'a type in the prelude' : 'already a type';
            throw new CompileError(`${h.name} is ${what}`, h.pos);
        }
    });
    const named: Defs = heads.reduceRight<Defs>((next, h) => ({ name: h.name, params: h.params, body: ANY, next }), null);
    return heads.reduceRight<Defs>((next, h) => ({ name: h.name, params: h.params, body: readType(h.body, h.params, named), next }), null);
}

// A module's defuns, each with its sig, if there's one.
function readFunctions(forms: readonly Sexp[], module: Module, defs: Defs, states: readonly string[]): readonly Fn[] {
    const defuns = defunsOf(forms);
    const sigs = forms.filter((f) => isForm(f, 'sig')).map((f) => readSig(f as Pair, defs));
    sigs.forEach((s, i) => {
        if (sigs.findIndex((o) => o.name === s.name) !== i) throw new CompileError(`${s.name} has a sig already`, s.pos);
        if (!defuns.some((d) => isSym(toArray(d)![1]!, s.name))) throw new CompileError(`a sig for ${s.name}, which isn't a function here`, s.pos);
    });
    return defuns.flatMap((d) => {
        const items = toArray(d);
        if (items === null || items.length < 4 || items[1]!.t !== 'sym') return [];    // codegen says what's wrong
        const name = items[1]!.name;
        const params = paramNames(items[2]!);
        const s = sigs.find((o) => o.name === name) ?? null;
        if (s !== null && s.sig.params.length !== params.length) {
            throw new CompileError(`${name}'s sig has ${s.sig.params.length} parameter${s.sig.params.length === 1 ? '' : 's'}, and ${name} ${params.length}`, s.pos);
        }
        return [{ name, module, params, body: list(...items.slice(3)), pos: d.pos, sig: s?.sig ?? null, state: states.includes(name) }];
    });
}

// (sig name (T ...) R), (sig (name A B) (T ...) R), and either with
// (recv M) after R.
function readSig(x: Pair, defs: Defs): { readonly name: string; readonly sig: Sig; readonly pos: Pos | null } {
    const items = toArray(x)!;
    const bad = (): CompileError => new CompileError(`sig is (sig name (param-type ...) result-type), maybe with (recv message-type) after, not ${show(x)}`, x.pos);
    if (items.length !== 4 && items.length !== 5) throw bad();
    const head = items[1]!.t === 'sym' ? [items[1]!] : toArray(items[1]!);
    const params = toArray(items[2]!);
    if (head === null || head.length === 0 || head.some((h) => h.t !== 'sym') || params === null) throw bad();
    const [name, ...vars] = head.map((h) => (h as { name: string }).name);
    let recv: Ty | null = null;
    if (items.length === 5) {
        const r = toArray(items[4]!);
        if (r === null || r.length !== 2 || !isSym(r[0]!, 'recv')) throw bad();
        recv = readType(r[1]!, vars, defs);
    }
    return {
        name: name!,
        sig: { vars, params: params.map((p) => readType(p, vars, defs)), result: readType(items[3]!, vars, defs), recv },
        pos: x.pos,
    };
}

// --- mailboxes -----------------------------------------------------------------------------

// For each function that waits: what the actor it's in can take from
// then on, which is what the recvs of it, and of every function it can
// tail-call that waits, take, or what their sigs say they take. There's
// no selective receive, so a message none of them takes is a dead letter.
function mailboxesOf(fns: readonly Fn[]): Readonly<Record<string, Ty>> {
    const states = fns.filter((f) => f.state);
    const own: Record<string, Ty> = Object.fromEntries(states.map((f) => [f.name, f.sig?.recv ?? recvsTake(f.body)]));
    const calls: Record<string, readonly string[]> = Object.fromEntries(states.map((f) =>
        [f.name, tailCallsIn(f.body).filter((n) => own[n] !== undefined)]));
    return Object.fromEntries(states.map((f) => {
        const seen = [f.name];
        for (let i = 0; i < seen.length; i++) for (const c of calls[seen[i]!] ?? []) if (!seen.includes(c)) seen.push(c);
        return [f.name, union(...seen.map((n) => own[n]!))];
    }));
}

// The union of what a body's recvs take, from their patterns.
function recvsTake(x: Sexp): Ty {
    if (x.t !== 'pair') return NEVER;
    if (x.car.t === 'sym') {
        switch (x.car.name) {
            case 'quote': case 'lambda': case 'fork': case 'spawn': case 'connect': return NEVER;
            case 'recv': {
                const clauses = toArray(x.cdr) ?? [];
                if (clauses.length === 0) return ANY;
                return union(...clauses.map((c) => (c.t === 'pair' ? union(patternType(c.car), recvsTake(c.cdr)) : NEVER)));
            }
        }
    }
    const out: Ty[] = [];
    for (let xs: Sexp = x; xs.t === 'pair'; xs = xs.cdr) out.push(recvsTake(xs.car));
    return union(...out);
}

// The names a body calls, anywhere but in a lambda's or a new process's:
// the state functions among them are the ones it can tail-call, since a
// state function can only be tail-called (the recv rule).
function tailCallsIn(x: Sexp): readonly string[] {
    if (x.t !== 'pair') return [];
    if (x.car.t === 'sym' && ['quote', 'lambda', 'fork', 'spawn', 'connect'].includes(x.car.name)) return [];
    const out: string[] = x.car.t === 'sym' ? [x.car.name] : [];
    for (let xs: Sexp = x; xs.t === 'pair'; xs = xs.cdr) out.push(...tailCallsIn(xs.car));
    return out;
}

// --- functions -------------------------------------------------------------------------------

// What a function's body gives, checking it on the way.
function checkFn(f: Fn, g: G, fns: Fns): Ty {
    const ptypes = f.sig?.params ?? f.params.map(() => ANY);
    const env = f.params.reduce<Env>((e, p, i) => bind(e, p, ptypes[i]!), null);
    const mailbox = f.state ? (g.mailboxes[f.name] ?? ANY) : ANY;
    const cx: Cx = {
        g, fns, module: f.module, env, vars: f.sig?.vars ?? [],
        kind: f.state ? 'actor' : 'unknown',
        self: f.state ? actorOf(mailbox) : PID,
        mailbox: f.sig?.recv ?? ANY,
    };
    const t = bodyType(f.body, cx);
    if (f.sig !== null) {
        const sly = isSly(f.pos);
        const r = f.sig.result;
        if (t.k !== 'never' && !overlaps(t, r, g.defs)) {
            warn(cx, f.pos, `${name(f.name, sly)} says it gives ${showType(r, sly)}, and it gives ${showType(t, sly)}`, true);
        } else if (t.k !== 'never' && !fits(t, r, g.defs)) {
            warn(cx, f.pos, `${name(f.name, sly)} says it gives ${showType(r, sly)}, and it may give ${showType(t, sly)}`, false);
        }
    }
    return t;
}

const isSly = (pos: Pos | null): boolean => pos !== null && pos.file.endsWith('.sly');
const name = (n: string, sly: boolean): string => (sly ? slyName(displayName(n)) : displayName(n));

// A function by the name code calls it: `f (prelude)` is the prelude's
// f, as a library's code names it (modules.ts, D248).
const fnNamed = (cx: Cx, n: string): Fn | undefined =>
    n.endsWith(PRELUDE_SUFFIX) ? cx.g.prelude[n.slice(0, -PRELUDE_SUFFIX.length)] : cx.fns[n];

function warn(cx: Cx, pos: Pos | null, message: string, certain: boolean): void {
    cx.g.report?.({ pos, message, certain });
}

// A body's forms in order, each let binding for the ones after; its
// value is the last one's. A form that never gives a value (a raise, a
// fault) makes the body never give one.
function bodyType(forms: Sexp, cx: Cx): Ty {
    let env = cx.env;
    let t: Ty = NIL_T;
    for (; forms.t === 'pair'; forms = forms.cdr) {
        const f = forms.car;
        const here = { ...cx, env };
        if (isForm(f, 'let')) {
            const items = toArray(f) ?? [];
            const value = items[2];
            t = value === undefined ? ANY : typeOf(value, here);
            if (items[1]?.t === 'sym') env = bind(env, items[1].name, t, aliasOf(value, env));
        } else {
            t = typeOf(f, here);
        }
        if (t.k === 'never') return NEVER;
    }
    return t;
}

// The binding a let of a plain local is another name for.
function aliasOf(value: Sexp | undefined, env: Env): number | null {
    if (value === undefined || value.t !== 'sym') return null;
    return lookup(env, value.name)?.id ?? null;
}

// --- expressions -------------------------------------------------------------------------------

function typeOf(x: Sexp, cx: Cx, expected: Ty | null = null): Ty {
    switch (x.t) {
        case 'int':   return INT;
        case 'float': return FLOAT;
        case 'str':   return STR;
        case 'nil':   return NIL_T;
        case 'sym':   return nameType(x.name, cx);
        case 'pair':  return formType(x, cx, expected);
    }
}

function nameType(n: string, cx: Cx): Ty {
    switch (n) {
        case '#true':  return TRUE_T;
        case '#false': return FALSE_T;
        case '$$':     return cx.self;
        case '^$$':    return cx.kind === 'actor' ? PID : union(PID, NIL_T);    // the root, a task, has none
        case '@ARGV':  return listOf(STR);
        case '%ENV':   return listOf(shape([STR, STR]));
    }
    const local = lookup(cx.env, n);
    if (local !== null) return local.ty;
    const c = CONSTANTS[n];
    if (c !== undefined) return c;
    const f = fnNamed(cx, n);
    if (f !== undefined) return funOfFn(f, cx.g);
    // a builtin as a value takes the arguments it's called with, its
    // optional ones among them (D219): no arity is fixed
    const b = BUILTINS[n];
    if (b !== undefined) return { k: 'fun', params: null, result: typeof b.result === 'function' ? ANY : b.result };
    if (PREDICATES[n] !== undefined) return { k: 'fun', params: [ANY], result: BOOL };
    if (cxrSteps(n) !== null) return { k: 'fun', params: [ANY], result: ANY };
    return ANY;
}

function funOfFn(f: Fn, g: G): Ty {
    if (f.sig !== null) {
        const vars = f.sig.vars.map((v) => [v, ANY] as const);
        return { k: 'fun', params: f.sig.params.map((p) => subst(p, vars)), result: subst(f.sig.result, vars) };
    }
    return { k: 'fun', params: f.params.map(() => ANY), result: g.results[key(f)] ?? ANY };
}

function formType(x: Pair, cx: Cx, expected: Ty | null): Ty {
    const head = x.car;
    const args = toArray(x.cdr) ?? [];
    if (head.t === 'sym' && lookup(cx.env, head.name) === null) {
        switch (head.name) {
            case 'quote':   return args.length === 1 ? datumType(args[0]!) : ANY;
            case 'lambda':  return lambdaType(args, cx, expected);
            case 'let':     return args.length === 2 ? typeOf(args[1]!, cx) : ANY;
            case 'cond':    return condType(args, cx);
            case 'do':      return bodyType(x.cdr, cx);
            case 'match':   return matchType(x, args, cx);
            case 'recv':    return recvType(x, args, cx);
            case 'yield':   return args.length === 1 ? typeOf(args[0]!, cx) : ANY;
            case 'fork':    return forkType(args, cx);
            case 'spawn':   return spawnType(x, [], args[0], cx);
            case 'connect': return spawnType(x, args.slice(0, -1), args[args.length - 1], cx);
            case 'the':     return theType(x, args, cx);
            case 'defun': case 'deftype': case 'sig': return NIL_T;
        }
        const f = fnNamed(cx, head.name);
        if (f !== undefined) return callFn(x, f, args, cx);
        const b = BUILTINS[head.name];
        if (b !== undefined) return callBuiltin(x, head.name, args, cx);
        const p = PREDICATES[head.name];
        if (p !== undefined) return argsThen(args, cx, BOOL);
        const steps = cxrSteps(head.name);
        if (steps !== null) return callCxr(x, head.name, steps, args, cx);
        return argsThen(args, cx, ANY);
    }
    return callValue(x, head, args, cx);
}

// The arguments' types, for what's in them, then t; never if one never
// gives a value.
function argsThen(args: readonly Sexp[], cx: Cx, t: Ty): Ty {
    const ts = args.map((a) => typeOf(a, cx));
    return ts.some((a) => a.k === 'never') ? NEVER : t;
}

// --- calls ---------------------------------------------------------------------------------------

const ORDINALS = ['first', 'second', 'third', 'fourth', 'fifth', 'sixth', 'seventh', 'eighth'];
const nth = (i: number, n: number): string => (n === 1 ? 'argument' : `${ORDINALS[i] ?? `${i + 1}th`} argument`);

// Whether a value of type a can be what wants a t: a warning if it
// can't (certain), or may not (strict), with fault as what it does.
// Gives whether it can.
function want(cx: Cx, a: Ty, t: Ty, pos: Pos | null, what: (verb: string) => string, fault: string | null): boolean {
    if (a.k === 'never' || a.k === 'any') return true;
    const sly = isSly(pos);
    const ending = fault === null ? '' : `: it faults :${fault}`;
    if (!overlaps(a, t, cx.g.defs)) {
        warn(cx, pos, `${what('is')} ${showType(a, sly)}, not ${showType(t, sly)}${ending}`, true);
        return false;
    }
    if (!fits(a, t, cx.g.defs)) {
        warn(cx, pos, `${what('may be')} ${showType(a, sly)}, not just ${showType(t, sly)}${fault === null ? '' : `: it would fault :${fault}`}`, false);
    }
    return true;
}

function callBuiltin(x: Pair, n: string, args: readonly Sexp[], cx: Cx): Ty {
    const b = BUILTINS[n]!;
    const ts = args.map((a) => typeOf(a, cx));
    if (ts.some((t) => t.k === 'never')) return NEVER;
    const sly = isSly(x.pos);
    let ok = true;
    ts.forEach((t, i) => {
        const p = i < b.params.length ? b.params[i]! : b.rest;
        if (p === null) return;
        const pos = posOf(args[i]!) ?? x.pos;
        if (!want(cx, t, p, pos, (v) => `${name(n, sly)}'s ${nth(i, Math.max(args.length, b.params.length))} ${v}`, faultOf(t, p, cx.g.defs))) ok = false;
    });
    if (!ok) return NEVER;
    const processes = processCall(x, n, args, ts, cx);
    if (processes !== null) return processes;
    return typeof b.result === 'function' ? b.result(ts, cx.g.defs) : b.result;
}

function callCxr(x: Pair, n: string, steps: readonly ('a' | 'd')[], args: readonly Sexp[], cx: Cx): Ty {
    const ts = args.map((a) => typeOf(a, cx));
    if (ts.some((t) => t.k === 'never') || ts.length !== 1) return ts.some((t) => t.k === 'never') ? NEVER : ANY;
    const { result, failsAt } = cxr(ts[0]!, steps, cx.g.defs);
    if (failsAt !== null) {
        const sly = isSly(x.pos);
        const what = failsAt === 0 ? `${name(n, sly)} takes a non-empty list, and this is` : `${name(n, sly)} takes a list of at least ${failsAt + 1} conses down, and this is`;
        warn(cx, posOf(args[0]!) ?? x.pos, `${what} ${showType(ts[0]!, sly)}: it faults :not-a-cons`, true);
        return NEVER;
    }
    if (cx.g.strict && ts[0]!.k !== 'any') {
        // each step on a value that may not be a cons
        let cur: Ty = ts[0]!;
        for (const s of steps) {
            if (!fits(cur, { k: 'cons', head: ANY, tail: LIST_ANY }, cx.g.defs)) {
                const sly = isSly(x.pos);
                warn(cx, posOf(args[0]!) ?? x.pos, `${name(n, sly)} takes a non-empty list, and this may be ${showType(ts[0]!, sly)}: it would fault :not-a-cons`, false);
                break;
            }
            cur = s === 'a' ? headOf(cur, cx.g.defs) : tailOf(cur, cx.g.defs);
        }
    }
    return result;
}

// A call of a value: a local, or what an expression gives.
function callValue(x: Pair, head: Sexp, args: readonly Sexp[], cx: Cx): Ty {
    const h = typeOf(head, cx);
    const fparams = h.k === 'fun' ? h.params : null;
    const ts = args.map((a, i) => typeOf(a, cx, fparams?.[i] ?? null));
    if (h.k === 'never' || ts.some((t) => t.k === 'never')) return NEVER;
    const sly = isSly(x.pos);
    const pos = posOf(head) ?? x.pos;
    if (!want(cx, h, FUN_ANY, pos, (v) => `what's called here ${v}`, 'not-a-function')) return NEVER;
    const funs = members(h, cx.g.defs).filter((m) => m.k === 'fun') as { params: readonly Ty[] | null }[];
    if (funs.length > 0 && funs.every((f) => f.params !== null && f.params.length !== args.length)) {
        const ns = [...new Set(funs.map((f) => f.params!.length))].join(' or ');
        warn(cx, pos, `what's called here takes ${ns} argument${ns === '1' ? '' : 's'}, and it's given ${args.length}: it faults :arity`, true);
        return NEVER;
    }
    if (h.k === 'fun' && h.params !== null) {
        ts.forEach((t, i) => {
            const p = h.params![i];
            if (p !== undefined) want(cx, t, p, posOf(args[i]!) ?? x.pos, (v) => `the ${nth(i, args.length)} ${v}`, null);
        });
    }
    return resultOf(h);
}

// A call of one of the program's or the prelude's functions: its sig's
// types, its type variables worked out from the arguments, or else what
// its body was worked out to give.
function callFn(x: Pair, f: Fn, args: readonly Sexp[], cx: Cx): Ty {
    const sly = isSly(x.pos);
    if (f.sig === null) {
        const ts = args.map((a) => typeOf(a, cx));
        if (ts.some((t) => t.k === 'never')) return NEVER;
        return cx.g.results[key(f)] ?? ANY;
    }
    const sig = f.sig;
    // the arguments that aren't lambdas first, so a lambda's parameters
    // can come from what they say the variables are
    let vars: readonly (readonly [string, Ty])[] = [];
    const ts: (Ty | null)[] = args.map((a, i) => {
        if (isForm(a, 'lambda')) return null;
        const t = typeOf(a, cx);
        vars = unify(sig.params[i] ?? ANY, t, sig.vars, vars, cx.g.defs);
        return t;
    });
    const known = (t: Ty): Ty => subst(t, [...vars, ...sig.vars.map((v) => [v, ANY] as const)]);
    // a lambda's parameters can depend on what it gives back (fold/l's
    // accumulator), so it's worked out quietly till the variables stop
    // changing, then checked once, with what they came to
    const lambdas = ts.flatMap((t, i) => (t === null ? [i] : []));
    if (lambdas.length > 0) {
        const quiet: Cx = { ...cx, g: { ...cx.g, report: null } };
        let settled = false;
        for (let round = 0; round < 4 && !settled; round++) {
            const before = vars;
            for (const i of lambdas) vars = unify(sig.params[i] ?? ANY, typeOf(args[i]!, quiet, known(sig.params[i] ?? ANY)), sig.vars, vars, cx.g.defs);
            settled = vars.length === before.length && vars.every(([n, v]) => before.some(([m, w]) => m === n && same(v, w)));
        }
        if (!settled) vars = vars.map(([n]) => [n, ANY] as const);
    }
    const all = ts.map((t, i) => (t !== null ? t : typeOf(args[i]!, cx, known(sig.params[i] ?? ANY))));
    if (all.some((t) => t.k === 'never')) return NEVER;
    all.forEach((t, i) => {
        const p = known(sig.params[i] ?? ANY);
        want(cx, t, p, posOf(args[i]!) ?? x.pos, (v) => `${name(f.name, sly)}'s ${nth(i, args.length)} ${v}`, null);
    });
    return known(sig.result);
}

// What a sig's type variables are, from an argument of type t where it
// takes a p: each variable found in p gets the part of t where it is,
// put together with what it had.
function unify(p: Ty, t: Ty, names: readonly string[], vars: readonly (readonly [string, Ty])[], defs: Defs, depth = 0): readonly (readonly [string, Ty])[] {
    if (depth > 12 || t.k === 'never') return vars;
    switch (p.k) {
        case 'var': {
            if (!names.includes(p.name)) return vars;
            const had = vars.find(([n]) => n === p.name);
            return had === undefined ? [...vars, [p.name, t]] : vars.map(([n, v]) => [n, n === p.name ? union(v, t) : v] as const);
        }
        case 'list':
            return unify(p.of, elementOf(meet(t, LIST_ANY, defs), defs), names, vars, defs, depth + 1);
        case 'cons': {
            const c = meet(t, { k: 'cons', head: ANY, tail: LIST_ANY }, defs);
            const v1 = unify(p.head, headOf(c, defs), names, vars, defs, depth + 1);
            return unify(p.tail, tailOf(c, defs), names, v1, defs, depth + 1);
        }
        case 'tuple':
            if (p.of === null) return vars;
            return p.of.reduce((vs, e, i) => unify(e, tupleElement(t, i, defs), names, vs, defs, depth + 1), vars);
        case 'fun': {
            if (t.k !== 'fun') return vars;
            // a parameter that's any says nothing of the variable there
            const vs = p.params === null || t.params === null ? vars
                : p.params.reduce((acc, q, i) => (t.params![i] === undefined || t.params![i]!.k === 'any' ? acc
                    : unify(q, t.params![i]!, names, acc, defs, depth + 1)), vars);
            return t.result.k === 'any' ? vs : unify(p.result, t.result, names, vs, defs, depth + 1);
        }
        case 'task':    return t.k === 'task' ? unify(p.of, t.of, names, vars, defs, depth + 1) : vars;
        case 'actor':   return t.k === 'actor' ? unify(p.msg, t.msg, names, vars, defs, depth + 1) : vars;
        case 'address': return t.k === 'address' ? unify(p.msg, t.msg, names, vars, defs, depth + 1) : vars;
        case 'or': {
            // a variable alongside other types takes what of t they don't
            const plain = p.of.filter((m) => m.k !== 'var');
            const rest = members(t, defs).filter((m) => !plain.some((q) => fits(m, q, defs) && !hasAny(m)));
            return p.of.filter((m) => m.k === 'var').reduce((vs, v) => unify(v, union(...rest), names, vs, defs, depth + 1), vars);
        }
        case 'alias':
            return unify(unfold(p, defs), t, names, vars, defs, depth + 1);
        default:
            return vars;
    }
}

function hasAny(t: Ty): boolean {
    switch (t.k) {
        case 'any': case 'var': return true;
        case 'list': return hasAny(t.of);
        case 'cons': return hasAny(t.head) || hasAny(t.tail);
        case 'tuple': return t.of === null || t.of.some(hasAny);
        case 'or': return t.of.some(hasAny);
        case 'task': return hasAny(t.of);
        case 'actor': case 'address': return hasAny(t.msg);
        case 'fun': return true;
        case 'alias': return true;
        default: return false;
    }
}

// --- forms ---------------------------------------------------------------------------------------

function lambdaType(args: readonly Sexp[], cx: Cx, expected: Ty | null): Ty {
    const params = toArray(args[0] ?? NIL) ?? [];
    const names = params.filter((p) => p.t === 'sym').map((p) => (p as { name: string }).name);
    const want = expected !== null ? meet(expected, FUN_ANY, cx.g.defs) : null;
    const ptypes = want !== null && want.k === 'fun' && want.params !== null && want.params.length === names.length
        ? want.params.map((p) => (p.k === 'var' ? ANY : p)) : names.map(() => ANY);
    const env = names.reduce<Env>((e, n, i) => bind(e, n, ptypes[i]!), cx.env);
    const r = bodyType(list(...args.slice(1)), { ...cx, env, kind: 'unknown', self: PID, mailbox: ANY });
    return { k: 'fun', params: ptypes, result: r.k === 'never' ? ANY : r };
}

function theType(x: Pair, args: readonly Sexp[], cx: Cx): Ty {
    if (args.length !== 2) throw new CompileError(`the is (the type expr), not ${show(x)}`, x.pos);
    const t = readType(args[0]!, cx.vars, cx.g.defs);
    const e = typeOf(args[1]!, cx, t);
    if (e.k === 'never') return NEVER;
    if (!want(cx, e, t, posOf(args[1]!) ?? x.pos, (v) => `this ${v}`, null)) return t;
    // a function, a lambda with types, keeps what it was found to give;
    // anything else is what it's said to be, which may be more than it is
    const both = meet(e, t, cx.g.defs);
    return e.k === 'fun' && both.k === 'fun' ? both : t;
}

// (cond (test body...) ...): each test must be a boolean, and narrows
// what it tests for its body, and its opposite for the clauses after.
function condType(clauses: readonly Sexp[], cx: Cx): Ty {
    let env = cx.env;
    const out: Ty[] = [];
    for (const c of clauses) {
        const items = toArray(c);
        if (items === null || items.length === 0) continue;
        const [test, ...body] = items;
        const here = { ...cx, env };
        const tt = typeOf(test!, here);
        if (tt.k === 'never') break;
        want(here, tt, BOOL, posOf(test!) ?? posOf(c), (v) => `this test ${v}`, 'not-a-bool');
        if (isSym(test!, '#false')) continue;
        const yes = refine(test!, here, true);
        if (yes !== null) out.push(body.length === 0 ? tt : bodyType(list(...body), { ...cx, env: applyFacts(env, yes) }));
        if (isSym(test!, '#true')) break;
        const no = refine(test!, here, false);
        if (no === null) break;
        env = applyFacts(env, no);
    }
    return union(...out);
}

// --- narrowing -------------------------------------------------------------------------------------

// What a test being true (or false) says of the locals: each binding's
// narrowed type, by its id; or null if it can't be.
type Facts = readonly { readonly id: number; readonly ty: Ty }[];

function refine(test: Sexp, cx: Cx, truth: boolean): Facts | null {
    if (isSym(test, '#true')) return truth ? [] : null;
    if (isSym(test, '#false')) return truth ? null : [];
    if (test.t === 'sym') return refineName(test, cx, truth);
    if (test.t !== 'pair' || test.car.t !== 'sym' || lookup(cx.env, test.car.name) !== null) return [];
    const args = toArray(test.cdr) ?? [];
    const head = test.car.name;
    const defs = cx.g.defs;
    if ((head === 'not' || head === `not${PRELUDE_SUFFIX}`) && args.length === 1 && fnNamed(cx, head)?.module === 'prelude') return refine(args[0]!, cx, !truth);
    const pred = PREDICATES[head];
    if (pred !== undefined && args.length === 1) return narrow(args[0]!, cx, (t) => (truth ? meet(t, pred, defs) : minus(t, pred, defs)));
    if ((head === 'eq?' || head === 'ne?') && args.length === 2) {
        const yes = head === 'eq?' ? truth : !truth;
        const [a, b] = args;
        const [v, l] = literalOf(b!) !== null ? [a!, literalOf(b!)!] : [b!, literalOf(a!)];
        if (l === null) return [];
        return narrow(v, cx, (t) => (yes ? meet(t, l, defs) : singleton(l) ? minus(t, l, defs) : t));
    }
    if (head === 'cond') return refineCond(args, cx, truth);
    return [];
}

// A local as a test: it's #true where the test is, #false where it isn't.
function refineName(test: Sexp, cx: Cx, truth: boolean): Facts | null {
    return narrow(test, cx, (t) => meet(t, truth ? TRUE_T : FALSE_T, cx.g.defs));
}

// The type of a literal expression, or null.
function literalOf(x: Sexp): Ty | null {
    switch (x.t) {
        case 'int': return INT;
        case 'float': return FLOAT;
        case 'str': return STR;
        case 'nil': return NIL_T;
        case 'sym': return x.name === '#true' || x.name === '#false' ? lit(x.name) : null;
        case 'pair': return isForm(x, 'quote') ? datumType(toArray(x)![1] ?? NIL) : null;
    }
}

// A type of one value: a symbol, or ().
const singleton = (t: Ty): boolean => t.k === 'lit' || t.k === 'nil';

// t without the members that are all s. A member with any in it stays,
// since nothing says it's all s.
function minus(t: Ty, s: Ty, defs: Defs): Ty {
    if (t.k === 'any') return t;
    return union(...members(t, defs).filter((m) => hasAny(m) || !fits(m, s, defs)));
}

// Facts for x, if it's a local: its type, and an alias's, as f has it.
function narrow(x: Sexp, cx: Cx, f: (t: Ty) => Ty): Facts | null {
    if (x.t !== 'sym') return [];
    const b = lookup(cx.env, x.name);
    if (b === null) return [];
    const t = f(b.ty);
    if (t.k === 'never') return null;
    const facts: { id: number; ty: Ty }[] = [{ id: b.id, ty: t }];
    if (b.alias !== null) {
        const other = byId(cx.env, b.alias);
        if (other !== null) facts.push({ id: other.id, ty: meet(other.ty, t, cx.g.defs) });
    }
    return facts;
}

// A cond as a test, as and and or are made (expand.ts): true if some
// clause's test is, the ones before it not, and its body's value is
// true; false the same way, its body false.
function refineCond(clauses: readonly Sexp[], cx: Cx, truth: boolean): Facts | null {
    const ways: Facts[] = [];
    let before: Facts = [];
    for (const c of clauses) {
        const items = toArray(c);
        if (items === null || items.length !== 2) return [];
        const [test, value] = items;
        const env = applyFacts(cx.env, before);
        const yes = refine(test!, { ...cx, env }, true);
        if (yes !== null) {
            const then = refine(value!, { ...cx, env: applyFacts(env, yes) }, truth);
            if (then !== null) ways.push([...before, ...yes, ...then]);
        }
        const no = refine(test!, { ...cx, env }, false);
        if (no === null) break;
        before = [...before, ...no];
    }
    if (ways.length === 0) return null;
    // what holds every way: each binding narrowed in all of them, to the
    // union of its types (the last fact for it in each way is its type)
    const last = (way: Facts, id: number): Ty | null => {
        for (let i = way.length - 1; i >= 0; i--) if (way[i]!.id === id) return way[i]!.ty;
        return null;
    };
    const ids = [...new Set(ways[0]!.map((f) => f.id))];
    return ids.flatMap((id) => {
        const ts = ways.map((w) => last(w, id));
        return ts.every((t) => t !== null) ? [{ id, ty: union(...(ts as Ty[])) }] : [];
    });
}

function applyFacts(env: Env, facts: Facts): Env {
    let out = env;
    for (const f of facts) {
        const b = byId(out, f.id);
        if (b === null || lookup(out, b.name)?.id !== f.id) continue;
        out = { ...b, ty: f.ty, count: out?.count ?? 0, next: out };
    }
    return out;
}

// --- match and recv --------------------------------------------------------------------------------

// The type of every value a pattern can match: a name or _ is any.
function patternType(p: Sexp): Ty {
    switch (p.t) {
        case 'int':   return INT;
        case 'float': return FLOAT;
        case 'str':   return STR;
        case 'nil':   return NIL_T;
        case 'sym':   return p.name === '#true' || p.name === '#false' ? lit(p.name) : ANY;
        case 'pair': {
            const items = toArray(p) ?? [];
            if (isForm(p, 'quote')) return datumType(items[1] ?? NIL);
            if (isForm(p, TUPLE)) return tupleOf(items.slice(1).map(patternType));
            const at = items.findIndex((i) => isSym(i, REST));
            const fixed = (at < 0 ? items : items.slice(0, at)).map(patternType);
            const rest = at < 0 ? NIL_T : listRest(patternType(items[at + 1] ?? sym_('_')));
            return shape(fixed, rest);
        }
    }
}

const sym_ = (n: string): Sexp => ({ t: 'sym', name: n, pos: null });
const listRest = (t: Ty): Ty => (t.k === 'any' ? LIST_ANY : t);

// Whether every value of the member type m matches pattern p.
function covers(p: Sexp, m: Ty, defs: Defs): boolean {
    if (m.k === 'never') return true;
    if (m.k === 'or') return m.of.every((x) => covers(p, x, defs));
    if (m.k === 'alias') return covers(p, unfold(m, defs), defs);
    switch (p.t) {
        case 'int': case 'float': case 'str': return false;
        case 'nil': return m.k === 'nil';
        case 'sym':
            if (p.name === '#true' || p.name === '#false') return m.k === 'lit' && m.name === p.name;
            return p.name !== REST;
        case 'pair': {
            const items = toArray(p) ?? [];
            if (isForm(p, 'quote')) {
                const d = datumType(items[1] ?? NIL);
                return onlyOne(d) && same(m, d);
            }
            if (isForm(p, TUPLE)) {
                const es = items.slice(1);
                return m.k === 'tuple' && m.of !== null && m.of.length === es.length
                    && es.every((e, i) => coversType(e, m.of![i]!, defs));
            }
            const at = items.findIndex((i) => isSym(i, REST));
            const fixed = at < 0 ? items : items.slice(0, at);
            let cur: Ty = m;
            for (const e of fixed) {
                if (cur.k !== 'cons' || !coversType(e, cur.head, defs)) return false;
                cur = cur.tail;
            }
            const ms = members(cur, defs);
            if (at >= 0) return ms.every((x) => x.k === 'nil' || x.k === 'cons') && coversType(items[at + 1]!, cur, defs);
            return ms.every((x) => x.k === 'nil');
        }
    }
}

const coversType = (p: Sexp, t: Ty, defs: Defs): boolean => members(t, defs).every((m) => covers(p, m, defs));

// A literal type with one value in it: a symbol, (), or a list of those.
function onlyOne(t: Ty): boolean {
    if (t.k === 'lit' || t.k === 'nil') return true;
    return t.k === 'cons' && onlyOne(t.head) && onlyOne(t.tail);
}

// The names a pattern binds, with the types of their parts of a value of
// type t (which the pattern matches).
function bindPattern(p: Sexp, t: Ty, env: Env, defs: Defs): Env {
    switch (p.t) {
        case 'sym':
            if (p.name === '_' || p.name === '#true' || p.name === '#false' || p.name === REST) return env;
            return bind(env, p.name, t);
        case 'pair': {
            const items = toArray(p) ?? [];
            if (isForm(p, 'quote')) return env;
            if (isForm(p, TUPLE)) return items.slice(1).reduce((e, x, i) => bindPattern(x, tupleElement(t, i, defs), e, defs), env);
            const at = items.findIndex((i) => isSym(i, REST));
            const fixed = at < 0 ? items : items.slice(0, at);
            let cur = t;
            let e = env;
            for (const x of fixed) {
                e = bindPattern(x, headOf(cur, defs), e, defs);
                cur = tailOf(cur, defs);
            }
            return at >= 0 ? bindPattern(items[at + 1]!, cur, e, defs) : e;
        }
        default:
            return env;
    }
}

// The arms of a match or a recv, on a value of type t: each arm's body
// with what its pattern binds, the value narrowed by the arms before it.
// what: for the warnings. topic: the local matched, if it is one, which
// each arm narrows too.
type Arms = { readonly types: readonly Ty[]; readonly reachable: number; readonly left: Ty };

// unreachable: an arm that can't match; none says whether no value of
// type t could, or only none the arms before it left.
function armsType(clauses: readonly Sexp[], t: Ty, topic: Sexp | null, cx: Cx,
                  unreachable: (pos: Pos | null, left: Ty, none: boolean) => void): Arms {
    const defs = cx.g.defs;
    let left = t;
    let reachable = 0;
    const types: Ty[] = [];
    for (const c of clauses) {
        const items = toArray(c);
        if (items === null || items.length < 1) continue;
        const [p, ...body] = items;
        const armT = meet(left, patternType(p!), defs);
        if (armT.k === 'never') {
            if (t.k !== 'never') unreachable(posOf(p!) ?? posOf(c), left, meet(t, patternType(p!), defs).k === 'never');
            continue;
        }
        reachable++;
        let env = bindPattern(p!, armT, cx.env, defs);
        if (topic !== null && topic.t === 'sym') {
            const facts = narrow(topic, cx, () => armT);
            if (facts !== null) env = applyFacts(env, facts);
        }
        types.push(bodyType(list(...body), { ...cx, env }));
        left = union(...members(left, defs).filter((m) => !covers(p!, m, defs)));
    }
    return { types, reachable, left };
}

function matchType(x: Pair, args: readonly Sexp[], cx: Cx): Ty {
    if (args.length < 2) return ANY;
    const [topic, ...clauses] = args;
    const t = typeOf(topic!, cx);
    if (t.k === 'never') return NEVER;
    const defs = cx.g.defs;
    const sly = isSly(x.pos);
    const none = clauses.every((c) => c.t === 'pair' && meet(t, patternType(c.car), defs).k === 'never');
    if (none) {
        warn(cx, x.pos, `no arm of this match can match ${showType(t, sly)}: it faults :no-match`, true);
        return NEVER;
    }
    const arms = armsType(clauses, t, topic!, cx, (pos, left, none) =>
        warn(cx, pos, none ? `this arm can't match ${showType(t, sly)}`
            : left.k === 'never' ? 'this arm can\'t match: the ones before it take everything'
            : `this arm can't match ${showType(left, sly)}, which is what's left`, true));
    if (arms.left.k !== 'never' && !hasAny(arms.left)) {
        warn(cx, x.pos, `this match has no arm for ${showType(arms.left, sly)}: it would fault :no-match`, false);
    }
    return union(...arms.types);
}

// A recv's arms take what the actor is sent: what its sig says, or any.
function recvType(x: Pair, clauses: readonly Sexp[], cx: Cx): Ty {
    if (clauses.length === 0) return cx.mailbox;
    const sly = isSly(x.pos);
    const declared = cx.mailbox.k !== 'any';
    const arms = armsType(clauses, cx.mailbox, null, cx, (pos, left, none) =>
        warn(cx, pos, none ? `this arm takes nothing this actor is sent, which is ${showType(cx.mailbox, sly)}`
            : left.k === 'never' ? 'this arm can\'t take a message: the ones before it take everything'
            : `this arm takes none of ${showType(left, sly)}, which is what the ones before it leave`, true));
    if (declared && arms.left.k !== 'never' && !hasAny(arms.left)) {
        warn(cx, x.pos, `no arm here takes ${showType(arms.left, sly)}, which this actor can be sent: it would be a dead letter`, false);
    }
    return union(...arms.types);
}

// --- processes -------------------------------------------------------------------------------------

function forkType(args: readonly Sexp[], cx: Cx): Ty {
    if (args.length !== 1) return taskOf(ANY);
    const t = typeOf(args[0]!, { ...cx, kind: 'task', self: taskOf(ANY), mailbox: ANY });
    return taskOf(t.k === 'never' ? ANY : t);
}

// spawn, and connect: an actor, taking what the recvs of the function its
// body calls take (mailboxesOf).
function spawnType(x: Pair, devices: readonly Sexp[], body: Sexp | undefined, cx: Cx): Ty {
    const ds = devices.map((d) => typeOf(d, cx));
    if (ds.some((t) => t.k === 'never')) return NEVER;
    const starts = body !== undefined && body.t === 'pair' && body.car.t === 'sym' && lookup(cx.env, body.car.name) === null
        ? fnNamed(cx, body.car.name) : undefined;
    const m = starts !== undefined && starts.module === 'user' ? (cx.g.mailboxes[starts.name] ?? ANY) : ANY;
    if (body !== undefined) typeOf(body, { ...cx, kind: 'actor', self: actorOf(m), mailbox: ANY });
    if (isForm(x, 'connect') && devices.length >= 1 && isKeyword(devices[0]!, 'keypress') && m.k !== 'any' && starts !== undefined) {
        const sly = isSly(x.pos);
        if (!overlaps(KEY, m, cx.g.defs)) {
            warn(cx, posOf(body!) ?? x.pos, `the keyboard sends ${showType(KEY, sly)}, and ${name(starts.name, sly)} takes ${showType(m, sly)}: every key would be a dead letter`, true);
        }
    }
    return actorOf(m);
}

// A key, as :keypress sends it: the key, a string or a name, and the mods.
const KEY: Ty = { k: 'cons', head: union(STR, SYM), tail: listOf(SYM) };

const isKeyword = (x: Sexp, n: string): boolean => {
    const items = isForm(x, 'quote') ? toArray(x) : null;
    return items !== null && items.length === 2 && isSym(items[1]!, n);
};

// What the builtins that take processes check, and give, beyond their
// types: a task isn't sent to, an actor isn't joined, an address is a
// task's. null: the builtin's own result.
function processCall(x: Pair, n: string, args: readonly Sexp[], ts: readonly Ty[], cx: Cx): Ty | null {
    const sly = isSly(x.pos);
    const defs = cx.g.defs;
    const at = (i: number): Pos | null => posOf(args[i]!) ?? x.pos;
    const only = (t: Ty, k: Ty['k']): boolean => t.k !== 'any' && members(t, defs).every((m) => m.k === k);
    switch (n) {
        case 'send': case 'after': {
            const i = n === 'send' ? 0 : 1;
            const to = ts[i];
            const msg = ts[i + 1];
            if (to === undefined || msg === undefined) return null;
            if (only(to, 'task')) {
                // one that has ended takes it as any ended process does
                warn(cx, at(i), `${name(n, sly)} to a task, which can't be sent to unless it has ended: it would fault :not-an-actor`, false);
                return null;
            }
            if (to.k === 'actor' || to.k === 'address') {
                if (to.msg.k !== 'any' && msg.k !== 'never' && !overlaps(msg, to.msg, defs)) {
                    const who = to.k === 'actor' ? 'this actor takes' : 'this address is for';
                    warn(cx, at(i + 1), `${who} ${showType(to.msg, sly)}, and this is ${showType(msg, sly)}: it would be a dead letter`, true);
                } else if (to.msg.k !== 'any' && !fits(msg, to.msg, defs)) {
                    const who = to.k === 'actor' ? 'this actor takes' : 'this address is for';
                    warn(cx, at(i + 1), `${who} ${showType(to.msg, sly)}, and this may be ${showType(msg, sly)}: it may be a dead letter`, false);
                }
            }
            return null;
        }
        case 'join': {
            const p = ts[0]!;
            if (only(p, 'actor')) {
                warn(cx, at(0), `join of an actor, which can't be joined: it faults :not-a-task (read it, from a task)`, true);
                return NEVER;
            }
            if (only(p, 'address')) {
                warn(cx, at(0), `join of an address, which isn't a pid: it faults :not-a-pid`, true);
                return NEVER;
            }
            return ended(p, args[0]!);
        }
        case 'monitor': case 'kill': {
            if (only(ts[0]!, 'address')) {
                warn(cx, at(0), `${name(n, sly)} of an address, which isn't a pid: it faults :not-a-pid`, true);
                return NEVER;
            }
            if (n === 'monitor' && cx.kind === 'task') {
                warn(cx, x.pos, `monitor in a task, which can't be sent the notice: it faults :not-an-actor`, true);
                return NEVER;
            }
            return null;
        }
        case 'read': {
            if (cx.kind === 'actor') {
                warn(cx, x.pos, `read in an actor, which takes what comes to it with recv: it faults :not-a-task`, true);
                return NEVER;
            }
            const p = ts[0]!;
            if (p.k === 'address') return p.msg;
            if (p.k === 'task' || p.k === 'actor') return ended(p, args[0]!);
            return null;
        }
        case 'address': case 'read/any': case 'open': {
            if (cx.kind === 'actor') {
                warn(cx, x.pos, `${name(n, sly)} in an actor, which is a task's: it faults :not-a-task`, true);
                return NEVER;
            }
            return null;
        }
    }
    return null;
}

const tagged = (tag: string, ...items: readonly Ty[]): Ty => shape([lit(tag), ...items]);

// What join or read gives for a process of type p, from expression e:
// its result, or :ended once its parent has had it (D244). A process
// started right there, (join (fork ...)), can't have ended for anyone
// yet: its value is kept for its parent, which this is (D245 for an
// actor, read from a task).
function ended(p: Ty, e: Sexp): Ty {
    const value = p.k === 'task' ? p.of : ANY;
    const result = union(tagged('ok', value), tagged('error', ANY));
    const fresh = isForm(e, 'fork') || isForm(e, 'spawn') || isForm(e, 'connect');
    return fresh ? result : union(result, lit('ended'));
}

