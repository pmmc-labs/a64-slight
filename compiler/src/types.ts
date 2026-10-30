// Types (docs/TYPES.md, D247): what the checker (check.ts) knows of a
// value. They describe slight's values as they are: structural, with
// unions, a keyword as a type of its own, and `any` where nothing is known.
//
// A list is () or conses: list<T> is any list of Ts, and a cons type is a
// list known to start with an element of its head type, the rest being of
// its tail type, itself a list type. So [A, B] is cons(A, cons(B, nil)),
// :tag(A) is cons(:tag, cons(A, nil)), and [A, ...list<T>] is
// cons(A, list(T)).
//
// The two questions the checker asks are whether two types overlap (some
// value is of both: meet isn't never), which is what a certain fault turns
// on, and whether one fits in another (every value of the one is of the
// other), which --strict asks. `any` overlaps and fits everything, both
// ways, so where nothing is known nothing is said.
//
// An alias stands for its body, with its parameters put in, and may refer
// to itself through a list, cons or tuple (Json does); every function that
// looks inside one unfolds it a step, and gives up past a depth (saying
// what's safe: that they overlap, or that it doesn't fit), so none loops.

import { CompileError } from './errors.ts';
import { displayName } from './modules.ts';
import { isForm, isSym, posOf, show as showSexp, toArray, TUPLE, type Sexp } from './sexp.ts';

export type Ty =
    | { readonly k: 'any' }
    | { readonly k: 'never' }
    | { readonly k: 'int' }
    | { readonly k: 'float' }
    | { readonly k: 'str' }
    | { readonly k: 'sym' }                                          // every symbol
    | { readonly k: 'lit'; readonly name: string }                   // one symbol: :ok, #true
    | { readonly k: 'nil' }
    | { readonly k: 'list'; readonly of: Ty }
    | { readonly k: 'cons'; readonly head: Ty; readonly tail: Ty }
    | { readonly k: 'tuple'; readonly of: readonly Ty[] | null }      // null: any tuple
    | { readonly k: 'array'; readonly of: Ty }                       // of numbers (D253): int, float, or num for either kind
    | { readonly k: 'fun'; readonly params: readonly Ty[] | null; readonly result: Ty }
    | { readonly k: 'task'; readonly of: Ty }
    | { readonly k: 'actor'; readonly msg: Ty }
    | { readonly k: 'address'; readonly msg: Ty }
    | { readonly k: 'pid' }                                          // any pid, a device's or an address included
    | { readonly k: 'or'; readonly of: readonly Ty[] }
    | { readonly k: 'var'; readonly name: string }
    | { readonly k: 'alias'; readonly name: string; readonly args: readonly Ty[] };

export const ANY: Ty   = { k: 'any' };
export const NEVER: Ty = { k: 'never' };
export const INT: Ty   = { k: 'int' };
export const FLOAT: Ty = { k: 'float' };
export const STR: Ty   = { k: 'str' };
export const SYM: Ty   = { k: 'sym' };
export const NIL_T: Ty = { k: 'nil' };
export const PID: Ty   = { k: 'pid' };
export const lit       = (name: string): Ty => ({ k: 'lit', name });
export const TRUE_T    = lit('#true');
export const FALSE_T   = lit('#false');
export const listOf    = (of: Ty): Ty => (of.k === 'never' ? NIL_T : { k: 'list', of });
export const consOf    = (head: Ty, tail: Ty): Ty =>
    (head.k === 'never' || tail.k === 'never' || tail.k === 'nil' && false) ? NEVER : { k: 'cons', head, tail };
export const tupleOf   = (of: readonly Ty[] | null): Ty => (of !== null && of.some((t) => t.k === 'never') ? NEVER : { k: 'tuple', of });
export const arrayOf   = (of: Ty): Ty => (of.k === 'never' ? NEVER : { k: 'array', of });
export const funOf     = (params: readonly Ty[] | null, result: Ty): Ty => ({ k: 'fun', params, result });
export const taskOf    = (of: Ty): Ty => ({ k: 'task', of });
export const actorOf   = (msg: Ty): Ty => ({ k: 'actor', msg });
export const addressOf = (msg: Ty): Ty => ({ k: 'address', msg });
export const LIST_ANY  = listOf(ANY);
export const CONS_ANY  = consOf(ANY, LIST_ANY);
export const TUPLE_ANY = tupleOf(null);
export const FUN_ANY   = funOf(null, ANY);

