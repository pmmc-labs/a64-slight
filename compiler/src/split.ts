// Splitting functions at recv (D167). A recv can go anywhere a tail call
// could: a body form, or a let's value, in a body in tail position, a
// cond clause's included; and inside a call or a do there, which is
// flattened first, in the order things are evaluated:
//
//     (f (g) (recv))    (let t1 (g)) (let t2 (recv)) (f t1 t2)
//
// Then the recv and the rest of the body become a receive function of
// their own, which the body tail-calls, passing it the locals the rest
// still uses:
//
//     (defun ask (c)                (defun ask (c)
//         (send c (list :get $$))       (send c (list :get $$))
//         (let n (recv                  (|ask (recv at f:3:12)| c))
//             ((:count n) n)))      (defun |ask (recv at f:3:12)| (c)
//         (pprint (list c n)))          (recv
//                                           ((:count n) (let n (do n)) (pprint (list c n)))))
//
// (|...| marks a name with spaces in it, which slight can't write.)
//
// So the code generator, and the recv rule (classify.ts), still see only
// receive functions: a function whose whole body is one recv. A function
// with a recv in it tail-calls one, so it waits too, and it can only be
// tail-called. A generated name has spaces in it, so no program can
// write it, and says where the recv is.
//
// A recv with more than one clause and something after it gets a second
// function for what's after (after recv at ...), which each clause
// tail-calls with its value, rather than a copy of the rest in each. A
// name a pattern binds that the rest needs from before the recv comes
// across under another name, and is bound again where it's wanted. More
// than 8 locals come across as one list. (recv) with no clauses takes the
// next message, whatever it is.
//
// Not split: a recv in a lambda (a lambda can't wait), or in a cond, do or
// yield that isn't in tail position; each is an error. A fork's or a
// connect's body is the body of a function of its own, the new process's,
// and is split as one.

import { CompileError } from './errors.ts';
import { patternNames } from './classify.ts';
import { NIL, cons, list, posOf, show, sym, toArray, type Pair, type Pos, type Sexp, type Sym } from './sexp.ts';

const MAX_ARGS = 8;     // as in codegen.ts: arguments go in registers

// The functions generated so far, newest last, and a count for names
// where a recv has no position.
type St = { readonly gen: readonly Sexp[]; readonly n: number };

// The forms of a program, with every recv the whole body of a receive
// function: its defuns, then the ones generated, then its top-level
// forms, in their order.
export function splitProgram(forms: Sexp): Sexp {
    const all = toArray(forms) ?? [];
    let st: St = { gen: [], n: 0 };
    const defuns: Sexp[] = [];
    for (const f of all.filter((x) => isForm(x, 'defun'))) {
        const [d, s] = splitDefun(f as Pair, st);
        defuns.push(d);
        st = s;
    }
    const top = all.filter((x) => !isForm(x, 'defun'));
    const [body, st1] = splitFunction(top, ['@ARGV'], 'the top level', false, st);
    return list(...defuns, ...st1.gen, ...body);
}

function splitDefun(x: Pair, st: St): [Sexp, St] {
    const items = toArray(x);
    const params = items === null ? null : toArray(items[2] ?? NIL);
    const name = items === null ? null : items[1];
    if (items === null || items.length < 4 || name === undefined || name === null || name.t !== 'sym' || params === null) return [x, st];    // codegen says what's wrong
    const env = params.filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
    const [body, st1] = splitFunction(items.slice(3), env, name.name, true, st);
    return [form(x.pos, ...items.slice(0, 3), ...body), st1];
}

// A function's body: its forks' and connects' bodies first, then its own
// recvs. A defun's that's one recv is a receive function already, and
// stays one, with its clauses' bodies split; the top level and a new
// process's body aren't functions that can start again.
function splitFunction(body: readonly Sexp[], env: readonly string[], fn: string, defun: boolean, st: St): [readonly Sexp[], St] {
    const [b, st1] = childrenBody(body, env, fn, st);
    if (defun && b.length === 1 && isForm(b[0]!, 'recv')) {
        const r = b[0] as Pair;
        const [cs, st2] = recvClauses(r, st1);
        const [clauses, st3] = mapSt(cs, st2, (c, s) => splitClause(c, env, fn, s));
        return [[form(r.pos, r.car, ...clauses)], st3];
    }
    return splitBody(b, env, fn, st1);
}

