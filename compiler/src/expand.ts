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
//   - if, when, case, and and or become cond, so the compiler, the recv
//     rule and the tail calls see only cond (D142):
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
//   - match becomes cond too (D168), its patterns the tests and lets that
//     pattern.ts makes of them:
//
//       (match topic               (do (let t topic)
//           ((:move x y) body...)      (cond ((and (cons? t) (eq? (car t) :move) ...)
//           ...)                             (let x (cadr t)) (let y (caddr t)) body...)
//                                            ...
//                                            (#true (no-match t))))
//
//     A match that matches nothing faults (:no-match), with the topic;
//     the last clause doesn't need it when its pattern takes anything.
//
// It knows just enough of the special forms to leave alone what isn't an
// expression: quoted data, the parameters of defun and lambda, and the
// patterns of recv and match. (Names are atoms, which it leaves alone
// anyway.) So a tuple pattern, #(p ...), that it comes to is in an
// expression, where it means nothing yet (D176): an error.

import { CompileError } from './errors.ts';
import { link, type Unit, type UnitKind } from './modules.ts';
import { lowerPattern, NO_MATCH } from './pattern.ts';
import { read } from './reader.ts';
import { readSly } from './sly.ts';
import { NIL, cons, form, isForm, isSym, list, posOf, show, sym, toArray, TUPLE, type Pair, type Pos, type Sexp, type Sym } from './sexp.ts';

// The outside world: a key for the file at path that's the same however
// it's named (its real path), or null if there's no such file; and the
// text of the file with that key. A file already included isn't read
// again.
export type Loader = {
    readonly key:  (path: string) => string | null;
    readonly text: (key: string) => string;
};

// modules: the files of the modules a program includes, headers and
// what they splice in (D248), whose insides --strict leaves to their own
// checks.
export type Expanded = { readonly prelude: Sexp; readonly program: Sexp; readonly modules: readonly string[] };

// Where a file is read from, and the name its positions and messages use.
type Source = { readonly path: string; readonly name: string };

// A file being included, for spotting cycles.
type Open = { readonly name: string; readonly key: string };

type Cx = { readonly lib: string; readonly load: Loader; readonly keepMatch: boolean };

// The prelude, then the files in order, each with what it includes, as
// units (D248): the prelude, the program, and each file with a header,
// linked into the prelude's forms and the program's (modules.ts). lib is
// the directory with the built-in files (lib/name.slight). keepMatch:
// leave match as it is, for the checker (check.ts), which expand() then
// lowers.
export function expandProgram(files: readonly string[], lib: string, load: Loader, keepMatch = false): Expanded {
    const cx: Cx = { lib, load, keepMatch };
    const units: Unit[] = [];
    readUnit([builtin('prelude', lib)], 'prelude', null, cx, [], units);
    readUnit(files.map((file) => ({ path: file, name: file })), 'program', null, cx, [], units);
    return link(units);
}

const builtin = (name: string, lib: string): Source => ({ path: `${lib}/${name}.slight`, name: `lib/${name}.slight` });

const keyOf = (src: Source, from: Pos | null, cx: Cx): string => {
    const key = cx.load.key(src.path);
    if (key === null) throw new CompileError(`can't read ${src.name}`, from);
    return key;
};

// A unit's files, with what they splice in, and its header, if it has
// one; added to units once it's read, after the modules it includes.
function readUnit(srcs: readonly Source[], kind: UnitKind, from: Pos | null, cx: Cx, stack: readonly Open[], units: Unit[]): string {
    const keys = srcs.map((src) => keyOf(src, from, cx));
    const key = kind === 'program' ? `the program: ${keys.join(' ')}` : keys[0]!;
    const done: string[] = [];
    const imports: string[] = [];
    const forms = srcs.flatMap((src) => spliceFile(src, from, cx, stack, done, imports, units));
    // a program's first file's header too, so a module checked on its own
    // is checked against what it says
    const h = kind === 'program' && units.some((u) => u.key === keys[0]) ? null : headerOf(srcs[0]!, cx);
    const [header, reexports] = h === null ? [null, []] : readHeader(h, cx, [...stack, { name: srcs[0]!.name, key }], units);
    if (kind === 'module') {
        const loose = forms.find((f) => !isForm(f, 'defun') && !isForm(f, 'sig') && !isForm(f, 'deftype'));
        if (loose !== undefined) {
            throw new CompileError(`${srcs[0]!.name} has a header, so it's a module: it can only define functions and types`, posOf(loose));
        }
    }
    units.push({
        key, kind, forms, header, reexports,
        name: kind === 'program' ? 'the program' : srcs[0]!.name,
        imports: [...imports, ...reexports.filter((r) => !imports.includes(r))],
    });
    return key;
}