// A fixed list of these, then rest (nil: no more).
export const shape = (items: readonly Ty[], rest: Ty = NIL_T): Ty =>
    items.reduceRight<Ty>((tail, t) => consOf(t, tail), rest);
export const tagged = (tag: string, ...items: readonly Ty[]): Ty => shape([lit(tag), ...items]);

// --- aliases ------------------------------------------------------------------

// The aliases a program declares: an association list from name to its
// parameters and body.
export type Defs = { readonly name: string; readonly params: readonly string[]; readonly body: Ty; readonly next: Defs } | null;

export function findDef(defs: Defs, name: string): Defs {
    for (; defs !== null; defs = defs.next) if (defs.name === name) return defs;
    return null;
}

// An alias's body with its arguments put in for its parameters.
export function unfold(t: Ty, defs: Defs): Ty {
    if (t.k !== 'alias') return t;
    const d = findDef(defs, t.name);
    if (d === null) return ANY;
    const vars = d.params.map((p, i) => [p, t.args[i] ?? ANY] as const);
    return vars.length === 0 ? d.body : subst(d.body, vars);
}

// t with each type variable named in vars replaced.
export function subst(t: Ty, vars: readonly (readonly [string, Ty])[]): Ty {
    switch (t.k) {
        case 'var': {
            const v = vars.find(([n]) => n === t.name);
            return v === undefined ? t : v[1];
        }
        case 'list':    return listOf(subst(t.of, vars));
        case 'cons':    return consOf(subst(t.head, vars), subst(t.tail, vars));
        case 'tuple':   return tupleOf(t.of === null ? null : t.of.map((x) => subst(x, vars)));
        case 'array':   return arrayOf(subst(t.of, vars));
        case 'fun':     return funOf(t.params === null ? null : t.params.map((x) => subst(x, vars)), subst(t.result, vars));
        case 'task':    return taskOf(subst(t.of, vars));
        case 'actor':   return actorOf(subst(t.msg, vars));
        case 'address': return addressOf(subst(t.msg, vars));
        case 'or':      return union(...t.of.map((x) => subst(x, vars)));
        case 'alias':   return { k: 'alias', name: t.name, args: t.args.map((x) => subst(x, vars)) };
        default:        return t;
    }
}

// --- union ----------------------------------------------------------------------

export function same(a: Ty, b: Ty): boolean {
    if (a.k !== b.k) return false;
    switch (a.k) {
        case 'lit':     return a.name === (b as typeof a).name;
        case 'var':     return a.name === (b as typeof a).name;
        case 'list':    return same(a.of, (b as typeof a).of);
        case 'array':   return same(a.of, (b as typeof a).of);
        case 'cons':    return same(a.head, (b as typeof a).head) && same(a.tail, (b as typeof a).tail);
        case 'task':    return same(a.of, (b as typeof a).of);
        case 'actor':   return same(a.msg, (b as typeof a).msg);
        case 'address': return same(a.msg, (b as typeof a).msg);
        case 'tuple':   return sameAll(a.of, (b as typeof a).of);
        case 'fun':     return sameAll(a.params, (b as typeof a).params) && same(a.result, (b as typeof a).result);
        case 'or':      return sameAll(a.of, (b as typeof a).of);
        case 'alias':   return a.name === (b as typeof a).name && sameAll(a.args, (b as typeof a).args);
        default:        return true;
    }
}

function sameAll(a: readonly Ty[] | null, b: readonly Ty[] | null): boolean {
    if (a === null || b === null) return a === b;
    return a.length === b.length && a.every((x, i) => same(x, b[i]!));
}

// More members than this and a union is any: a type that big says little.
const MAX_MEMBERS = 64;

