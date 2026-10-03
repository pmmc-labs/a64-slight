// The recv rule (docs/DESIGN.md): which functions can reach a recv.
//
// A receive function is a defun whose whole body is one recv form. A state
// function is a receive function, or a defun that tail-calls a state
// function: the fixpoint over the program's tail calls. Code generation
// then holds state functions to the rule: they can only be called in tail
// position, never from a lambda, and never used as values. So the stack is
// empty at every recv.
//
// Only a program's own defuns are classified: the prelude has no recv, and
// can't see the program's functions.

import { toArray, type Sexp } from './sexp.ts';

export type FnSource = { readonly name: string; readonly params: readonly string[]; readonly body: Sexp };

export const isReceiveBody = (body: Sexp): boolean =>
    body.t === 'pair' && body.cdr.t === 'nil' && body.car.t === 'pair' && body.car.car.t === 'sym' && body.car.car.name === 'recv';

export function stateFunctions(defuns: readonly FnSource[]): readonly string[] {
    const names = defuns.map((d) => d.name);
    const calls = defuns.map((d) => tailCalls(d.body, d.params, names));
    let state   = defuns.filter((d) => isReceiveBody(d.body)).map((d) => d.name);
    for (;;) {
        const more = defuns.filter((d, i) => !state.includes(d.name) && calls[i]!.some((c) => state.includes(c)));
        if (more.length === 0) return state;
        state = [...state, ...more.map((d) => d.name)];
    }
}

// The program's functions (globals) that body calls in tail position. A
// call through a local of the same name isn't one, and lambdas and fork
// bodies are functions of their own.
function tailCalls(body: Sexp, params: readonly string[], globals: readonly string[]): readonly string[] {
    const found: string[] = [];
    const scanBody = (forms: Sexp, bound: readonly string[], tail: boolean): void => {
        for (; forms.t === 'pair'; forms = forms.cdr) {
            const last = forms.cdr.t === 'nil';
            const form = forms.car;
            const [head, name, expr] = toArray(form) ?? [];
            if (head !== undefined && head.t === 'sym' && head.name === 'let' && expr !== undefined) {
                scan(expr, bound, tail && last);
                if (name !== undefined && name.t === 'sym') bound = [...bound, name.name];
            } else {
                scan(form, bound, tail && last);
            }
        }
    };
    const scanClauses = (clauses: Sexp, bound: readonly string[], tail: boolean, patterns: boolean): void => {
        for (; clauses.t === 'pair'; clauses = clauses.cdr) {
            const clause = clauses.car;
            if (clause.t !== 'pair') continue;
            if (patterns) scanBody(clause.cdr, [...bound, ...patternNames(clause.car)], tail);
            else {
                scan(clause.car, bound, false);
                scanBody(clause.cdr, bound, tail);
            }
        }
    };
    const scan = (x: Sexp, bound: readonly string[], tail: boolean): void => {
        if (x.t !== 'pair') return;
        const head = x.car;
        if (head.t === 'sym' && !bound.includes(head.name)) {
            switch (head.name) {
                case 'quote': case 'lambda': case 'fork': case 'connect': case 'defun':
                    return;
                case 'cond':
                    return scanClauses(x.cdr, bound, tail, false);
                case 'recv':
                    return scanClauses(x.cdr, bound, tail, true);
                case 'do': case 'yield':
                    return scanBody(x.cdr, bound, tail);
            }
            if (tail && globals.includes(head.name) && !found.includes(head.name)) found.push(head.name);
        } else {
            scan(head, bound, false);
        }
        for (let args = x.cdr; args.t === 'pair'; args = args.cdr) scan(args.car, bound, false);
    };
    scanBody(body, params, true);
    return found;
}

// The names a recv pattern binds: itself, if it's a name, or the names
// after the tag of (:tag names...).
export function patternNames(pattern: Sexp): readonly string[] {
    if (pattern.t === 'sym') return pattern.name === '_' ? [] : [pattern.name];
    const items = toArray(pattern) ?? [];
    if (items[0]?.t === 'sym') return [];                  // (quote tag)
    return items.slice(1).filter((p) => p.t === 'sym' && p.name !== '_').map((p) => (p as { name: string }).name);
}
