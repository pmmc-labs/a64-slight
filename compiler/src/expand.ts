// The expander: what the reader's forms become before they're compiled.
//
//   - (@include "path/file.slight") and (@include :name) splice in, where
//     they stand, the forms of another file: a path is relative to the
//     file it's in; :name is lib/name.slight. Only at the top level. A file
//     is included once, however often it's asked for (the loader's key, its
//     real path, says which file it is); a file that includes itself,
//     directly or not, is an error. Every program starts with an unwritten
//     (@include :prelude), whose forms the compiler keeps apart (D80).
//
//   - if, when, case, and and or become cond, as ts-slight's expander made
//     them into if:
//
//       (if test then else)    (cond (test then) (#true else))
//       (if test then)         (cond (test then) (#true ()))
//       (when test body...)    (cond (test body...) (#true ()))
//       (case topic            (do (let t topic)
//           (value body...)        (cond ((eq? t value) body...)
//           (#true body...))             (#true body...)))
//       (and a b ... z)        (cond (a (and b ... z)) (#true #false)), (and z) is z, (and) is #true
//       (or a b ... z)         (cond (a #true) (b #true) ... (#true z)), (or z) is z, (or) is #false
//
//     A case with no #true clause gives () when nothing matches. Its topic's
//     name has spaces in it, so no program can write it. and and or stop
//     at the first operand that decides; as in Scheme, the last one isn't
//     tested, and is what they give if they get to it. The others are cond
//     tests, so each must be #true or #false.
//
// It knows just enough of the special forms to leave alone what isn't an
// expression: quoted data, the parameters of defun and lambda, and recv's
// patterns. (Names are atoms, which it leaves alone anyway.)

import { CompileError } from './errors.ts';
import { read } from './reader.ts';
import { NIL, cons, list, show, sym, toArray, type Pair, type Pos, type Sexp } from './sexp.ts';

// The outside world: a key for the file at path that's the same however
// it's named (its real path), or null if there's no such file; and the
// text of the file with that key. A file already included isn't read
// again.
export type Loader = {
    readonly key:  (path: string) => string | null;
    readonly text: (key: string) => string;
};

export type Expanded = { readonly prelude: Sexp; readonly program: Sexp };

// Where a file is read from, and the name its positions and messages use.
type Source = { readonly path: string; readonly name: string };

// A file being included, for spotting cycles.
type Open = { readonly name: string; readonly key: string };

type Cx = { readonly lib: string; readonly load: Loader };

// The prelude, then the files in order, each with what it includes. lib is
// the directory with the built-in files (lib/name.slight).
export function expandProgram(files: readonly string[], lib: string, load: Loader): Expanded {
    const cx: Cx = { lib, load };
    const [prelude, done] = includeFile(builtin('prelude', lib), null, cx, [], []);
    const [program] = files.reduce<[readonly Sexp[], readonly string[]]>(([acc, d], file) => {
        const [forms, d1] = includeFile({ path: file, name: file }, null, cx, [], d);
        return [[...acc, ...forms], d1];
    }, [[], done]);
    return { prelude: list(...prelude), program: list(...program) };
}

const builtin = (name: string, lib: string): Source => ({ path: `${lib}/${name}.slight`, name: `lib/${name}.slight` });

// The forms of src, with its includes spliced in, unless it's in done
// already. stack holds the files being included, outermost first.
function includeFile(src: Source, from: Pos | null, cx: Cx, stack: readonly Open[],
                     done: readonly string[]): [readonly Sexp[], readonly string[]] {
    const key = cx.load.key(src.path);
    if (key === null) throw new CompileError(`can't read ${src.name}`, from);
    if (done.includes(key)) return [[], done];
    const path = [...stack, { name: src.name, key }];
    if (stack.some((o) => o.key === key)) {
        throw new CompileError(`including ${src.name} makes a cycle: ${path.map((o) => o.name).join(' -> ')}`, from);
    }
    const out: Sexp[] = [];
    let d = done;
    for (let xs = read(cx.load.text(key), src.name); xs.t === 'pair'; xs = xs.cdr) {
        const x = xs.car;
        if (headIs(x, '@include')) {
            const [forms, d1] = includeFile(target(x as Pair, src, cx.lib), (x as Pair).pos, cx, path, d);
            out.push(...forms);
            d = d1;
        } else {
            out.push(expand(x));
        }
    }
    return [out, [...d, key]];
}

// What (@include x) in src asks for.
function target(x: Pair, src: Source, lib: string): Source {
    const args = toArray(x.cdr) ?? [];
    if (args.length !== 1) throw new CompileError(`@include takes 1 argument, not ${args.length}`, x.pos);
    const arg = args[0]!;
    if (arg.t === 'str') return { path: relative(src.path, arg.v), name: relative(src.name, arg.v) };
    const name = keyword(arg);
    if (name !== null) return builtin(name, lib);
    throw new CompileError(`@include takes a path ("file.slight") or a built-in (:fs), not ${show(arg)}`, x.pos);
}

// path, relative to the directory of the file at from (unless it starts
// with /), with its . and .. steps taken.
export function relative(from: string, path: string): string {
    const dir   = from.lastIndexOf('/') < 0 ? '' : from.slice(0, from.lastIndexOf('/') + 1);
    const steps = (path.startsWith('/') ? path : dir + path).split('/');
    const out: string[] = [];
    for (const [i, s] of steps.entries()) {
        if (s === '.' || (s === '' && i > 0)) continue;
        if (s === '..' && out.length > 0 && out[out.length - 1] !== '..' && out[out.length - 1] !== '') out.pop();
        else out.push(s);
    }
    return out.join('/');
}

