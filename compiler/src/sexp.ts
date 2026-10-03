// S-expressions: what the reader produces and every pass consumes.
//
// Everything is immutable. Symbols are plain records compared by name, not
// interned objects: the self-hosted compiler will hold source symbols as
// data (slight can't make symbols at runtime), so this is the shape it will
// have there too. Every atom carries its source position; a list carries
// the position of its `(` on its first pair. Positions are never part of
// equality.

export type Pos = { readonly file: string; readonly line: number; readonly col: number };

export type Sym   = { readonly t: 'sym';   readonly name: string; readonly pos: Pos | null };
export type Int   = { readonly t: 'int';   readonly v: bigint;    readonly pos: Pos | null };
export type Float = { readonly t: 'float'; readonly v: number;    readonly pos: Pos | null };
export type Str   = { readonly t: 'str';   readonly v: string;    readonly pos: Pos | null };
export type Nil   = { readonly t: 'nil' };
export type Pair  = { readonly t: 'pair';  readonly car: Sexp; readonly cdr: Sexp; readonly pos: Pos | null };

export type Sexp = Sym | Int | Float | Str | Nil | Pair;

export const NIL: Nil = { t: 'nil' };

export const sym   = (name: string, pos: Pos | null = null): Sym => ({ t: 'sym', name, pos });
export const int   = (v: bigint, pos: Pos | null = null): Int => ({ t: 'int', v, pos });
export const float = (v: number, pos: Pos | null = null): Float => ({ t: 'float', v, pos });
export const str   = (v: string, pos: Pos | null = null): Str => ({ t: 'str', v, pos });
export const cons  = (car: Sexp, cdr: Sexp, pos: Pos | null = null): Pair => ({ t: 'pair', car, cdr, pos });

// slight integers are 63-bit: one bit of the word is the tag.
export const INT_MIN = -(2n ** 62n);
export const INT_MAX = 2n ** 62n - 1n;
export const fitsInt = (n: bigint): boolean => n >= INT_MIN && n <= INT_MAX;

export const isSym = (x: Sexp, name: string): boolean => x.t === 'sym' && x.name === name;

// The position of an expression, for error messages: its own, or failing
// that, its first element's.
export function posOf(x: Sexp): Pos | null {
    if (x.t === 'nil') return null;
    if (x.pos !== null || x.t !== 'pair') return x.pos;
    return posOf(x.car);
}

export function list(...items: readonly Sexp[]): Sexp {
    return items.reduceRight<Sexp>((tail, item) => cons(item, tail), NIL);
}

export function reverse(xs: Sexp, acc: Sexp = NIL): Sexp {
    for (;;) {
        if (xs.t !== 'pair') return acc;
        acc = cons(xs.car, acc);
        xs  = xs.cdr;
    }
}

export const append = (xs: Sexp, ys: Sexp): Sexp => reverse(reverse(xs), ys);

// The elements of a proper list, or null if it isn't one.
export function toArray(xs: Sexp): readonly Sexp[] | null {
    const out: Sexp[] = [];
    for (; xs.t === 'pair'; xs = xs.cdr) out.push(xs.car);
    return xs.t === 'nil' ? out : null;
}

// Reader syntax, for error messages and tests.
export function show(x: Sexp): string {
    switch (x.t) {
        case 'sym':   return x.name;
        case 'int':   return x.v.toString();
        case 'float': return Number.isInteger(x.v) ? x.v.toFixed(1) : x.v.toString();
        case 'str':   return JSON.stringify(x.v);
        case 'nil':   return '()';
        case 'pair': {
            const items = toArray(x);
            return items === null ? '(<improper list>)' : `(${items.map(show).join(' ')})`;
        }
    }
}