function splitClause(clause: Sexp, env: readonly string[], fn: string, st: St): [Sexp, St] {
    const items = toArray(clause);
    if (items === null || items.length < 2) return [clause, st];                     // codegen says what's wrong
    const [body, st1] = splitBody(items.slice(1), [...env, ...patternNames(items[0]!)], fn, st);
    return [form(clauseOf(clause), items[0]!, ...body), st1];
}

// --- the bodies of new processes ------------------------------------------

// The forms of a body with every fork's and connect's body split as a
// function of its own, env being the locals in scope.
function childrenBody(forms: readonly Sexp[], env: readonly string[], fn: string, st: St): [readonly Sexp[], St] {
    const out: Sexp[] = [];
    let e = env;
    for (const f of forms) {
        const [g, s] = children(f, e, fn, st);
        out.push(g);
        st = s;
        e = binds(f, e);
    }
    return [out, st];
}

function children(x: Sexp, env: readonly string[], fn: string, st: St): [Sexp, St] {
    if (x.t !== 'pair') return [x, st];
    const items = toArray(x);
    if (items === null) return [x, st];
    const head = x.car.t === 'sym' ? x.car.name : '';
    switch (head) {
        case 'quote': case 'defun':
            return [x, st];
        case 'lambda': {
            const body = items.slice(2);
            const r = body.map(firstRecv).find((p) => p !== undefined);
            if (r !== undefined) throw new CompileError("recv can't be in a lambda: a lambda can't wait for messages", r);
            const params = (toArray(items[1] ?? NIL) ?? []).filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
            const [b, st1] = childrenBody(body, [...env, ...params], fn, st);
            return [form(x.pos, ...items.slice(0, 2), ...b), st1];
        }
        case 'fork': case 'connect': {
            if (items.length < 2) return [x, st];
            const [args, st1] = mapSt(items.slice(1, -1), st, (a, s) => children(a, env, fn, s));
            const [body, st2] = splitFunction([items[items.length - 1]!], env, fn, false, st1);
            return [form(x.pos, x.car, ...args, body.length === 1 ? body[0]! : form(x.pos, sym('do', x.pos), ...body)), st2];
        }
        case 'recv': case 'cond': {
            const bound = (c: Sexp): readonly string[] => (head === 'recv' && c.t === 'pair' ? [...env, ...patternNames(c.car)] : env);
            const [clauses, st1] = mapSt(items.slice(1), st, (c, s) => {
                const parts = toArray(c);
                if (parts === null || parts.length === 0) return [c, s];
                const [test, s1] = head === 'cond' ? children(parts[0]!, env, fn, s) : [parts[0]!, s] as [Sexp, St];
                const [body, s2] = childrenBody(parts.slice(1), bound(c), fn, s1);
                return [form(clauseOf(c), test, ...body), s2];
            });
            return [form(x.pos, x.car, ...clauses), st1];
        }
        case 'do': {
            const [body, st1] = childrenBody(items.slice(1), env, fn, st);
            return [form(x.pos, x.car, ...body), st1];
        }
        default: {
            const [parts, st1] = mapSt(items, st, (a, s) => children(a, env, fn, s));
            return [form(x.pos, ...parts), st1];
        }
    }
}

// --- a body ----------------------------------------------------------------

// A body in tail position, with its first recv made the whole body of a
// receive function that the body tail-calls, and so on for the rest,
// which goes with it. env is the locals in scope.
function splitBody(forms: readonly Sexp[], env: readonly string[], fn: string, st: St): [readonly Sexp[], St] {
    const out: Sexp[] = [];
    let rest = forms;
    let e = env;
    while (rest.length > 0) {
        const [f, ...more] = rest;
        const last = more.length === 0;
        if (!ownRecv(f!)) {
            out.push(f!);
            e = binds(f!, e);
            rest = more;
            continue;
        }
        const x = f as Pair;
        const let_ = letParts(x);
        if (isForm(x, 'recv') || (let_ !== null && isForm(let_[1], 'recv'))) {
            const [r, t] = let_ === null ? [x, null] : [let_[1] as Pair, let_[0]];
            const [call, st1] = waitFor(r, t, more, e, fn, st);
            return [[...out, call], st1];
        }
        if (let_ !== null) {
            const [pre, value, st1] = lift(let_[1], st);
            rest = [...pre, form(x.pos, x.car, let_[0], value), ...more];
            st = st1;
            continue;
        }
        const head = x.car.t === 'sym' ? x.car.name : '';
        if (head === 'cond' || head === 'do' || head === 'yield') {
            if (head === 'do' && !last && !(toArray(x.cdr) ?? []).some((g) => isForm(g, 'let'))) {
                rest = [...(toArray(x.cdr) ?? []), ...more];        // just its forms, in order
                continue;
            }
            if (!last) throw notTail(head, x);
            const [g, st1] = head === 'cond' ? splitCond(x, e, fn, st) : splitInner(x, e, fn, st);
            return [[...out, g], st1];
        }
        const [pre, g, st1] = lift(x, st);
        rest = [...pre, g, ...more];
        st = st1;
    }
    return [out, st];
}