const headIs = (x: Sexp, name: string): boolean => x.t === 'pair' && x.car.t === 'sym' && x.car.name === name;

// The name in (quote name), or null.
function keyword(x: Sexp): string | null {
    const items = headIs(x, 'quote') ? toArray(x) : null;
    return items !== null && items.length === 2 && items[1]!.t === 'sym' ? items[1]!.name : null;
}

// --- the forms that become cond -----------------------------------------------

// A list whose first pair is at pos, so errors and faults point there.
function form(pos: Pos | null, ...items: readonly Sexp[]): Sexp {
    const xs = list(...items);
    return xs.t === 'pair' ? cons(xs.car, xs.cdr, pos) : xs;
}

// f over the elements of a list, keeping its positions.
function mapList(xs: Sexp, f: (x: Sexp) => Sexp): Sexp {
    if (xs.t !== 'pair') return xs;
    return cons(f(xs.car), mapList(xs.cdr, f), xs.pos);
}

// The first n elements as they are, and the rest expanded.
function after(xs: Sexp, n: number): Sexp {
    if (xs.t !== 'pair') return xs;
    return n > 0 ? cons(xs.car, after(xs.cdr, n - 1), xs.pos) : mapList(xs, expand);
}

export function expand(x: Sexp): Sexp {
    if (x.t !== 'pair') return x;
    if (x.car.t !== 'sym') return mapList(x, expand);
    switch (x.car.name) {
        case 'quote':    return x;
        case '@include': throw new CompileError('@include is only allowed at the top level', x.pos);
        case 'if':       return expand(expandIf(x));
        case 'when':     return expand(expandWhen(x));
        case 'case':     return expand(expandCase(x));
        case 'and':      return expand(expandAnd(x));
        case 'or':       return expand(expandOr(x));
        case 'defun':    return after(x, 3);    // (defun name params body...)
        case 'lambda':   return after(x, 2);    // (lambda params body...)
        case 'cond':     return cons(x.car, mapList(x.cdr, (c) => mapList(c, expand)), x.pos);
        case 'recv':     return cons(x.car, mapList(x.cdr, (c) => after(c, 1)), x.pos);
        default:         return mapList(x, expand);
    }
}

const TRUE  = sym('#true');
const FALSE = sym('#false');

function args(x: Pair, name: string, min: number, max: number, what: string): readonly Sexp[] {
    const items = toArray(x.cdr);
    if (items === null || items.length < min || items.length > max) {
        throw new CompileError(`${name} takes ${what}, not ${show(x)}`, x.pos);
    }
    return items;
}

function expandIf(x: Pair): Sexp {
    const [test, then, otherwise] = args(x, 'if', 2, 3, 'a test, a then and maybe an else');
    return form(x.pos, sym('cond', x.pos), list(test!, then!), list(TRUE, otherwise ?? NIL));
}

function expandWhen(x: Pair): Sexp {
    const [test, ...body] = args(x, 'when', 2, Infinity, 'a test and a body');
    return form(x.pos, sym('cond', x.pos), list(test!, ...body), list(TRUE, NIL));
}

function expandCase(x: Pair): Sexp {
    const [topic, ...clauses] = args(x, 'case', 1, Infinity, 'a topic and clauses');
    const where = x.pos === null ? '' : ` ${x.pos.file}:${x.pos.line}:${x.pos.col}`;
    const t     = sym(`case topic${where}`, x.pos);
    const tests = clauses.map((c) => {
        const items = toArray(c);
        if (items === null || items.length < 2) throw new CompileError(`a case clause is (value body...), not ${show(c)}`, x.pos);
        const [value, ...body] = items;
        const test = value!.t === 'sym' && (value!.name === '#true' || value!.name === '#false') ? value! : form(x.pos, sym('eq?'), t, value!);
        return list(test, ...body);
    });
    const last     = clauses.length > 0 ? toArray(clauses[clauses.length - 1]!)![0]! : null;
    const fallback = last !== null && last.t === 'sym' && last.name === '#true' ? [] : [list(TRUE, NIL)];
    return form(x.pos, sym('do', x.pos), form(x.pos, sym('let', x.pos), t, topic!), form(x.pos, sym('cond', x.pos), ...tests, ...fallback));
}

function expandAnd(x: Pair): Sexp {
    const operands = args(x, 'and', 0, Infinity, 'operands');
    if (operands.length === 0) return TRUE;
    const [a, ...rest] = operands;
    if (rest.length === 0) return a!;
    return form(x.pos, sym('cond', x.pos), list(a!, form(x.pos, sym('and', x.pos), ...rest)), list(TRUE, FALSE));
}

function expandOr(x: Pair): Sexp {
    const operands = args(x, 'or', 0, Infinity, 'operands');
    if (operands.length === 0) return FALSE;
    const tested = operands.slice(0, -1).map((a) => list(a, TRUE));
    if (tested.length === 0) return operands[0]!;
    return form(x.pos, sym('cond', x.pos), ...tested, list(TRUE, operands[operands.length - 1]!));
}