export function union(...ts: readonly Ty[]): Ty {
    const flat: Ty[] = [];
    const add = (t: Ty): void => {
        if (t.k === 'or') t.of.forEach(add);
        else if (t.k !== 'never' && !flat.some((x) => same(x, t))) flat.push(t);
    };
    ts.forEach(add);
    if (flat.some((t) => t.k === 'any')) return ANY;
    // lists of different elements are one list of either
    const lists = flat.filter((t) => t.k === 'list');
    let members = lists.length < 2 ? flat : [
        ...flat.filter((t) => t.k !== 'list'),
        listOf(union(...lists.map((t) => (t as { of: Ty }).of))),
    ];
    const has = (k: Ty['k']): boolean => members.some((t) => t.k === k);
    members = members.filter((t) =>
        !(t.k === 'lit' && has('sym'))
        && !(t.k === 'nil' && has('list'))
        && !((t.k === 'task' || t.k === 'actor' || t.k === 'address') && has('pid')));
    if (members.length > MAX_MEMBERS) return ANY;
    if (members.length === 0) return NEVER;
    return members.length === 1 ? members[0]! : { k: 'or', of: members };
}

export const NUM: Ty  = union(INT, FLOAT);
export const BOOL: Ty = union(TRUE_T, FALSE_T);

// --- meet: the values of both ----------------------------------------------------

const DEPTH = 24;

export function meet(a: Ty, b: Ty, defs: Defs, depth = 0): Ty {
    if (a.k === 'any' || a.k === 'var') return b.k === 'var' ? ANY : b;
    if (b.k === 'any' || b.k === 'var') return a;
    if (a.k === 'never' || b.k === 'never') return NEVER;
    if (a.k === 'or') return union(...a.of.map((x) => meet(x, b, defs, depth)));
    if (b.k === 'or') return union(...b.of.map((x) => meet(a, x, defs, depth)));
    if (a.k === 'alias' || b.k === 'alias') {
        if (a.k === 'alias' && b.k === 'alias' && same(a, b)) return a;
        if (depth > DEPTH) return a;    // as if they overlap: the safe side
        return meet(unfold(a, defs), unfold(b, defs), defs, depth + 1);
    }
    switch (a.k) {
        case 'int': case 'float': case 'str': case 'nil':
            if (b.k === a.k) return a;
            if (a.k === 'nil' && b.k === 'list') return a;
            return NEVER;
        case 'sym':
            return b.k === 'sym' || b.k === 'lit' ? b : NEVER;
        case 'lit':
            if (b.k === 'sym') return a;
            return b.k === 'lit' && b.name === a.name ? a : NEVER;
        case 'list':
            if (b.k === 'nil') return b;
            if (b.k === 'list') return listOf(meet(a.of, b.of, defs, depth + 1));
            if (b.k === 'cons') return consOf(meet(a.of, b.head, defs, depth + 1), meet(a, b.tail, defs, depth + 1));
            return NEVER;
        case 'cons':
            if (b.k === 'list') return meet(b, a, defs, depth);
            if (b.k === 'cons') return consOf(meet(a.head, b.head, defs, depth + 1), meet(a.tail, b.tail, defs, depth + 1));
            return NEVER;
        case 'tuple':
            if (b.k !== 'tuple') return NEVER;
            if (a.of === null) return b;
            if (b.of === null) return a;
            if (a.of.length !== b.of.length) return NEVER;
            return tupleOf(a.of.map((x, i) => meet(x, b.of![i]!, defs, depth + 1)));
        case 'array':
            return b.k === 'array' ? arrayOf(meet(a.of, b.of, defs, depth + 1)) : NEVER;
        case 'fun':
            if (b.k !== 'fun') return NEVER;
            if (a.params !== null && b.params !== null && a.params.length !== b.params.length) return NEVER;
            // nor one whose results are never the other's: a handler that
            // gives a string where a response is wanted (one that always
            // raises, giving never, goes anywhere)
            if (a.result.k !== 'never' && b.result.k !== 'never' && meet(a.result, b.result, defs, depth + 1).k === 'never') return NEVER;
            return a.params === null ? b : a;
        case 'pid':
            return b.k === 'pid' || b.k === 'task' || b.k === 'actor' || b.k === 'address' ? b : NEVER;
        case 'task': case 'actor': case 'address':
            // a pid's type says what it is, and how it's used: any two of
            // a kind can be the same pid
            return b.k === 'pid' || b.k === a.k ? a : NEVER;
    }
}