// A do's body or a yield's expression, in tail position.
function splitInner(x: Pair, env: readonly string[], fn: string, st: St): [Sexp, St] {
    const inner = toArray(x.cdr) ?? [];
    const [body, st1] = splitBody(inner, env, fn, st);
    if (isForm(x, 'do')) return [form(x.pos, x.car, ...body), st1];
    return [form(x.pos, x.car, body.length === 1 ? body[0]! : form(x.pos, sym('do', x.pos), ...body)), st1];
}

// A cond in tail position. Its clauses' bodies are in tail position too.
// A test with a recv in it is taken out: the clauses before it stay, and
// the rest go into a cond of their own, after the test is a let:
//
//     (cond (a x) ((p (recv)) y) (#true z))
//     (cond (a x) (#true (let t (p (recv))) (cond (t y) (#true z))))
function splitCond(x: Pair, env: readonly string[], fn: string, st: St): [Sexp, St] {
    const clauses = toArray(x.cdr) ?? [];
    const j = clauses.findIndex((c) => c.t === 'pair' && ownRecv(c.car));
    let cond: Pair = x;
    if (j >= 0) {
        const c = clauses[j] as Pair;
        const t = sym(`(test ${j + 1} of the cond at ${at(x.pos)})`, posOf(c));
        const rest = form(x.pos, x.car, form(clauseOf(c), t, ...(toArray(c.cdr) ?? [])), ...clauses.slice(j + 1));
        const last = list(sym('#true', posOf(c)), form(posOf(c), sym('let', posOf(c)), t, c.car), rest);
        cond = form(x.pos, x.car, ...clauses.slice(0, j), last) as Pair;
    }
    const [out, st1] = mapSt(toArray(cond.cdr) ?? [], st, (c, s) => {
        const parts = toArray(c);
        if (parts === null || parts.length < 2) return [c, s];                       // codegen says what's wrong
        const [body, s1] = splitBody(parts.slice(1), env, fn, s);
        return [form(clauseOf(c), parts[0]!, ...body), s1];
    });
    return [form(cond.pos, cond.car, ...out), st1];
}

// --- flattening ----------------------------------------------------------

// x, which has a recv in it, as lets to do first and what's left: the
// lets take out, in order, everything evaluated before its first recv,
// and then the recv itself.
function lift(x: Sexp, st: St): [readonly Sexp[], Sexp, St] {
    if (x.t !== 'pair') return [[], x, st];
    if (isForm(x, 'recv')) {
        const t = sym(`(${where(x.pos)})`, x.pos);
        return [[form(x.pos, sym('let', x.pos), t, x)], t, st];
    }
    const items = toArray(x) ?? [];
    const head = x.car.t === 'sym' ? x.car.name : null;
    if (head === 'cond' || head === 'yield') throw notTail(head, x);
    if (head === 'let') throw new CompileError('let is (let name expr), and only in a body', x.pos);
    if (head === 'do') {
        const forms = items.slice(1);
        if (forms.some((g) => isForm(g, 'let'))) throw notTail('do', x);
        return [forms.slice(0, -1), forms.length > 0 ? forms[forms.length - 1]! : NIL, st];
    }
    // a call (or connect, whose last argument is the new process's body):
    // the head and arguments are evaluated in order, so those before the
    // one with the recv are taken out first, unless they're names,
    // constants or lambdas, which come out the same whenever they're
    // evaluated.
    const from = head === null ? 0 : 1;
    const k    = items.findIndex((a, i) => i >= from && ownRecv(a) && !(head === 'connect' && i === items.length - 1));
    const pre: Sexp[] = [];
    const parts = items.map((a, i) => {
        if (i < from || i >= k || settled(a)) return a;
        const t = sym(`(argument ${i} of the call at ${at(x.pos)})`, posOf(a));
        pre.push(form(posOf(a), sym('let', posOf(a)), t, a));
        return t;
    });
    const [inner, value, st1] = lift(items[k]!, st);
    parts[k] = value;
    return [[...pre, ...inner], form(x.pos, ...parts), st1];
}

