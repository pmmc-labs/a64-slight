// Patterns, for match and recv (D168): what one is, the names it binds,
// and the tests and bindings that match a value against it.
//
//   _                  anything, binding nothing
//   name               anything, bound to name
//   a literal          a value eq? to it: an integer, a float, a string,
//                      #true, #false, (), or a quoted datum (:tag, 'x, '(1 2))
//   (p ...)            a list of exactly that many elements, each matching
//                      its pattern
//   (p ... & rest)     a list of at least that many; what follows them (a
//                      list, maybe ()) matches rest
//   #(p ...)           a tuple of exactly that many elements, each matching
//                      its pattern (D176); the reader makes it (TUPLE p ...)
//
// A name can be bound once in a pattern. There are no guards: match the
// shape, and test the rest in the body.
//
// A pattern becomes tests, which are true when the value matches, and
// bindings, each a name and the expression for its part of the value, as
// car and cdr of the value's expression. The expander makes match into
// cond with them (expand.ts), and the code generator does recv's clauses
// with them (codegen.ts), so the two can't come to mean different things.
// The tests are of the shapes the code generator branches on directly:
// (cons? x), (nil? x), and (eq? x literal); the others are a list
// pattern's test that a value is a list, for (& rest), and a tuple
// pattern's (tuple? x), before its length is compared. They, and the
// paths, call the builtins by names that no local can hide (BUILTIN), so
// a pattern can bind car, say. A path's steps are on what the tests before
// it have shown to be conses, so its c[ad]r needn't check (ON_CONSES); a
// tuple's element is a tuple/get, on a tuple the tests have shown is long
// enough.

import { CompileError } from './errors.ts';
import { int, isForm, isSym, list, posOf, show, sym, toArray, TUPLE, type Pos, type Sexp, type Sym } from './sexp.ts';

export type Lowered = {
    readonly tests: readonly Sexp[];
    readonly binds: readonly (readonly [Sym, Sexp])[];
};

// The rest marker in a list pattern.
export const REST = '&';

// Names with spaces in them, which no program can write. A builtin's name
// with BUILTIN after it is that builtin, whatever locals there are; the
// code generator knows these and NO_MATCH, a match that matched nothing
// (expand.ts). RECV_MESSAGE is what recv names the message it took.
export const BUILTIN      = ' (builtin)';
export const ON_CONSES    = ' (on conses)';
export const NO_MATCH     = 'no match (match)';
export const RECV_MESSAGE = 'message (recv)';

const prim = (name: string): Sexp => sym(`${name}${BUILTIN}`);

// The tests and bindings for the value of expression v to match pattern p.
export function lowerPattern(p: Sexp, v: Sexp): Lowered {
    const tests: Sexp[] = [];
    const binds: (readonly [Sym, Sexp])[] = [];
    lower(p, v, [], p, tests, binds);
    binds.forEach(([name], i) => {
        if (binds.findIndex(([n]) => n.name === name.name) !== i) {
            throw new CompileError(`${name.name} is in the pattern twice`, name.pos ?? posOf(p));
        }
    });
    return { tests, binds };
}

// The names a pattern binds, in order.
export const patternNames = (p: Sexp): readonly string[] => lowerPattern(p, sym('value')).binds.map(([name]) => name.name);

// A step from a value to a part of it: its car, its cdr, or a tuple's
// element at an index.
type Step = 'a' | 'd' | number;

// steps: the steps from v to the part of the value p is matched against,
// in the order they're taken.
function lower(p: Sexp, v: Sexp, steps: readonly Step[], whole: Sexp,
               tests: Sexp[], binds: (readonly [Sym, Sexp])[]): void {
    const here = path(v, steps);
    switch (p.t) {
        case 'int': case 'float': case 'str':
            tests.push(list(prim('eq?'), here, p));
            return;
        case 'nil':
            tests.push(list(prim('nil?'), here));
            return;
        case 'sym':
            if (p.name === '#true' || p.name === '#false') tests.push(list(prim('eq?'), here, p));
            else if (p.name === REST) throw bad(`${REST} can only come before the last pattern of a list`, p, whole);
            else if (p.name !== '_') binds.push([p, here]);
            return;
        case 'pair': {
            const items = toArray(p);
            if (items === null) throw bad('a pattern is a name, a literal, or a list or tuple of patterns', p, whole);
            if (isForm(p, TUPLE)) {
                const elements = items.slice(1);
                if (elements.some((x) => isSym(x, REST))) throw bad(`${REST} can't be in a tuple pattern`, p, whole);
                tests.push(list(prim('tuple?'), here));
                tests.push(list(prim('eq?'), list(prim('tuple/len'), here), int(BigInt(elements.length))));
                elements.forEach((e, i) => lower(e, v, [...steps, i], whole, tests, binds));
                return;
            }
            if (items[0]!.t === 'sym' && items[0]!.name === 'quote') {
                if (items.length !== 2) throw bad('quote takes 1 argument', p, whole);
                tests.push(list(prim('eq?'), here, p));
                return;
            }
            const at = items.findIndex((x) => x.t === 'sym' && x.name === REST);
            if (at >= 0 && at !== items.length - 2) {
                throw bad(`${REST} can only come before the last pattern of a list`, p, whole);
            }
            const elements = at < 0 ? items : items.slice(0, at);
            if (at === 0) tests.push(isList(here));
            let rest = steps;
            for (const e of elements) {
                tests.push(list(prim('cons?'), path(v, rest)));
                lower(e, v, [...rest, 'a'], whole, tests, binds);
                rest = [...rest, 'd'];
            }
            if (at < 0) tests.push(list(prim('nil?'), path(v, rest)));
            else lower(items[at + 1]!, v, rest, whole, tests, binds);
            return;
        }
    }
}

// (or (nil? x) (cons? x)), as cond: lists are proper, so a list pattern
// with elements before its rest needs only its conses tested.
const isList = (x: Sexp): Sexp =>
    list(sym('cond'), list(list(prim('nil?'), x), sym('#true')), list(sym('#true'), list(prim('cons?'), x)));

// v with the steps taken: a run of car and cdr steps four at a time, as
// c[ad]r's letters (the last step taken is the first letter), and a
// tuple's element as tuple/get.
function path(v: Sexp, steps: readonly Step[]): Sexp {
    let x = v;
    let i = 0;
    while (i < steps.length) {
        const s = steps[i]!;
        if (typeof s === 'number') {
            x = list(prim('tuple/get'), x, int(BigInt(s)));
            i += 1;
            continue;
        }
        let j = i;
        while (j < steps.length && j < i + 4 && typeof steps[j] !== 'number') j += 1;
        x = list(sym(`c${steps.slice(i, j).reverse().join('')}r${ON_CONSES}`), x);
        i = j;
    }
    return x;
}

function bad(what: string, p: Sexp, whole: Sexp): CompileError {
    const pos: Pos | null = posOf(p) ?? posOf(whole);
    return new CompileError(`${what}, not ${show(p)}`, pos);
}