// A file's forms, with its includes spliced in, unless it's in done (this
// unit's) already; a module it includes, a file with a header, is read as
// a unit of its own, once, and goes in imports. stack holds the files
// being included, outermost first.
function spliceFile(src: Source, from: Pos | null, cx: Cx, stack: readonly Open[],
                    done: string[], imports: string[], units: Unit[]): readonly Sexp[] {
    const key = keyOf(src, from, cx);
    const path = [...stack, { name: src.name, key }];
    if (stack.some((o) => o.key === key)) {
        throw new CompileError(`including ${src.name} makes a cycle: ${path.map((o) => o.name).join(' -> ')}`, from);
    }
    if (done.includes(key)) return [];
    done.push(key);
    const out: Sexp[] = [];
    // a .sly file is in the C-style syntax (D246), read into the same forms
    const reader = src.path.endsWith('.sly') ? readSly : read;
    for (let xs = reader(cx.load.text(key), src.name); xs.t === 'pair'; xs = xs.cdr) {
        const x = xs.car;
        if (!isForm(x, '@include')) {
            out.push(expand(x, cx.keepMatch));
            continue;
        }
        const t = target(x as Pair, src, cx.lib);
        const module = moduleKey(t, (x as Pair).pos, cx, path, units);
        if (module === null) out.push(...spliceFile(t, (x as Pair).pos, cx, path, done, imports, units));
        else if (!imports.includes(module)) imports.push(module);
    }
    return out;
}

// The key of the unit t is, read now if it hasn't been: or null, if t
// has no header, so it's spliced in where it's included.
function moduleKey(t: Source, from: Pos | null, cx: Cx, stack: readonly Open[], units: Unit[]): string | null {
    const key = keyOf(t, from, cx);
    if (units.some((u) => u.key === key)) return key;
    if (headerOf(t, cx) === null) return null;
    if (stack.some((o) => o.key === key)) {
        throw new CompileError(`including ${t.name} makes a cycle: ${[...stack.map((o) => o.name), t.name].join(' -> ')}`, from);
    }
    return readUnit([t], 'module', from, cx, stack, units);
}

// X.sli, or X.slyi, beside X.slight or X.sly: its header, if it has one.
function headerOf(src: Source, cx: Cx): Source | null {
    const strip = (p: string): string => (p.lastIndexOf('.') < 0 ? p : p.slice(0, p.lastIndexOf('.')));
    for (const ext of ['.sli', '.slyi']) {
        const path = strip(src.path) + ext;
        if (cx.load.key(path) !== null) return { path, name: strip(src.name) + ext };
    }
    return null;
}