const settled = (x: Sexp): boolean => x.t !== 'pair' || isForm(x, 'quote') || isForm(x, 'lambda');

// --- waiting ---------------------------------------------------------------

// The call that replaces recv r and the forms after it (rest), and the
// receive function it calls. t is the name the recv's value is bound to,
// or null if it's dropped (or, with no rest, if it's the body's value).
function waitFor(r: Pair, t: Sym | null, rest: readonly Sexp[], env: readonly string[], fn: string, st: St): [Sexp, St] {
    const [clauses0, st0] = recvClauses(r, st);
    const site  = where(r.pos, st0.n);
    const st1   = { ...st0, n: st0.n + 1 };
    const argv  = sym(`@ARGV (carried across ${site})`, r.pos);
    const carry = (xs: readonly Sexp[]): readonly Sexp[] => (env.includes('@ARGV') ? xs.map((x) => rename(x, '@ARGV', argv)) : xs);
    const clauses = clauses0.map((c) => form(clauseOf(c), ...carry(toArray(c) ?? [])));
    const after   = carry(rest);

    const restUses   = uses(after, t === null ? [] : [t.name]);
    const clauseUses = new Set(clauses.flatMap((c) => {
        const parts = toArray(c) ?? [];
        return [...uses(parts.slice(1), patternNames(parts[0] ?? NIL))];
    }));
    const outer = [...new Set(env)].map((v) => (v === '@ARGV' ? argv.name : v));
    const live  = outer.filter((v) => restUses.has(v) || clauseUses.has(v));
    const bound = new Set(clauses.flatMap((c) => patternNames((c as Pair).car)));
    const alias = (v: string): string => (bound.has(v) ? `${v} (before ${site})` : v);
    const args  = live.map((v) => sym(v === argv.name ? '@ARGV' : v, r.pos));
    const name  = sym(`${fn} (${site})`, r.pos);

    // In a clause whose pattern doesn't bind it, a name the patterns bind
    // is bound again to what it was before.
    const again = (pattern: Sexp, used: Set<string>): readonly Sexp[] =>
        live.filter((v) => bound.has(v) && !patternNames(pattern).includes(v) && used.has(v))
            .map((v) => letForm(sym(v, r.pos), sym(alias(v), r.pos)));

    let bodies: readonly (readonly Sexp[])[];
    let st2: St = st1;
    if (after.length === 0) {
        bodies = clauses.map((c) => {
            const parts = toArray(c)!;
            return [...again(parts[0]!, uses(parts.slice(1), [])), ...parts.slice(1)];
        });
    } else if (clauses.length === 1) {
        // the rest comes after the clause's body, with the names its
        // pattern hid bound again
        const parts = toArray(clauses[0]!)!;
        const value = form(r.pos, sym('do', r.pos), ...parts.slice(1));
        const back  = live.filter((v) => bound.has(v) && v !== t?.name && restUses.has(v))
            .map((v) => letForm(sym(v, r.pos), sym(alias(v), r.pos)));
        bodies = [[t === null ? value : letForm(t, value), ...back, ...after]];
    } else {
        const kname = sym(`${fn} (after ${site})`, r.pos);
        const kLive = live.filter((v) => restUses.has(v));
        const kArgs = kLive.map((v) => sym(alias(v), r.pos));
        const [k, s] = makeFunction(kname, [...kLive, ...(t === null ? [] : [t.name])], null, after, env, fn, st2);
        st2 = { ...s, gen: [...s.gen, k] };
        bodies = clauses.map((c) => {
            const parts = toArray(c)!;
            const value = form(r.pos, sym('do', r.pos), ...parts.slice(1));
            const scope = [...live.map(alias), ...patternNames(parts[0]!)];
            const call  = (...extra: readonly Sexp[]): Sexp => {
                const all = [...kArgs, ...extra];
                return form(r.pos, kname, ...(all.length > MAX_ARGS ? [bundle(all, scope, r.pos)] : all));
            };
            return [...again(parts[0]!, uses(parts.slice(1), [])), ...(t === null ? [value, call()] : [call(value)])];
        });
    }
    const pats = clauses.map((c) => (c as Pair).car);
    const [g, st3] = makeFunction(name, live.map(alias), pats, bodies, env, fn, st2);
    return [form(r.pos, name, ...(live.length > MAX_ARGS ? [bundle(args, env, r.pos)] : args)), { ...st3, gen: [...st3.gen, g] }];
}