export const overlaps = (a: Ty, b: Ty, defs: Defs): boolean => meet(a, b, defs).k !== 'never';

// --- fits: every value of a is a b ------------------------------------------------

export function fits(a: Ty, b: Ty, defs: Defs, depth = 0): boolean {
    if (b.k === 'any' || a.k === 'any' || a.k === 'var' || b.k === 'var' || a.k === 'never') return true;
    if (a.k === 'or') return a.of.every((x) => fits(x, b, defs, depth));
    if (a.k === 'alias' || b.k === 'alias') {
        if (a.k === 'alias' && b.k === 'alias' && same(a, b)) return true;
        if (depth > DEPTH) return false;
        return fits(unfold(a, defs), unfold(b, defs), defs, depth + 1);
    }
    if (b.k === 'or') {
        if (b.of.some((x) => fits(a, x, defs, depth))) return true;
        // a list of a union fits a union of lists only member by member,
        // which isn't worth the search; a cons whose head is a union may
        if (a.k === 'cons' && a.head.k === 'or') return a.head.of.every((h) => fits(consOf(h, a.tail), b, defs, depth));
        if (a.k === 'list') return fits(NIL_T, b, defs, depth) && fits(consOf(a.of, a), b, defs, depth + 1);
        return false;
    }
    if (b.k === 'never') return false;
    switch (a.k) {
        case 'int': case 'float': case 'str': case 'sym': case 'pid':
            return b.k === a.k;
        case 'nil':
            return b.k === 'nil' || b.k === 'list';
        case 'lit':
            return b.k === 'sym' || (b.k === 'lit' && b.name === a.name);
        case 'list':
            return b.k === 'list' && fits(a.of, b.of, defs, depth + 1);
        case 'cons':
            if (b.k === 'list') return fits(a.head, b.of, defs, depth + 1) && fits(a.tail, b, defs, depth + 1);
            return b.k === 'cons' && fits(a.head, b.head, defs, depth + 1) && fits(a.tail, b.tail, defs, depth + 1);
        case 'tuple':
            if (b.k !== 'tuple') return false;
            if (b.of === null) return true;
            return a.of !== null && a.of.length === b.of.length && a.of.every((x, i) => fits(x, b.of![i]!, defs, depth + 1));
        case 'array':
            return b.k === 'array' && fits(a.of, b.of, defs, depth + 1);
        case 'fun':
            if (b.k !== 'fun') return false;
            if (b.params !== null && a.params !== null) {
                if (a.params.length !== b.params.length) return false;
                if (!b.params.every((p, i) => fits(p, a.params![i]!, defs, depth + 1))) return false;
            }
            return fits(a.result, b.result, defs, depth + 1);
        case 'task':
            return b.k === 'pid' || (b.k === 'task' && fits(a.of, b.of, defs, depth + 1));
        case 'actor':
            return b.k === 'pid' || (b.k === 'actor' && fits(b.msg, a.msg, defs, depth + 1));
        case 'address':
            return b.k === 'pid' || (b.k === 'address' && fits(b.msg, a.msg, defs, depth + 1));
    }
}

// --- parts of lists and tuples -------------------------------------------------------

// The first element of a value of type t, if it's a cons; the rest of it.
export function headOf(t: Ty, defs: Defs, depth = 0): Ty {
    switch (t.k) {
        case 'any':   return ANY;
        case 'cons':  return t.head;
        case 'list':  return t.of;
        case 'or':    return union(...t.of.map((x) => headOf(x, defs, depth)));
        case 'alias': return depth > DEPTH ? ANY : headOf(unfold(t, defs), defs, depth + 1);
        default:      return NEVER;
    }
}

export function tailOf(t: Ty, defs: Defs, depth = 0): Ty {
    switch (t.k) {
        case 'any':   return LIST_ANY;
        case 'cons':  return t.tail;
        case 'list':  return t;
        case 'or':    return union(...t.of.map((x) => tailOf(x, defs, depth)));
        case 'alias': return depth > DEPTH ? LIST_ANY : tailOf(unfold(t, defs), defs, depth + 1);
        default:      return NEVER;
    }
}

