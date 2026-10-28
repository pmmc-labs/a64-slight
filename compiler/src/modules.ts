// Modules (D248): a file with a header is a module, and what it doesn't
// declare there is its own.
//
// expand.ts reads a program as units: the prelude, the program (its
// files, and the files they splice in), and each file that has a header
// beside it (X.sli, or X.slyi in the C-style syntax, for X.slight or
// X.sly), with what it splices in. A header holds deftype and sig forms,
// and @include of other headers: the sigs say which functions the module
// exports, and the types are the module's types for anyone. What a module
// includes it sees; what a header includes, whoever includes the header
// sees too, as with C's headers.
//
// link makes the units one program again, as the rest of the compiler
// has always had it, by renaming: each of a module's functions, and each
// type of its own that's not in its header, gets a name with its file in
// it, which no program can write (`map/bit (in lib/map.slight)`), and
// each name a unit's code mentions becomes the one it means there: its
// own function, one a module it includes exports, or the prelude's.
// Library code calls the prelude's as `f (prelude)`, so a program's own
// f (D80) doesn't take its place. The program's own names stay as they
// are, and so do the prelude's exports, in the prelude's namespace. A
// name another unit has, which this one can't see, is a compile error
// that says whose it is. displayName takes the file back out, for what
// people read: faults, :system, warnings.

import { CompileError } from './errors.ts';
import { patternNames } from './pattern.ts';
import { cons, isForm, list, posOf, sym, toArray, type Pair, type Sexp, type Sym } from './sexp.ts';

export type UnitKind = 'prelude' | 'program' | 'module';

export type Unit = {
    readonly key: string;
    readonly name: string;                  // as positions name its file: lib/map.slight
    readonly kind: UnitKind;
    readonly forms: readonly Sexp[];        // its own, with what it splices in, expanded
    readonly header: readonly Sexp[] | null;  // its header's deftypes and sigs, or null if it has none
    readonly imports: readonly string[];    // the keys of the modules it includes, its header's included
    readonly reexports: readonly string[];  // the keys of the modules its header includes
};

// A name with its unit's file in it, and the name a prelude function has
// in a library's code.
const IN = ' (in ';
export const PRELUDE_SUFFIX = ' (prelude)';
export const ownName = (name: string, unit: string): string => `${name}${IN}${unit})`;

// A name as people read it: without its file, or (prelude).
export function displayName(name: string): string {
    let n = name;
    const at = n.indexOf(IN);
    if (at >= 0) {
        const end = n.indexOf(')', at);
        n = n.slice(0, at) + (end < 0 ? '' : n.slice(end + 1));
    }
    return n.endsWith(PRELUDE_SUFFIX) ? n.slice(0, -PRELUDE_SUFFIX.length) : n;
}

const defunName = (x: Sexp): string | null => {
    const items = isForm(x, 'defun') ? toArray(x) : null;
    return items !== null && items[1]?.t === 'sym' ? items[1].name : null;
};

// A sig's or a deftype's name: (sig f ...), (sig (f A) ...).
const declName = (x: Sexp): string | null => {
    const items = toArray(x);
    const head = items?.[1];
    if (head === undefined) return null;
    if (head.t === 'sym') return head.name;
    const first = toArray(head)?.[0];
    return first !== undefined && first.t === 'sym' ? first.name : null;
};

// The type variables a sig or deftype declares: (sig (f A B) ...).
const declVars = (x: Sexp): readonly string[] => {
    const head = toArray(x)?.[1];
    const parts = head !== undefined && head.t === 'pair' ? toArray(head) ?? [] : [];
    return parts.slice(1).filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
};

// What each unit's code can see: a function's name to the name it has
// after linking; and a type's.
type Scope = { readonly fns: ReadonlyMap<string, string>; readonly types: ReadonlyMap<string, string> };

// Where each function is, for saying why a name can't be seen.
type Owner = { readonly unit: Unit; readonly exported: boolean };