// A header's deftypes and sigs, and the modules its includes name, whose
// headers whoever includes this one sees too.
function readHeader(h: Source, cx: Cx, stack: readonly Open[], units: Unit[]): [readonly Sexp[], readonly string[]] {
    const reader = h.path.endsWith('.slyi') ? readSly : read;
    const forms: Sexp[] = [];
    const includes: string[] = [];
    for (let xs = reader(cx.load.text(keyOf(h, null, cx)), h.name); xs.t === 'pair'; xs = xs.cdr) {
        const x = xs.car;
        if (isForm(x, 'deftype') || isForm(x, 'sig')) {
            forms.push(x);
        } else if (isForm(x, '@include')) {
            const t = target(x as Pair, h, cx.lib);
            const module = moduleKey(t, (x as Pair).pos, cx, stack, units);
            if (module === null) throw new CompileError(`a header includes only modules, files with headers: ${t.name} has none`, (x as Pair).pos);
            if (!includes.includes(module)) includes.push(module);
        } else {
            throw new CompileError(`${h.name} is a header: it has deftype and sig forms, and @include, not ${show(x)}`, posOf(x));
        }
    }
    return [forms, includes];
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

// The name in (quote name), or null.
function keyword(x: Sexp): string | null {
    const items = isForm(x, 'quote') ? toArray(x) : null;
    return items !== null && items.length === 2 && items[1]!.t === 'sym' ? items[1]!.name : null;
}

// --- the forms that become cond -----------------------------------------------

// f over the elements of a list, in order, keeping its positions. A loop
// (D39), so that a long list doesn't run Node out of stack.
function mapList(xs: Sexp, f: (x: Sexp) => Sexp): Sexp {
    const cells: Pair[] = [];
    for (; xs.t === 'pair'; xs = xs.cdr) cells.push(xs);
    const items = cells.map((cell) => f(cell.car));
    return cells.reduceRight<Sexp>((tail, cell, i) => cons(items[i]!, tail, cell.pos), xs);
}

// The first n elements as they are, and the rest expanded.
function after(xs: Sexp, n: number, keep: boolean): Sexp {
    if (xs.t !== 'pair') return xs;
    return n > 0 ? cons(xs.car, after(xs.cdr, n - 1, keep), xs.pos) : mapList(xs, (y) => expand(y, keep));
}

// keepMatch: everything but match, which the checker wants as it is
// (D247); expanding the result again lowers it.
export function expand(x: Sexp, keepMatch = false): Sexp {
    if (x.t !== 'pair') return x;
    const ex = (y: Sexp): Sexp => expand(y, keepMatch);
    if (x.car.t !== 'sym') return mapList(x, ex);
    switch (x.car.name) {
        case 'quote':    return x;
        case 'deftype':  return x;    // types, which check.ts reads
        case 'sig':      return x;
        case 'the':      return after(x, 2, keepMatch);
        case '@include': throw new CompileError('@include is only allowed at the top level', x.pos);
        case TUPLE:      throw new CompileError(`#(...) is a pattern, for match and recv; (tuple ...) makes a tuple, not ${show(x)}`, x.pos);
        case 'if':       return ex(expandIf(x));
        case 'when':     return ex(expandWhen(x));
        case 'case':     return ex(expandCase(x));
        case 'and':      return ex(expandAnd(x));
        case 'or':       return ex(expandOr(x));
        case 'match':    return keepMatch ? matchKept(x) : ex(expandMatch(x));
        case 'defun':    return after(x, 3, keepMatch);    // (defun name params body...)
        case 'lambda':   return after(x, 2, keepMatch);    // (lambda params body...)
        case 'cond':     return cons(x.car, mapList(x.cdr, (c) => mapList(c, ex)), x.pos);
        case 'recv':     return cons(x.car, mapList(x.cdr, (c) => after(c, 1, keepMatch)), x.pos);
        default:         return mapList(x, ex);
    }
}

// (match topic (pattern body...) ...) with its topic and bodies expanded,
// and its patterns as they are.
function matchKept(x: Pair): Sexp {
    if (x.cdr.t !== 'pair') return x;
    const clauses = mapList(x.cdr.cdr, (c) => after(c, 1, true));
    return cons(x.car, cons(expand(x.cdr.car, true), clauses, x.cdr.pos), x.pos);
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

// A case's or a match's topic is bound under a name no program can write,
// which says which of them it was.
function topicName(of: 'case' | 'match', x: Pair): Sym {
    const where = x.pos === null ? '' : ` ${x.pos.file}:${x.pos.line}:${x.pos.col}`;
    return sym(`${of} topic${where}`, x.pos);
}

// The case or match a do was made from, or null: so that a message about
// it can name what the program has (split.ts), not the do it became.
export function madeFrom(x: Pair): string | null {
    const first = x.cdr.t === 'pair' ? toArray(x.cdr.car) : null;
    const name  = first !== null && first.length === 3 && isSym(first[0]!, 'let') && first[1]!.t === 'sym' ? first[1]!.name : '';
    return name.startsWith('case topic') ? 'case' : name.startsWith('match topic') ? 'match' : null;
}

function expandCase(x: Pair): Sexp {
    const [topic, ...clauses] = args(x, 'case', 1, Infinity, 'a topic and clauses');
    const t     = topicName('case', x);
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

function expandMatch(x: Pair): Sexp {
    const [topic, ...clauses] = args(x, 'match', 2, Infinity, 'a topic and clauses');
    const t     = topicName('match', x);
    const conds = clauses.map((c) => {
        const items = toArray(c);
        if (items === null || items.length < 2) throw new CompileError(`a match clause is (pattern body...), not ${show(c)}`, posOf(c) ?? x.pos);
        const [pattern, ...body] = items;
        const { tests, binds } = lowerPattern(pattern!, t);
        const test = tests.length === 0 ? TRUE : tests.length === 1 ? tests[0]! : form(x.pos, sym('and', x.pos), ...tests);
        return list(test, ...binds.map(([name, part]) => form(name.pos, sym('let', name.pos), name, part)), ...body);
    });
    const last     = toArray(conds[conds.length - 1]!)![0]!;
    const fallback = last === TRUE ? [] : [list(TRUE, form(x.pos, sym(NO_MATCH, x.pos), t))];
    return form(x.pos, sym('do', x.pos), form(x.pos, sym('let', x.pos), t, topic!), form(x.pos, sym('cond', x.pos), ...conds, ...fallback));
}