// Any element of a list of type t.
export function elementOf(t: Ty, defs: Defs, depth = 0): Ty {
    switch (t.k) {
        case 'any':   return ANY;
        case 'nil':   return NEVER;
        case 'list':  return t.of;
        case 'cons':  return union(t.head, elementOf(t.tail, defs, depth + 1));
        case 'or':    return union(...t.of.map((x) => elementOf(x, defs, depth)));
        case 'alias': return depth > DEPTH ? ANY : elementOf(unfold(t, defs), defs, depth + 1);
        default:      return NEVER;
    }
}

// The element at i of a tuple of type t; i null: any of them.
export function tupleElement(t: Ty, i: number | null, defs: Defs, depth = 0): Ty {
    switch (t.k) {
        case 'any':   return ANY;
        case 'tuple':
            if (t.of === null) return ANY;
            if (i === null) return union(...t.of);
            return t.of[i] ?? NEVER;
        case 'or':    return union(...t.of.map((x) => tupleElement(x, i, defs, depth)));
        case 'alias': return depth > DEPTH ? ANY : tupleElement(unfold(t, defs), i, defs, depth + 1);
        default:      return NEVER;
    }
}

// The members of t, as far as they can be told apart: aliases unfolded,
// a list as () or a cons, and a cons, or a tuple, whose part is a union
// as one of each. For matching patterns against, in check.ts. Past
// MAX_MEMBERS, t as it is.
export function members(t: Ty, defs: Defs, outer = 0): readonly Ty[] {
    const out: Ty[] = [];
    const go = (x: Ty, depth: number): boolean => {
        if (out.length > MAX_MEMBERS || depth > 8) return false;
        // a part's members only so far down: a type that refers to itself
        // would otherwise go on for ever
        if (outer > 3 && x.k !== 'or' && x.k !== 'alias') {
            out.push(x);
            return true;
        }
        switch (x.k) {
            case 'or':
                return x.of.every((m) => go(m, depth));
            case 'alias':
                return go(unfold(x, defs), depth + 1);
            case 'list':
                out.push(NIL_T);
                out.push(consOf(x.of, x));
                return true;
            case 'cons': {
                const heads = members(x.head, defs, outer + 1);
                const tails = x.tail.k === 'list' ? [x.tail] : members(x.tail, defs, outer + 1);
                if (heads.length * tails.length > MAX_MEMBERS) return false;
                heads.forEach((h) => tails.forEach((tl) => out.push(consOf(h, tl))));
                return true;
            }
            case 'tuple': {
                if (x.of === null) {
                    out.push(x);
                    return true;
                }
                let rows: Ty[][] = [[]];
                for (const e of x.of) {
                    const ms = members(e, defs, outer + 1);
                    rows = rows.flatMap((r) => ms.map((m) => [...r, m]));
                    if (rows.length > MAX_MEMBERS) return false;
                }
                rows.forEach((r) => out.push(tupleOf(r)));
                return true;
            }
            default:
                out.push(x);
                return true;
        }
    };
    return go(t, 0) ? out : [t];
}

// A type that a loop's results can't keep growing past: a cons chain
// becomes a list of its elements, and the same within; but a tagged list,
// :tag(A, B), short and ending, keeps its shape, which says what it is.
export function widen(t: Ty, defs: Defs): Ty {
    switch (t.k) {
        case 'cons': {
            const items: Ty[] = [];
            let rest: Ty = t;
            while (rest.k === 'cons' && items.length <= 8) {
                items.push(rest.head);
                rest = rest.tail;
            }
            if (rest.k === 'nil' && items[0]!.k === 'lit') return shape([items[0]!, ...items.slice(1).map((x) => widen(x, defs))]);
            return listOf(widen(elementOf(t, defs), defs));
        }
        case 'list': return listOf(widen(t.of, defs));
        case 'or':   return union(...t.of.map((x) => widen(x, defs)));
        default:     return t;
    }
}

// --- the type of a quoted datum ----------------------------------------------------

export function datumType(d: Sexp): Ty {
    switch (d.t) {
        case 'int':   return INT;
        case 'float': return FLOAT;
        case 'str':   return STR;
        case 'nil':   return NIL_T;
        case 'sym':   return lit(d.name);
        case 'pair': {
            const items = toArray(d);
            return items === null ? ANY : shape(items.map(datumType));
        }
    }
}