// (defun name (params...) body...), or with patterns, a receive function
// whose clauses have those patterns and bodies; its bodies split in turn.
// More than 8 parameters come as one list, taken apart at the start of
// each body.
function makeFunction(name: Sym, params: readonly string[], patterns: readonly Sexp[] | null,
                      bodies: readonly Sexp[] | readonly (readonly Sexp[])[], env: readonly string[], fn: string, st: St): [Sexp, St] {
    const pos      = name.pos;
    const bundled  = params.length > MAX_ARGS;
    const list_    = sym(`locals (${name.name})`, pos);
    const formals  = bundled ? [list_] : params.map((p) => sym(p, pos));
    const unpack   = bundled ? params.map((p, i) => letForm(sym(p, pos), nth(list_, i))) : [];
    const bodyEnv  = [...params, ...(bundled ? [list_.name] : [])];
    if (bundled) {
        const clash = [...params, ...(patterns ?? []).flatMap(patternNames)].find((v) => v === 'car' || v === 'cdr');
        if (clash !== undefined) throw tooMany(clash, pos);
    }
    if (patterns === null) {
        const [body, st1] = splitBody([...unpack, ...(bodies as readonly Sexp[])], bodyEnv, fn, st);
        return [form(pos, sym('defun', pos), name, list(...formals), ...body), st1];
    }
    const [clauses, st1] = mapSt(patterns, st, (p, s, i) => {
        const [body, s1] = splitBody([...unpack, ...(bodies as readonly (readonly Sexp[])[])[i]!], [...bodyEnv, ...patternNames(p)], fn, s);
        return [form(posOf(p), p, ...body), s1];
    });
    return [form(pos, sym('defun', pos), name, list(...formals), form(pos, sym('recv', pos), ...clauses)), st1];
}

// (cons a (cons b ... ())), for more than 8 locals.
function bundle(args: readonly Sexp[], env: readonly string[], pos: Pos | null): Sexp {
    if (env.includes('cons')) throw tooMany('cons', pos);
    return args.reduceRight<Sexp>((tail, a) => form(pos, sym('cons', pos), a, tail), NIL);
}

// (car (cdr (cdr ... xs))): element i.
function nth(xs: Sym, i: number): Sexp {
    let x: Sexp = xs;
    for (let n = 0; n < i; n++) x = form(xs.pos, sym('cdr', xs.pos), x);
    return form(xs.pos, sym('car', xs.pos), x);
}

const tooMany = (name: string, pos: Pos | null): CompileError =>
    new CompileError(`more than ${MAX_ARGS} locals go across this recv, as a list taken apart with car, cdr and cons, but a local is named ${name}`, pos);

// recv's clauses; (recv) has one that takes any message.
function recvClauses(r: Pair, st: St): [readonly Sexp[], St] {
    const clauses = toArray(r.cdr) ?? [];
    clauses.forEach((c) => {
        if (c.t !== 'pair' || c.cdr.t !== 'pair') throw new CompileError(`a recv clause is (pattern body...), not ${show(c)}`, posOf(c) ?? r.pos);
    });
    if (clauses.length > 0) return [clauses, st];
    const m = sym(`message (${where(r.pos, st.n)})`, r.pos);
    return [[list(m, m)], st];
}

// --- what a form binds and uses -------------------------------------------

// The locals after f, a form in a body.
function binds(f: Sexp, env: readonly string[]): readonly string[] {
    const l = letParts(f);
    return l === null ? env : [...env, l[0].name];
}