export function link(units: readonly Unit[]): { readonly prelude: Sexp; readonly program: Sexp; readonly modules: readonly string[] } {
    const byKey = new Map(units.map((u) => [u.key, u]));
    const prelude = units.find((u) => u.kind === 'prelude');
    const exportsOf = (u: Unit): readonly string[] =>
        u.header === null ? u.forms.flatMap((f) => defunName(f) ?? []) : u.header.filter((f) => isForm(f, 'sig')).flatMap((f) => declName(f) ?? []);
    const defunsOf = (u: Unit): readonly string[] => u.forms.flatMap((f) => defunName(f) ?? []);

    // what a module's header declares, it defines
    for (const u of units) {
        if (u.header === null) continue;
        const own = defunsOf(u);
        for (const f of u.header.filter((h) => isForm(h, 'sig'))) {
            const n = declName(f);
            if (n !== null && !own.includes(n)) throw new CompileError(`${u.name}'s header declares ${n}, which it doesn't define`, posOf(f));
        }
    }

    // the name a unit's function has after linking
    const linked = (u: Unit, f: string): string => {
        if (u.kind === 'program') return f;
        if (u.kind === 'prelude' && exportsOf(u).includes(f)) return f;
        return ownName(f, u.name);
    };
    // a unit's types that aren't in its header are its own
    const privateTypes = (u: Unit): readonly string[] =>
        u.kind === 'program' ? [] : u.forms.filter((f) => isForm(f, 'deftype')).flatMap((f) => declName(f) ?? []);

    const owners = new Map<string, Owner[]>();
    for (const u of units) {
        const exported = exportsOf(u);
        for (const f of defunsOf(u)) owners.set(f, [...(owners.get(f) ?? []), { unit: u, exported: exported.includes(f) }]);
    }

    // the modules a unit sees: those it includes, and what their headers include
    const seen = (u: Unit): readonly Unit[] => {
        const out: Unit[] = [];
        const visit = (key: string): void => {
            const m = byKey.get(key);
            if (m === undefined || out.includes(m)) return;
            out.push(m);
            m.reexports.forEach(visit);
        };
        u.imports.forEach(visit);
        return out;
    };

    const scopeOf = (u: Unit): Scope => {
        const fns = new Map<string, string>();
        if (prelude !== undefined && u !== prelude) {
            for (const f of exportsOf(prelude)) fns.set(f, u.kind === 'program' ? f : `${f}${PRELUDE_SUFFIX}`);
        }
        const from = new Map<string, Unit>();
        for (const m of seen(u)) {
            if (m.kind === 'prelude') continue;
            for (const f of exportsOf(m)) {
                const other = from.get(f);
                if (other !== undefined && other !== m) {
                    throw new CompileError(`${f} is exported by both ${other.name} and ${m.name}, which ${u.name} includes`, null);
                }
                from.set(f, m);
                fns.set(f, linked(m, f));
            }
        }
        for (const f of defunsOf(u)) fns.set(f, linked(u, f));
        const types = new Map(privateTypes(u).map((t) => [t, ownName(t, u.name)]));
        return { fns, types };
    };

    const renamed = new Map(units.map((u) => [u.key, renameUnit(u, scopeOf(u), owners)]));
    const forms = (kind: UnitKind): readonly Sexp[] => units.filter((u) => u.kind === kind).flatMap((u) => renamed.get(u.key)!);
    // every file a module's forms came from: its own, what it splices in, its header
    const files = new Set<string>();
    const note = (x: Sexp): void => {
        const p = posOf(x);
        if (p !== null) files.add(p.file);
    };
    units.filter((u) => u.kind !== 'program').forEach((u) => [...u.forms, ...(u.header ?? [])].forEach(note));
    return {
        prelude: list(...forms('prelude')),
        program: list(...forms('module'), ...forms('program')),
        modules: [...files],
    };
}

// A unit's forms, its header's after them, with every name as linking
// makes it.
// The top level's lets bind for the forms after them, as a body's do.
function renameUnit(u: Unit, scope: Scope, owners: ReadonlyMap<string, readonly Owner[]>): readonly Sexp[] {
    const r: R = { u, scope, owners };
    let locals: readonly string[] = [];
    const out: Sexp[] = [];
    for (const x of [...u.forms, ...(u.header ?? [])]) {
        out.push(renameTop(x, locals, r));
        if (isForm(x, 'let')) {
            const n = toArray(x)?.[1];
            if (n !== undefined && n.t === 'sym') locals = [...locals, n.name];
        }
    }
    return out;
}

function renameTop(x: Sexp, locals: readonly string[], r: R): Sexp {
    const scope = r.scope;
    if (x.t !== 'pair') return expr(x, locals, r);
    if (isForm(x, 'defun')) {
        const items = toArray(x)!;
        const [head, name, params, ...body] = items;
        if (name === undefined || name.t !== 'sym' || params === undefined) return x;
        const locals = (toArray(params) ?? []).filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
        return cons(head!, cons(sym(scope.fns.get(name.name) ?? name.name, name.pos), cons(params, bodyOf(list(...body), locals, r))), x.pos);
    }
    if (isForm(x, 'sig') || isForm(x, 'deftype')) {
        const items = toArray(x)!;
        const vars = declVars(x);
        const rename = isForm(x, 'sig') ? (n: string): string => scope.fns.get(n) ?? n : (n: string): string => scope.types.get(n) ?? n;
        const head = items[1]!;
        const newHead = head.t === 'sym' ? sym(rename(head.name), head.pos)
            : head.t === 'pair' && head.car.t === 'sym' ? cons(sym(rename(head.car.name), head.car.pos), head.cdr, head.pos) : head;
        return cons(items[0]!, cons(newHead, list(...items.slice(2).map((t) => typeExpr(t, vars, scope)))), x.pos);
    }
    return expr(x, locals, r);
}