// --- types as they're written -------------------------------------------------------

// The names a type can be, other than an alias's or a type variable's.
const BASE: Readonly<Record<string, Ty>> = {
    any: ANY, never: NEVER, int: INT, float: FLOAT, num: NUM, str: STR, sym: SYM, bool: BOOL,
    nil: NIL_T, pid: PID, tuple: TUPLE_ANY, fun: FUN_ANY, array: arrayOf(NUM),
};

export const BASE_TYPES: readonly string[] = Object.keys(BASE);

// A type from its s-expression (docs/TYPES.md):
//   int float num str sym bool nil any never pid tuple fun
//   :ok #true #false ()             one value
//   (list T) (shape A B & T) (tuple A B) (fun (A B) C) (or A B)
//   array (array int) (array float)  an array of numbers, of either kind or one
//   (task T) (actor M) (address M)
//   (:tag A B & T)                  a tagged list
//   Name (Name T ...)               an alias
// vars are the type variables in scope; defs the aliases, for checking
// that a name is one and is given its parameters.
export function readType(x: Sexp, vars: readonly string[], defs: Defs): Ty {
    const bad = (what: string): CompileError => new CompileError(`${what}, not ${showSexp(x)}`, posOf(x));
    switch (x.t) {
        case 'nil': return NIL_T;
        case 'int': case 'float': case 'str': throw bad('a type is a name or a form');
        case 'sym': {
            if (x.name === '#true' || x.name === '#false') return lit(x.name);
            if (vars.includes(x.name)) return { k: 'var', name: x.name };
            const base = BASE[x.name];
            if (base !== undefined) return base;
            return aliasType(x.name, [], x, defs);
        }
        case 'pair': {
            if (isForm(x, 'quote')) {
                const items = toArray(x)!;
                if (items.length === 2 && items[1]!.t === 'sym') return lit(items[1]!.name);
                throw bad('a quoted type is a keyword');
            }
            const items = toArray(x);
            if (items === null) throw bad('a type is a name or a form');
            const [head, ...rest] = items;
            if (isForm(head!, 'quote')) return listShape([head!, ...rest], vars, defs, x);
            if (head!.t !== 'sym' || head!.name === TUPLE) throw bad('a type form starts with its name');
            const one = (): Ty => {
                if (rest.length !== 1) throw bad(`(${head!.name} T) takes one type`);
                return readType(rest[0]!, vars, defs);
            };
            switch (head!.name) {
                case 'list':    return listOf(one());
                case 'array':   return arrayOf(one());
                case 'task':    return taskOf(one());
                case 'actor':   return actorOf(one());
                case 'address': return addressOf(one());
                case 'shape':   return listShape(rest, vars, defs, x);
                case 'tuple':   return tupleOf(rest.map((r) => readType(r, vars, defs)));
                case 'or':      return union(...rest.map((r) => readType(r, vars, defs)));
                case 'fun': {
                    const params = rest.length === 2 ? toArray(rest[0]!) : null;
                    if (params === null) throw bad('a function type is (fun (A B) C)');
                    return funOf(params.map((p) => readType(p, vars, defs)), readType(rest[1]!, vars, defs));
                }
                default:
                    return aliasType(head!.name, rest.map((r) => readType(r, vars, defs)), x, defs);
            }
        }
    }
}

// The items of a fixed list, maybe with & and the rest's type.
function listShape(items: readonly Sexp[], vars: readonly string[], defs: Defs, x: Sexp): Ty {
    const at = items.findIndex((i) => isSym(i, '&'));
    if (at >= 0 && at !== items.length - 2) throw new CompileError(`& comes before the last type of a list, not ${showSexp(x)}`, posOf(x));
    const fixed = (at < 0 ? items : items.slice(0, at)).map((i) => readType(i, vars, defs));
    const rest  = at < 0 ? NIL_T : readType(items[at + 1]!, vars, defs);
    return shape(fixed, rest);
}

function aliasType(name: string, args: readonly Ty[], x: Sexp, defs: Defs): Ty {
    const d = findDef(defs, name);
    if (d === null) throw new CompileError(`${name} isn't a type`, posOf(x));
    if (d.params.length !== args.length) {
        throw new CompileError(`${name} takes ${d.params.length} type${d.params.length === 1 ? '' : 's'}, not ${args.length}`, posOf(x));
    }
    return { k: 'alias', name, args };
}