// The names a body uses that aren't bound in it (or in bound).
function uses(forms: readonly Sexp[], bound: readonly string[]): Set<string> {
    const out = new Set<string>();
    const body = (xs: readonly Sexp[], b: readonly string[]): void => {
        for (const x of xs) {
            const l = letParts(x);
            if (l !== null) {
                expr(l[1], b);
                b = [...b, l[0].name];
            } else {
                expr(x, b);
            }
        }
    };
    const expr = (x: Sexp, b: readonly string[]): void => {
        if (x.t === 'sym') {
            if (!b.includes(x.name)) out.add(x.name);
            return;
        }
        if (x.t !== 'pair') return;
        const items = toArray(x) ?? [];
        const head = x.car.t === 'sym' ? x.car.name : '';
        switch (head) {
            case 'quote': case 'defun':
                return;
            case 'lambda': {
                const params = (toArray(items[1] ?? NIL) ?? []).filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
                return body(items.slice(2), [...b, ...params]);
            }
            case 'do':
                return body(items.slice(1), b);
            case 'cond': case 'recv':
                for (const c of items.slice(1)) {
                    const parts = toArray(c) ?? [];
                    if (parts.length === 0) continue;
                    if (head === 'cond') expr(parts[0]!, b);
                    body(parts.slice(1), head === 'recv' ? [...b, ...patternNames(parts[0]!)] : b);
                }
                return;
            default:
                items.forEach((i) => expr(i, b));
        }
    };
    body(forms, bound);
    return out;
}

// Whether x has a recv of its own in it: not quoted, nor in a lambda, nor
// in the body of a new process.
function ownRecv(x: Sexp): boolean {
    if (x.t !== 'pair') return false;
    const head = x.car.t === 'sym' ? x.car.name : '';
    if (head === 'recv') return true;
    if (head === 'quote' || head === 'lambda' || head === 'defun' || head === 'fork') return false;
    const items = toArray(x) ?? [];
    return (head === 'connect' ? items.slice(1, -1) : items).some(ownRecv);
}

// Where x's first recv is, if it has one of its own (as ownRecv, but
// looking into lambdas, to say where one is that can't be).
function firstRecv(x: Sexp): Pos | null | undefined {
    if (x.t !== 'pair') return undefined;
    if (isForm(x, 'recv')) return x.pos;
    if (isForm(x, 'quote') || isForm(x, 'fork')) return undefined;
    const items = toArray(x) ?? [];
    for (const i of isForm(x, 'connect') ? items.slice(1, -1) : items) {
        const p = firstRecv(i);
        if (p !== undefined) return p;
    }
    return undefined;
}

// x with every name `from` made `to`, but not in quoted data.
function rename(x: Sexp, from: string, to: Sym): Sexp {
    if (x.t === 'sym') return x.name === from ? sym(to.name, x.pos) : x;
    if (x.t !== 'pair' || isForm(x, 'quote')) return x;
    return cons(rename(x.car, from, to), rename(x.cdr, from, to), x.pos);
}

// --- small things -----------------------------------------------------------

const isForm = (x: Sexp, name: string): boolean => x.t === 'pair' && x.car.t === 'sym' && x.car.name === name;

// The name and value of (let name value), or null.
function letParts(x: Sexp): readonly [Sym, Sexp] | null {
    const items = isForm(x, 'let') ? toArray(x) : null;
    return items !== null && items.length === 3 && items[1]!.t === 'sym' ? [items[1] as Sym, items[2]!] : null;
}

const letForm = (name: Sym, value: Sexp): Sexp => form(name.pos, sym('let', name.pos), name, value);

// A list whose first pair is at pos, so errors and faults point there.
function form(pos: Pos | null, ...items: readonly Sexp[]): Sexp {
    const xs = list(...items);
    return xs.t === 'pair' ? cons(xs.car, xs.cdr, pos) : xs;
}

const clauseOf = (c: Sexp): Pos | null => (c.t === 'pair' ? c.pos : null);

// "recv at file:line:col", for names; n when there's no position.
const where = (pos: Pos | null, n = 0): string => (pos === null ? `recv ${n}` : `recv at ${at(pos)}`);
const at    = (pos: Pos | null): string => (pos === null ? '?' : `${pos.file}:${pos.line}:${pos.col}`);

const notTail = (head: string, x: Pair): CompileError =>
    new CompileError(`a recv can only be in a ${head} that's in tail position${head === 'do' ? ', or one with no lets' : ''}; take the message first, with (let m (recv ...))`, x.pos);

// f over xs in order, threading the state.
function mapSt<T>(xs: readonly T[], st: St, f: (x: T, st: St, i: number) => [Sexp, St]): [readonly Sexp[], St] {
    const out: Sexp[] = [];
    xs.forEach((x, i) => {
        const [y, s] = f(x, st, i);
        out.push(y);
        st = s;
    });
    return [out, st];
}
