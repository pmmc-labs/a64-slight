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
// (cons? x), (nil? x), and (eq? x literal); the one other is a list
// pattern's test that a value is a list, for (& rest). They, and the
// paths, call the builtins by names that no local can hide (BUILTIN), so
// a pattern can bind car, say. A path's steps are on what the tests before
// it have shown to be conses, so its c[ad]r needn't check (ON_CONSES).

import { CompileError } from './errors.ts';
import { list, posOf, show, sym, toArray, type Pos, type Sexp, type Sym } from './sexp.ts';

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

// steps: the car and cdr steps from v to the part of the value p is
// matched against, in the order they're taken.
function lower(p: Sexp, v: Sexp, steps: readonly ('a' | 'd')[], whole: Sexp,
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
            if (items === null) throw bad('a pattern is a name, a literal or a list of patterns', p, whole);
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

// v with the steps taken, four at a time, as c[ad]r's letters: the last
// step taken is the first letter.
function path(v: Sexp, steps: readonly ('a' | 'd')[]): Sexp {
    let x = v;
    for (let i = 0; i < steps.length; i += 4) {
        const chunk = steps.slice(i, i + 4);
        x = list(sym(`c${[...chunk].reverse().join('')}r${ON_CONSES}`), x);
    }
    return x;
}

function bad(what: string, p: Sexp, whole: Sexp): CompileError {
    const pos: Pos | null = posOf(p) ?? posOf(whole);
    return new CompileError(`${what}, not ${show(p)}`, pos);
}