// --- showing types --------------------------------------------------------------------

// A name as the .sly syntax writes it (D246): / as ::, -> as _to_, - as _.
const SLY_OPERATORS: Readonly<Record<string, string>> = {
    '$$': 'self', '^$$': 'parent', '~': '++', 'eq?': '==', 'ne?': '!=', 'not': '!', '==': '`==`', '!=': '`!=`',
};

export function slyName(name: string): string {
    const op = SLY_OPERATORS[name];
    if (op !== undefined) return op;
    const word = [...name].some((c) => (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    if (!word) return name;
    return name.split('->').join('_to_').split('/').join('::').split('-').join('_');
}

// t as the program's own syntax writes it: sly for a .sly file.
export function showType(t: Ty, sly: boolean): string {
    const s = (x: Ty): string => showType(x, sly);
    const keyword = (name: string): string =>
        name === '#true' ? (sly ? 'true' : '#true') : name === '#false' ? (sly ? 'false' : '#false') : `:${name}`;
    switch (t.k) {
        case 'any': case 'never': case 'int': case 'float': case 'str': case 'sym': case 'pid':
            return t.k;
        case 'nil':     return sly ? '[]' : '()';
        case 'lit':     return keyword(t.name);
        case 'var':     return t.name;
        case 'list':    return sly ? `list<${s(t.of)}>` : `(list ${s(t.of)})`;
        case 'task':    return sly ? `task<${s(t.of)}>` : `(task ${s(t.of)})`;
        case 'actor':   return sly ? `actor<${s(t.msg)}>` : `(actor ${s(t.msg)})`;
        case 'address': return sly ? `address<${s(t.msg)}>` : `(address ${s(t.msg)})`;
        case 'array':
            if (same(t.of, NUM)) return 'array';
            return sly ? `array<${s(t.of)}>` : `(array ${s(t.of)})`;
        case 'tuple':
            if (t.of === null) return 'tuple';
            return sly ? `#(${t.of.map(s).join(', ')})` : `(tuple ${t.of.map(s).join(' ')})`;
        case 'fun': {
            if (t.params === null) return t.result.k === 'any' ? 'fun' : (sly ? `fun(...) -> ${s(t.result)}` : `(fun (...) ${s(t.result)})`);
            return sly ? `fun(${t.params.map(s).join(', ')}) -> ${s(t.result)}` : `(fun (${t.params.map(s).join(' ')}) ${s(t.result)})`;
        }
        case 'or': {
            if (t.of.length === 2 && t.of.some((x) => same(x, TRUE_T)) && t.of.some((x) => same(x, FALSE_T))) return 'bool';
            if (t.of.length === 2 && t.of.some((x) => x.k === 'int') && t.of.some((x) => x.k === 'float')) return 'num';
            return sly ? t.of.map(s).join(' | ') : `(or ${t.of.map(s).join(' ')})`;
        }
        case 'alias': {
            const n = displayName(t.name);
            if (t.args.length === 0) return n;
            return sly ? `${n}<${t.args.map(s).join(', ')}>` : `(${n} ${t.args.map(s).join(' ')})`;
        }
        case 'cons': {
            const items: Ty[] = [];
            let rest: Ty = t;
            while (rest.k === 'cons') {
                items.push(rest.head);
                rest = rest.tail;
            }
            const tail = rest.k === 'nil' ? null : rest;
            const first = items[0]!;
            if (first.k === 'lit' && first.name !== '#true' && first.name !== '#false') {
                const parts = items.slice(1).map(s);
                if (sly) return `:${first.name}(${[...parts, ...(tail === null ? [] : [`...${s(tail)}`])].join(', ')})`;
                return `(:${first.name}${parts.map((p) => ` ${p}`).join('')}${tail === null ? '' : ` & ${s(tail)}`})`;
            }
            if (sly) return `[${[...items.map(s), ...(tail === null ? [] : [`...${s(tail)}`])].join(', ')}]`;
            return `(shape ${items.map(s).join(' ')}${tail === null ? '' : ` & ${s(tail)}`})`;
        }
    }
}