type R = { readonly u: Unit; readonly scope: Scope; readonly owners: ReadonlyMap<string, readonly Owner[]> };

// A type's names: a unit's own type is renamed, a type variable isn't.
function typeExpr(t: Sexp, vars: readonly string[], scope: Scope): Sexp {
    if (t.t === 'sym') return vars.includes(t.name) ? t : sym(scope.types.get(t.name) ?? t.name, t.pos);
    if (t.t !== 'pair' || isForm(t, 'quote')) return t;
    return mapCells(t, (x) => typeExpr(x, vars, scope));
}

// f over a list's elements, in a loop (D39), keeping its positions.
function mapCells(xs: Sexp, f: (x: Sexp) => Sexp): Sexp {
    const cells: Pair[] = [];
    let end: Sexp = xs;
    for (; end.t === 'pair'; end = end.cdr) cells.push(end);
    const items = cells.map((c) => f(c.car));
    return cells.reduceRight<Sexp>((tail, c, i) => cons(items[i]!, tail, c.pos), end);
}

// A body's forms, each let binding for the ones after it.
function bodyOf(forms: Sexp, locals: readonly string[], r: R): Sexp {
    const out: Sexp[] = [];
    let ls = locals;
    for (let xs = forms; xs.t === 'pair'; xs = xs.cdr) {
        const f = xs.car;
        if (isForm(f, 'let')) {
            const items = toArray(f) ?? [];
            const [head, name, value] = items;
            out.push(value === undefined ? f : cons(head!, cons(name!, list(expr(value, ls, r))), (f as Pair).pos));
            if (name !== undefined && name.t === 'sym') ls = [...ls, name.name];
        } else {
            out.push(expr(f, ls, r));
        }
    }
    return list(...out);
}

// A clause whose head binds names (a pattern) or is a test (cond's).
function clauses(cs: Sexp, locals: readonly string[], r: R, patterns: boolean): Sexp {
    return mapCells(cs, (c) => {
        if (c.t !== 'pair') return c;
        if (patterns) return cons(c.car, bodyOf(c.cdr, [...locals, ...patternNames(c.car)], r), c.pos);
        return bodyOf(c, locals, r);
    });
}

function expr(x: Sexp, locals: readonly string[], r: R): Sexp {
    if (x.t === 'sym') return name(x, locals, r);
    if (x.t !== 'pair') return x;
    const head = x.car;
    if (head.t === 'sym' && !locals.includes(head.name)) {
        switch (head.name) {
            case 'quote': return x;
            case 'lambda': {
                const params = x.cdr.t === 'pair' ? x.cdr.car : null;
                if (params === null) return x;
                const ps = (toArray(params) ?? []).filter((p) => p.t === 'sym').map((p) => (p as Sym).name);
                return cons(head, cons(params, bodyOf((x.cdr as Pair).cdr, [...locals, ...ps], r)), x.pos);
            }
            case 'let': {
                const items = toArray(x) ?? [];
                return items.length === 3 ? cons(head, cons(items[1]!, list(expr(items[2]!, locals, r))), x.pos) : x;
            }
            case 'do':
                return cons(head, bodyOf(x.cdr, locals, r), x.pos);
            case 'cond':
                return cons(head, clauses(x.cdr, locals, r, false), x.pos);
            case 'recv':
                return cons(head, clauses(x.cdr, locals, r, true), x.pos);
            case 'match': {
                if (x.cdr.t !== 'pair') return x;
                return cons(head, cons(expr(x.cdr.car, locals, r), clauses(x.cdr.cdr, locals, r, true), x.cdr.pos), x.pos);
            }
            case 'the': {
                const items = toArray(x) ?? [];
                if (items.length !== 3) return x;
                return cons(head, list(typeExpr(items[1]!, [], r.scope), expr(items[2]!, locals, r)), x.pos);
            }
        }
    }
    return mapCells(x, (e) => expr(e, locals, r));
}

// A name in an expression: a local as it is, else what it means here.
function name(x: Sym, locals: readonly string[], r: R): Sexp {
    if (locals.includes(x.name)) return x;
    const to = r.scope.fns.get(x.name);
    if (to !== undefined) return to === x.name ? x : sym(to, x.pos);
    const elsewhere = (r.owners.get(x.name) ?? []).filter((o) => o.unit !== r.u);
    if (elsewhere.length > 0) throw new CompileError(unseen(x.name, elsewhere[0]!, r.u), x.pos);
    return x;
}

function unseen(n: string, o: Owner, from: Unit): string {
    const whose = o.unit.kind === 'prelude' ? 'the prelude' : o.unit.kind === 'program' ? 'the program' : o.unit.name;
    if (o.unit.kind === 'program') return `${n} is the program's, which ${from.name} can't see`;
    if (!o.exported) return `${n} is private to ${whose}: its header doesn't declare it`;
    return `${n} is ${whose}'s: include it to use it`;
}

