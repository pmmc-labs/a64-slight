// The .sly reader: slight in a C-style syntax (D246, docs/SYNTAX.md),
// read into the s-expressions reader.ts makes, positions and all, so
// nothing after it knows which syntax a file was in.
//
// A precedence climber over a token list, as reader.ts is a recursive
// descent over one, so that it ports to slight the same way. Every form it
// makes carries the position of the token it came from: a call its
// callee's, an operator's its own, a keyword form its keyword's.
//
// What it reads into what, in short (SYNTAX.md has it all):
//
//   fun f(a, b) { body }       (defun f (a b) body...)
//   fun(a) => e                (lambda (a) e)
//   let x = e;                 (let x e)
//   let [a, b] = e; rest       (match e ((a b) rest...))
//   f(a, b)                    (f a b)
//   a + b, a == b, a && b      (+ a b), (eq? a b), (and a b)
//   x |> f(a)                  (f a x)
//   [a, b], [a, ...xs]         (list a b), (cons a xs)
//   :tag, :tag(a)              'tag, (list :tag a)
//   [1, :a], :tag()            '(1 a), '(tag): all literals, a constant
//   #(a, b)                    (tuple a b)
//   if c { a } else { b }      (if c a b)
//   match e { p => a, ... }    (match e (p a) ...)
//   recv { p => a }, recv      (recv (p a)), (recv)
//   spawn e, fork e, yield e   (spawn e), (fork e), (yield e)
//   self, parent               $$, ^$$
//
// And types (D247, docs/TYPES.md), which the checker reads and the
// compiler leaves out:
//
//   fun f(x : int, y) : str    (sig f (int any) str) (defun f (x y) ...)
//   fun g<A>(xs : list<A>) : A (sig (g A) ((list A)) A) ...
//   fun h(n : int) recv Msg    (sig h (int) any (recv Msg)) ...
//   let x : T = e;             (let x (the T e))
//   fun(x : int) => e          (the (fun (int) any) (lambda (x) e))
//   type Name<A> = T;          (deftype (Name A) T)
//
// A type's `:` has a space on each side, and `x: int` is an error that
// says so.
// Names: an identifier's `::` is slight's `/`, then `_to_` its `->`, then
// `_` its `-`, so str_to_int is str->int, and it may start with `$`; a
// name between backticks is taken as it is. Keywords keep slight's
// spelling. `'` and a datum is that datum, quoted, read as reader.ts
// reads s-expressions: '((lambda (x) x) 1).

import { CompileError } from './errors.ts';
import { atEnd, isDigit, isLetter, isSpace, numberKind, parseAtom, peek, posOf as cursorPos, readDatumAt, readString,
         skipDoc, skipWhile, step, type Cursor } from './reader.ts';
import { NIL, TUPLE, cons, fitsInt, float, form, int, isForm, isSym, list, posOf, str, sym, INT_MAX, INT_MIN,
         type Pos, type Sexp } from './sexp.ts';

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

// A name is an identifier or a raw name, `text` its slight name; `plain`
// says it's an identifier without `::`, which is all a parameter or a
// pattern's binding may be. A word is a reserved word, or @include, @ARGV
// or %ENV. A quoted token is `'` and the datum after it, as reader.ts
// reads one.
type Token =
    | { readonly kind: 'num' | 'str' | 'kw' | 'word' | 'punct'; readonly text: string; readonly pos: Pos }
    | { readonly kind: 'name'; readonly text: string; readonly plain: boolean; readonly pos: Pos }
    | { readonly kind: 'quoted'; readonly text: string; readonly datum: Sexp; readonly pos: Pos };

const RESERVED = ['fun', 'let', 'if', 'else', 'cond', 'case', 'match', 'recv', 'spawn', 'fork', 'connect', 'yield',
                  'true', 'false', 'self', 'parent'];

// Longest first, so that `...` isn't read as something shorter.
const PUNCTS = ['...', '=>', '==', '!=', '<=', '>=', '&&', '||', '|>', '++', '#(', '->',
                '(', ')', '[', ']', '{', '}', ',', ';', '<', '>', '+', '-', '*', '/', '%', '!', '=', '|'];

const isIdentStart = (ch: string): boolean => isLetter(ch) || ch === '_';
const isIdentChar  = (ch: string): boolean => isIdentStart(ch) || isDigit(ch);
// What can't be in a keyword: what ends one in an expression.
const isKwChar     = (ch: string): boolean => ch !== '' && !isSpace(ch) && !'()[]{},;"\'`'.includes(ch);

// An identifier's slight name.
export function slightName(ident: string): string {
    if (ident === '_') return ident;
    return ident.split('::').join('/').split('_to_').join('->').split('_').join('-');
}

function tokenize(src: string, file: string): readonly Token[] {
    const tokens: Token[] = [];
    let c: Cursor = { src, file, i: 0, line: 1, col: 1 };
    let depth = 0;
    while (!atEnd(c)) {
        // a doc block, between top-level items
        if (c.col === 1 && depth === 0 && peek(c) === '=' && isLetter(src[c.i + 1] ?? '')) {
            c = skipDoc(c);
            continue;
        }
        const [token, next] = nextToken(c);
        if (token !== null) {
            tokens.push(token);
            if (token.kind === 'punct' && ['(', '[', '{', '#('].includes(token.text)) depth++;
            if (token.kind === 'punct' && [')', ']', '}'].includes(token.text)) depth--;
        }
        c = next;
    }
    return tokens;
}

// The text from c on that's an identifier, with any `::` segments.
function identEnd(c: Cursor): Cursor {
    let end = skipWhile(c, isIdentChar);
    if (peek(end) === '?') end = step(end);
    const s = c.src;
    return s[end.i] === ':' && s[end.i + 1] === ':' && isIdentStart(s[end.i + 2] ?? '') ? identEnd(step(step(end))) : end;
}

// The token starting at c (null for white space and comments), and the
// cursor after it.
function nextToken(c: Cursor): [Token | null, Cursor] {
    const ch  = peek(c);
    const pos = cursorPos(c);
    const s   = c.src;
    if (isSpace(ch)) return [null, skipWhile(c, isSpace)];
    if (ch === '/' && s[c.i + 1] === '/') return [null, skipWhile(c, (x) => x !== '\n')];
    if (ch === '"') {
        const [t, next] = readString(step(c), pos);
        return [{ kind: 'str', text: (t as { text: string }).text, pos }, next];
    }
    if (isDigit(ch)) return readNumber(c, pos);
    // a name may start with the sigil $ (D246): $f is slight's $f
    if (isIdentStart(ch) || (ch === '$' && isIdentStart(s[c.i + 1] ?? ''))) {
        const end  = identEnd(ch === '$' ? step(c) : c);
        const text = s.slice(c.i, end.i);
        if (RESERVED.includes(text)) return [{ kind: 'word', text, pos }, end];
        return [{ kind: 'name', text: slightName(text), plain: !text.includes('::'), pos }, end];
    }
    if (ch === '`') {
        const end = skipWhile(step(c), (x) => x !== '`' && x !== '\n');
        const text = s.slice(c.i + 1, end.i);
        if (peek(end) !== '`' || text === '') throw new CompileError('a raw name is a name between backticks, on one line', pos);
        return [{ kind: 'name', text, plain: true, pos }, step(end)];
    }
    if (ch === "'") return readQuoted(c, pos);
    if (ch === ':') {
        // a type's : (D247) has space on each side; a keyword's has none after it
        const next = s[c.i + 1] ?? '';
        if (next === '' || isSpace(next)) {
            if (c.i > 0 && !isSpace(s[c.i - 1]!)) throw new CompileError("a type's ':' has a space on each side: write x : int", pos);
            return [{ kind: 'punct', text: ':', pos }, step(c)];
        }
        let end    = skipWhile(step(c), isKwChar);
        let text   = s.slice(c.i + 1, end.i);
        // a keyword doesn't end in >, so that a type's actor<:ok> closes
        let trim = 0;
        while (trim < text.length && text[text.length - 1 - trim] === '>') trim++;
        if (trim > 0 && trim < text.length) {
            text = text.slice(0, -trim);
            end  = advance(c, 1 + text.length);
        }
        if (text === '' || text.startsWith(':') || text.startsWith('#') || numberKind(text) !== null) {
            throw new CompileError(`invalid keyword ':${text}'`, pos);
        }
        return [{ kind: 'kw', text, pos }, end];
    }
    for (const w of ['@include', '@ARGV', '%ENV']) {
        if (s.startsWith(w, c.i) && !isIdentChar(s[c.i + w.length] ?? '')) {
            return [{ kind: 'word', text: w, pos }, advance(c, w.length)];
        }
    }
    for (const p of PUNCTS) {
        if (s.startsWith(p, c.i)) return [{ kind: 'punct', text: p, pos }, advance(c, p.length)];
    }
    throw new CompileError(`unexpected '${ch}'`, pos);
}

// 'datum, in slight's s-expressions (D246): a list to its matching `)`,
// with slight's rules inside it, `;` comments and all; or an atom, which
// stops where a keyword does, so that ['a, 'b] is a list of two.
function readQuoted(c: Cursor, pos: Pos): [Token, Cursor] {
    const d = step(c);
    if (peek(d) === '(' || (peek(d) === '#' && c.src[d.i + 1] === '(')) {
        const [datum, end] = readDatumAt(d);
        return [{ kind: 'quoted', text: "'", datum, pos }, end];
    }
    if (peek(d) === '"') {
        const [t, end] = readString(step(d), cursorPos(d));
        return [{ kind: 'quoted', text: "'", datum: str((t as { text: string }).text, cursorPos(d)), pos }, end];
    }
    const end  = skipWhile(d, isKwChar);
    const text = c.src.slice(d.i, end.i);
    if (text === '') throw new CompileError("expected something to quote after '", pos);
    return [{ kind: 'quoted', text: "'", datum: parseAtom(text, cursorPos(d)), pos }, end];
}

function advance(c: Cursor, n: number): Cursor {
    for (let k = 0; k < n; k++) c = step(c);
    return c;
}

// Digits, then a point and digits, then an exponent, as slight writes
// them; a `-` before them is the parser's, which folds it in.
function readNumber(c: Cursor, pos: Pos): [Token, Cursor] {
    const s = c.src;
    let end = skipWhile(c, isDigit);
    if (s[end.i] === '.' && isDigit(s[end.i + 1] ?? '')) end = skipWhile(step(end), isDigit);
    const e = s[end.i];
    if ((e === 'e' || e === 'E') && end.i > c.i && s.slice(c.i, end.i).includes('.')) {
        const k = s[end.i + 1] === '+' || s[end.i + 1] === '-' ? 2 : 1;
        if (isDigit(s[end.i + k] ?? '')) end = skipWhile(advance(end, k), isDigit);
    }
    if (isIdentChar(peek(end))) throw new CompileError(`invalid number '${s.slice(c.i, identEnd(end).i)}'`, pos);
    return [{ kind: 'num', text: s.slice(c.i, end.i), pos }, end];
}

// ---------------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------------

type Ts = readonly Token[];

const isP = (t: Token | undefined, text: string): boolean => t !== undefined && t.kind === 'punct' && t.text === text;
const isW = (t: Token | undefined, text: string): boolean => t !== undefined && t.kind === 'word' && t.text === text;

function describe(t: Token | undefined): string {
    if (t === undefined) return 'the end of the file';
    if (t.kind === 'str') return 'a string';
    if (t.kind === 'kw') return `':${t.text}'`;
    if (t.kind === 'quoted') return 'a quoted datum';
    return `'${t.text}'`;
}

function expect(ts: Ts, i: number, text: string, what: string): number {
    if (isP(ts[i], text)) return i + 1;
    throw new CompileError(`expected '${text}' ${what}, not ${describe(ts[i])}`, ts[i]?.pos ?? ts.at(-1)?.pos ?? null);
}

// The ';' after an item or a statement, which can be left out when it
// ends with a '}', as an arm's ',' can.
const endItem = (ts: Ts, i: number, what: string): number =>
    isP(ts[i - 1], '}') && !isP(ts[i], ';') ? i : expect(ts, i, ';', what);

// A '}' that ends a line ends the expression, when the next line starts
// with '(' or '-': either could begin a statement whose ';' before it was
// left out, and is taken to.
const freshLine = (ts: Ts, j: number): boolean =>
    isP(ts[j - 1], '}') && ts[j]!.pos.line > ts[j - 1]!.pos.line;

// Every top-level form in a .sly file, as a list, as read() gives them.
export function readSly(src: string, file: string): Sexp {
    const ts = tokenize(src, file);
    const out: Sexp[] = [];
    let i = 0;
    while (i < ts.length) {
        const t = ts[i]!;
        if (isP(t, ';')) {
            i++;
            continue;
        }
        if (isW(t, 'fun') && ts[i + 1]?.kind === 'name') {
            const [ds, next] = parseDefun(ts, i);
            out.push(...ds);
            i = next;
            continue;
        }
        let x: Sexp;
        if (isW(t, '@include')) {
            [x, i] = parseInclude(ts, i);
        } else if (isTypeDecl(ts, i)) {
            [x, i] = parseTypeDecl(ts, i);
        } else if (isW(t, 'let')) {
            const [pattern, j] = parsePattern(ts, i + 1);
            if (pattern.t !== 'sym' || isSym(pattern, '_')) {
                throw new CompileError("a top-level let binds a name: there's no rest of a block for a pattern's match", t.pos);
            }
            const [e, k] = parseLetValue(ts, j, 'after the name a let binds');
            [x, i] = [form(t.pos, sym('let', t.pos), pattern, e), k];
        } else {
            [x, i] = parseExpr(ts, i);
        }
        out.push(x);
        if (i < ts.length) i = endItem(ts, i, 'after a top-level item');
    }
    return list(...out);
}

// fun name<A, B>(params) : result recv messages { body }, the types
// optional: the defun, and before it a sig if anything has a type.
function parseDefun(ts: Ts, i: number): [readonly Sexp[], number] {
    const at   = ts[i]!.pos;
    const name = ts[i + 1]!;
    const [vars, j0] = isP(ts[i + 2], '<') ? parseTypeVars(ts, i + 2) : [[], i + 2];
    const [params, j] = parseParams(ts, j0);
    let k = j;
    let result: Sexp | null = null;
    let takes: Sexp | null = null;
    if (isP(ts[k], ':')) [result, k] = parseType(ts, k + 1);
    if (isW(ts[k], 'recv')) [takes, k] = parseType(ts, k + 1);
    const [body, m] = parseBlock(ts, k);
    const defun = form(at, sym('defun', at), sym(name.text, name.pos), params.names, ...nonEmpty(body));
    if (vars.length === 0 && result === null && takes === null && params.types.every((t) => t === null)) return [[defun], m];
    const head = vars.length === 0 ? sym(name.text, name.pos) : form(name.pos, sym(name.text, name.pos), ...vars);
    const sig  = form(at, sym('sig', at), head, list(...params.types.map((t) => t ?? sym('any', at))), result ?? sym('any', at),
                      ...(takes === null ? [] : [list(sym('recv', at), takes)]));
    return [[sig, defun], m];
}

// (a : T, b, ...): the names, and each one's type, or null.
type Params = { readonly names: Sexp; readonly types: readonly (Sexp | null)[] };

function parseParams(ts: Ts, i: number): [Params, number] {
    i = expect(ts, i, '(', 'before the parameters');
    const names: Sexp[] = [];
    const types: (Sexp | null)[] = [];
    while (!isP(ts[i], ')')) {
        const t = ts[i];
        if (t?.kind !== 'name' || !t.plain) throw new CompileError(`a parameter is a name, not ${describe(t)}`, t?.pos ?? null);
        names.push(sym(t.text, t.pos));
        i++;
        if (ts[i]?.kind === 'kw') throw new CompileError(`a type's ':' has a space on each side: write ${t.text} : ${ts[i]!.text}`, ts[i]!.pos);
        let type: Sexp | null = null;
        if (isP(ts[i], ':')) [type, i] = parseType(ts, i + 1);
        types.push(type);
        if (!isP(ts[i], ')')) i = expect(ts, i, ',', 'between parameters');
    }
    return [{ names: list(...names), types }, i + 1];
}

// A let's value, after its pattern: `: T = e` is (the T e).
function parseLetValue(ts: Ts, i: number, what: string): [Sexp, number] {
    if (!isP(ts[i], ':')) return parseExpr(ts, expect(ts, i, '=', what));
    const at = ts[i]!.pos;
    const [type, j] = parseType(ts, i + 1);
    const [e, k] = parseExpr(ts, expect(ts, j, '=', "after a let's type"));
    return [form(at, sym('the', at), type, e), k];
}

// ---- types (D247) ----------------------------------------------------------

// type Name = T; type Name<A, B> = T;
const isTypeDecl = (ts: Ts, i: number): boolean => {
    const t = ts[i];
    return t?.kind === 'name' && t.text === 'type' && ts[i + 1]?.kind === 'name' && (isP(ts[i + 2], '=') || isP(ts[i + 2], '<'));
};

function parseTypeDecl(ts: Ts, i: number): [Sexp, number] {
    const at = ts[i]!.pos;
    const name = ts[i + 1]!;
    const [vars, j] = isP(ts[i + 2], '<') ? parseTypeVars(ts, i + 2) : [[], i + 2];
    const [type, k] = parseType(ts, expect(ts, j, '=', "after a type's name"));
    const head = vars.length === 0 ? sym(name.text, name.pos) : form(name.pos, sym(name.text, name.pos), ...vars);
    return [form(at, sym('deftype', at), head, type), k];
}

// <A, B>: type variables.
function parseTypeVars(ts: Ts, i: number): [readonly Sexp[], number] {
    i = expect(ts, i, '<', 'before type variables');
    const vars: Sexp[] = [];
    while (!isP(ts[i], '>')) {
        const t = ts[i];
        if (t?.kind !== 'name' || !t.plain) throw new CompileError(`a type variable is a name, not ${describe(t)}`, t?.pos ?? null);
        vars.push(sym(t.text, t.pos));
        i = isP(ts[i + 1], '>') ? i + 1 : expect(ts, i + 1, ',', 'between type variables');
    }
    return [vars, i + 1];
}

// A type, as docs/TYPES.md writes them in this syntax, into the
// s-expression check.ts reads: A | B is (or A B).
function parseType(ts: Ts, i: number): [Sexp, number] {
    const at = ts[i]?.pos ?? null;
    const parts: Sexp[] = [];
    let [t, j] = parseTypeOne(ts, i);
    parts.push(t);
    while (isP(ts[j], '|')) {
        [t, j] = parseTypeOne(ts, j + 1);
        parts.push(t);
    }
    return [parts.length === 1 ? parts[0]! : form(at, sym('or', at), ...parts), j];
}

// Types up to close, each maybe after `...` (the last only), and past it.
function parseTypeItems(ts: Ts, i: number, close: string): [readonly Sexp[], Sexp | null, number] {
    const items: Sexp[] = [];
    while (!isP(ts[i], close)) {
        if (isP(ts[i], '...')) {
            const [rest, j] = parseType(ts, i + 1);
            return [items, rest, expect(ts, j, close, "after a type's '...'")];
        }
        const [t, j] = parseType(ts, i);
        items.push(t);
        i = isP(ts[j], close) ? j : expect(ts, j, ',', 'between types');
    }
    return [items, null, i + 1];
}

function parseTypeOne(ts: Ts, i: number): [Sexp, number] {
    const t = ts[i];
    if (t === undefined) throw new CompileError('expected a type, not the end of the file', ts.at(-1)?.pos ?? null);
    const at = t.pos;
    switch (t.kind) {
        case 'name': {
            if (!isP(ts[i + 1], '<')) return [sym(t.text, at), i + 1];
            const [args, , j] = parseTypeItems(ts, i + 2, '>');
            return [form(at, sym(t.text, at), ...args), j];
        }
        case 'word':
            if (t.text === 'true') return [sym('#true', at), i + 1];
            if (t.text === 'false') return [sym('#false', at), i + 1];
            if (t.text === 'fun') {
                if (!isP(ts[i + 1], '(')) return [sym('fun', at), i + 1];
                const [params, , j] = parseTypeItems(ts, i + 2, ')');
                const [result, k] = parseType(ts, expect(ts, j, '->', "after a function type's parameters"));
                return [form(at, sym('fun', at), list(...params), result), k];
            }
            break;
        case 'kw': {
            const tag = quote(sym(t.text, at), at);
            if (!isP(ts[i + 1], '(')) return [tag, i + 1];
            const [items, rest, j] = parseTypeItems(ts, i + 2, ')');
            return [form(at, tag, ...items, ...(rest === null ? [] : [sym('&', at), rest])), j];
        }
        case 'punct':
            if (t.text === '(') {
                const [x, j] = parseType(ts, i + 1);
                return [x, expect(ts, j, ')', 'to close the parenthesis')];
            }
            if (t.text === '[') {
                const [items, rest, j] = parseTypeItems(ts, i + 1, ']');
                if (items.length === 0) return [rest ?? NIL, j];
                return [form(at, sym('shape', at), ...items, ...(rest === null ? [] : [sym('&', at), rest])), j];
            }
            if (t.text === '#(') {
                const [items, rest, j] = parseTypeItems(ts, i + 1, ')');
                if (rest !== null) throw new CompileError("a tuple type has no '...'", at);
                return [form(at, sym('tuple', at), ...items), j];
            }
            break;
    }
    throw new CompileError(`expected a type, not ${describe(t)}`, at);
}

// @include("path") or @include(:name)
function parseInclude(ts: Ts, i: number): [Sexp, number] {
    const at = ts[i]!.pos;
    const j  = expect(ts, i + 1, '(', 'after @include');
    const t  = ts[j];
    let arg: Sexp;
    if (t?.kind === 'str') arg = str(t.text, t.pos);
    else if (t?.kind === 'kw') arg = quote(sym(t.text, t.pos), t.pos);
    else throw new CompileError(`@include takes a path ("file.sly") or a built-in (:fs), not ${describe(t)}`, t?.pos ?? at);
    return [form(at, sym('@include', at), arg), expect(ts, j + 1, ')', 'after what @include takes')];
}

const quote = (x: Sexp, pos: Pos): Sexp => form(pos, sym('quote', pos), x);

// A body can't be empty: an empty block is ().
const nonEmpty = (forms: readonly Sexp[]): readonly Sexp[] => forms.length === 0 ? [NIL] : forms;

// ---- blocks ---------------------------------------------------------------

// { stmt; stmt; expr }: the forms of a body. i is at the `{`.
function parseBlock(ts: Ts, i: number): [readonly Sexp[], number] {
    return parseStatements(ts, expect(ts, i, '{', 'to begin a block'));
}

// Statements up to the `}`, and past it.
function parseStatements(ts: Ts, i: number): [readonly Sexp[], number] {
    const out: Sexp[] = [];
    for (;;) {
        const t = ts[i];
        if (t === undefined) throw new CompileError("expected '}' to end a block, not the end of the file", ts.at(-1)?.pos ?? null);
        if (isP(t, '}')) return [out, i + 1];
        if (isP(t, ';')) {
            i++;
            continue;
        }
        if (isW(t, 'fun') && ts[i + 1]?.kind === 'name') throw new CompileError('a function is defined at the top level only', t.pos);
        if (isW(t, '@include')) throw new CompileError('@include is only allowed at the top level', t.pos);
        if (isW(t, 'let')) {
            const [pattern, j] = parsePattern(ts, i + 1);
            const [e, k] = parseLetValue(ts, j, 'after what a let binds');
            if (pattern.t === 'sym' && !isSym(pattern, '_')) {
                out.push(form(t.pos, sym('let', t.pos), pattern, e));
                i = isP(ts[k], '}') ? k : endItem(ts, k, 'after a let');
                continue;
            }
            // A let that takes a value apart is a match over the rest of
            // the block, which is the match's one clause.
            const [rest, end] = parseStatements(ts, endItem(ts, k, 'after a let'));
            if (rest.length === 0) throw new CompileError('a let that takes a value apart needs something after it to bind for', t.pos);
            out.push(form(t.pos, sym('match', t.pos), e, list(pattern, ...rest)));
            return [out, end];
        }
        const [x, j] = parseExpr(ts, i);
        out.push(x);
        i = isP(ts[j], '}') ? j : endItem(ts, j, 'between statements');
    }
}

// A block's forms as one expression.
function blockExpr(forms: readonly Sexp[], at: Pos): Sexp {
    if (forms.length === 0) return NIL;
    if (forms.length === 1 && !isForm(forms[0]!, 'let')) return forms[0]!;
    return form(at, sym('do', at), ...forms);
}

// What an arm, or an if's branch, runs: a block's forms, or one expression.
function parseBody(ts: Ts, i: number): [readonly Sexp[], number] {
    if (isP(ts[i], '{')) {
        const [forms, j] = parseBlock(ts, i);
        return [nonEmpty(forms), j];
    }
    const [x, j] = parseExpr(ts, i);
    return [[x], j];
}

// ---- expressions ----------------------------------------------------------

type Op = { readonly prec: number; readonly chain: boolean; readonly name: string };

// The binary operators, loosest first; those that don't chain can't be
// written a < b < c.
const OPS: Readonly<Record<string, Op>> = {
    '|>': { prec: 1, chain: true,  name: '|>' },
    '||': { prec: 2, chain: true,  name: 'or' },
    '&&': { prec: 3, chain: true,  name: 'and' },
    '==': { prec: 4, chain: false, name: 'eq?' },
    '!=': { prec: 4, chain: false, name: 'ne?' },
    '<':  { prec: 5, chain: false, name: '<' },
    '<=': { prec: 5, chain: false, name: '<=' },
    '>':  { prec: 5, chain: false, name: '>' },
    '>=': { prec: 5, chain: false, name: '>=' },
    '+':  { prec: 6, chain: true,  name: '+' },
    '-':  { prec: 6, chain: true,  name: '-' },
    '++': { prec: 6, chain: true,  name: '~' },
    '*':  { prec: 7, chain: true,  name: '*' },
    '/':  { prec: 7, chain: true,  name: '/' },
    '%':  { prec: 7, chain: true,  name: '%' },
};

const opAt = (t: Token | undefined): Op | null =>
    t !== undefined && t.kind === 'punct' && Object.hasOwn(OPS, t.text) ? OPS[t.text]! : null;

export function parseExpr(ts: Ts, i: number): [Sexp, number] {
    return parseBinary(ts, i, 1);
}

function parseBinary(ts: Ts, i: number, min: number): [Sexp, number] {
    let [left, j] = parseUnary(ts, i);
    for (;;) {
        const t  = ts[j];
        const op = opAt(t);
        if (op === null || op.prec < min || (t!.text === '-' && freshLine(ts, j))) return [left, j];
        const [right, k] = parseBinary(ts, j + 1, op.prec + 1);
        left = binary(op, left, right, t!.pos);
        j = k;
        const next = opAt(ts[j]);
        if (!op.chain && next !== null && next.prec === op.prec) {
            throw new CompileError(`'${ts[j]!.text}' can't follow '${t!.text}' without parentheses: write && between comparisons`, ts[j]!.pos);
        }
    }
}

function binary(op: Op, left: Sexp, right: Sexp, at: Pos): Sexp {
    if (op.name !== '|>') return form(at, sym(op.name, at), left, right);
    // x |> f is (f x), and x |> f(a) is (f a x)
    if (right.t === 'sym') return form(at, right, left);
    if (right.t === 'pair' && !isForm(right, 'quote')) return cons(right.car, appendOne(right.cdr, left), right.pos);
    throw new CompileError("the right of '|>' is a function or a call", at);
}

const appendOne = (xs: Sexp, x: Sexp): Sexp => xs.t === 'pair' ? cons(xs.car, appendOne(xs.cdr, x), xs.pos) : cons(x, NIL);

function parseUnary(ts: Ts, i: number): [Sexp, number] {
    const t = ts[i];
    if (isP(t, '-')) {
        const n = ts[i + 1];
        if (n?.kind === 'num') return [number('-' + n.text, t!.pos), i + 2];
        const [x, j] = parseUnary(ts, i + 1);
        return [form(t!.pos, sym('-', t!.pos), int(0n, t!.pos), x), j];
    }
    if (isP(t, '!')) {
        const [x, j] = parseUnary(ts, i + 1);
        return [form(t!.pos, sym('not', t!.pos), x), j];
    }
    return parsePostfix(ts, i);
}

// f(a)(b): calls of whatever the primary is.
function parsePostfix(ts: Ts, i: number): [Sexp, number] {
    let [x, j] = parsePrimary(ts, i);
    while (isP(ts[j], '(') && !freshLine(ts, j)) {
        const [args, k] = parseItems(ts, j + 1, ')', 'arguments');
        if (args.some((a) => a.spread)) throw new CompileError("'...' is for lists, not calls: use apply", ts[j]!.pos);
        x = cons(x, list(...args.map((a) => a.x)), posOf(x));
        j = k;
    }
    return [x, j];
}

function number(text: string, pos: Pos): Sexp {
    if (numberKind(text) === 'int') {
        const n = BigInt(text);
        if (!fitsInt(n)) throw new CompileError(`integer out of range (${INT_MIN} to ${INT_MAX}): ${text}`, pos);
        return int(n, pos);
    }
    const v = Number(text);
    if (numberKind(text) !== 'float' || !Number.isFinite(v)) throw new CompileError(`invalid number '${text}'`, pos);
    return float(v, pos);
}

function parsePrimary(ts: Ts, i: number): [Sexp, number] {
    const t = ts[i];
    if (t === undefined) throw new CompileError('expected an expression, not the end of the file', ts.at(-1)?.pos ?? null);
    switch (t.kind) {
        case 'num':  return [number(t.text, t.pos), i + 1];
        case 'str':  return [str(t.text, t.pos), i + 1];
        case 'name': return [sym(t.text, t.pos), i + 1];
        case 'quoted': return [quote(t.datum, t.pos), i + 1];
        case 'kw':
            if (isP(ts[i + 1], '(')) return parseTagged(ts, i);
            return [quote(sym(t.text, t.pos), t.pos), i + 1];
        case 'word':
            return parseWord(ts, i, t.text, t.pos);
        case 'punct':
            switch (t.text) {
                case '[': {
                    const [items, j] = parseItems(ts, i + 1, ']', 'list elements');
                    return [listOf(items, [], t.pos), j];
                }
                case '#(': {
                    const [items, j] = parseItems(ts, i + 1, ')', 'tuple elements');
                    if (items.some((a) => a.spread)) throw new CompileError("'...' is for lists, not tuples", t.pos);
                    return [form(t.pos, sym('tuple', t.pos), ...items.map((a) => a.x)), j];
                }
                case '{': {
                    const [forms, j] = parseBlock(ts, i);
                    return [blockExpr(forms, t.pos), j];
                }
                case '(': {
                    const [x, j] = parseExpr(ts, i + 1);
                    return [x, expect(ts, j, ')', 'to close the parenthesis')];
                }
            }
    }
    throw new CompileError(`expected an expression, not ${describe(t)}`, t.pos);
}

function parseWord(ts: Ts, i: number, w: string, at: Pos): [Sexp, number] {
    switch (w) {
        case 'true':   return [sym('#true', at), i + 1];
        case 'false':  return [sym('#false', at), i + 1];
        case 'self':   return [sym('$$', at), i + 1];
        case 'parent': return [sym('^$$', at), i + 1];
        case '@ARGV':  return [sym('@ARGV', at), i + 1];
        case '%ENV':   return [sym('%ENV', at), i + 1];
        case 'spawn':
        case 'fork':
        case 'yield': {
            const [x, j] = parseExpr(ts, i + 1);
            return [form(at, sym(w, at), x), j];
        }
        case 'fun': {
            if (ts[i + 1]?.kind === 'name') throw new CompileError('a function is defined at the top level only', at);
            const [params, j0] = parseParams(ts, i + 1);
            let j = j0;
            let result: Sexp | null = null;
            if (isP(ts[j], ':')) [result, j] = parseType(ts, j + 1);
            let lambda: Sexp;
            let k: number;
            if (isP(ts[j], '=>')) {
                const [x, k1] = parseExpr(ts, j + 1);
                [lambda, k] = [form(at, sym('lambda', at), params.names, x), k1];
            } else {
                const [body, k1] = parseBlock(ts, j);
                [lambda, k] = [form(at, sym('lambda', at), params.names, ...nonEmpty(body)), k1];
            }
            if (result === null && params.types.every((t) => t === null)) return [lambda, k];
            // a lambda with types is one, (the (fun (T ...) R) (lambda ...))
            const type = form(at, sym('fun', at), list(...params.types.map((t) => t ?? sym('any', at))), result ?? sym('any', at));
            return [form(at, sym('the', at), type, lambda), k];
        }
        case 'connect': {
            const [args, j] = parseItems(ts, expect(ts, i + 1, '(', 'after connect'), ')', 'what connect connects to');
            const [body, k] = parseBlock(ts, j);
            return [form(at, sym('connect', at), ...args.map((a) => a.x), blockExpr(body, at)), k];
        }
        case 'if':
            return parseIf(ts, i);
        case 'cond': {
            const [arms, j] = parseArms(ts, i + 1, 'test', at, parseExpr);
            return [form(at, sym('cond', at), ...arms), j];
        }
        case 'case':
        case 'match': {
            const [topic, j] = parseExpr(ts, i + 1);
            const [arms, k] = w === 'case' ? parseArms(ts, j, 'value', at, parseCaseValue) : parseArms(ts, j, 'pattern', at, parsePattern);
            return [form(at, sym(w, at), topic, ...arms), k];
        }
        case 'recv': {
            if (!isP(ts[i + 1], '{')) return [form(at, sym('recv', at)), i + 1];
            const [arms, j] = parseArms(ts, i + 1, 'pattern', at, parsePattern);
            return [form(at, sym('recv', at), ...arms), j];
        }
    }
    throw new CompileError(`expected an expression, not '${w}'`, at);
}

// if c { a } else if d { b } else { e }
function parseIf(ts: Ts, i: number): [Sexp, number] {
    const at = ts[i]!.pos;
    const [test, j] = parseExpr(ts, i + 1);
    if (!isP(ts[j], '{')) throw new CompileError(`expected '{' after an if's test, not ${describe(ts[j])}`, ts[j]?.pos ?? at);
    const [then, k] = parseBlock(ts, j);
    const thenX = blockExpr(then, ts[j]!.pos);
    if (!isW(ts[k], 'else')) return [form(at, sym('if', at), test, thenX), k];
    if (isW(ts[k + 1], 'if')) {
        const [rest, m] = parseIf(ts, k + 1);
        return [form(at, sym('if', at), test, thenX, rest), m];
    }
    const [els, m] = parseBlock(ts, k + 1);
    return [form(at, sym('if', at), test, thenX, blockExpr(els, ts[k + 1]!.pos)), m];
}

// case's value: `_` is the default, #true.
function parseCaseValue(ts: Ts, i: number): [Sexp, number] {
    const t = ts[i];
    if (t?.kind === 'name' && t.text === '_') return [sym('#true', t.pos), i + 1];
    return parseExpr(ts, i);
}

// { head => body, ... }, each arm as (head body...), head being what
// says (a test, a value or a pattern). After a body that ends with `}`,
// the comma can be left out.
function parseArms(ts: Ts, i: number, what: string, at: Pos,
                   head: (ts: Ts, i: number) => [Sexp, number]): [readonly Sexp[], number] {
    i = expect(ts, i, '{', 'to begin the arms');
    const arms: Sexp[] = [];
    while (!isP(ts[i], '}')) {
        if (ts[i] === undefined) throw new CompileError("expected '}' to end the arms, not the end of the file", at);
        const armAt = ts[i]!.pos;
        const [h, j] = head(ts, i);
        const [body, k] = parseBody(ts, expect(ts, j, '=>', `after an arm's ${what}`));
        arms.push(form(armAt, h, ...body));
        if (isP(ts[k], ',')) i = k + 1;
        else if (isP(ts[k], '}') || isP(ts[k - 1], '}')) i = k;
        else throw new CompileError(`expected ',' or '}' after an arm, not ${describe(ts[k])}`, ts[k]?.pos ?? at);
    }
    return [arms, i + 1];
}

// ---- data -----------------------------------------------------------------

type Item = { readonly x: Sexp; readonly spread: boolean };

// Expressions up to close, each maybe after `...`, and past close.
function parseItems(ts: Ts, i: number, close: string, what: string): [readonly Item[], number] {
    const items: Item[] = [];
    while (!isP(ts[i], close)) {
        const spread = isP(ts[i], '...');
        const [x, j] = parseExpr(ts, spread ? i + 1 : i);
        items.push({ x, spread });
        i = isP(ts[j], close) ? j : expect(ts, j, ',', `between ${what}`);
    }
    return [items, i + 1];
}

// :tag(a, b)
function parseTagged(ts: Ts, i: number): [Sexp, number] {
    const t = ts[i]!;
    const [items, j] = parseItems(ts, i + 2, ')', "a tag's elements");
    return [listOf(items, [quote(sym(t.text, t.pos), t.pos)], t.pos), j];
}

// A list of head and items: a constant if everything in it is a literal,
// a chain of conses if it ends in a spread, and a call of list otherwise.
function listOf(items: readonly Item[], head: readonly Sexp[], at: Pos): Sexp {
    const spreads = items.filter((a) => a.spread).length;
    if (spreads > 1 || (spreads === 1 && !items.at(-1)!.spread)) {
        throw new CompileError("'...' can only be last in a list: a list is added to at its front", at);
    }
    const xs = [...head, ...items.map((a) => a.x)];
    if (xs.length === 0) return NIL;
    if (spreads === 1) {
        return xs.slice(0, -1).reduceRight<Sexp>((tail, x) => form(at, sym('cons', at), x, tail), xs.at(-1)!);
    }
    if (xs.every(isLiteral)) return quote(list(...xs.map(datum)), at);
    return form(at, sym('list', at), ...xs);
}

// What reads the same quoted as evaluated: numbers, strings, booleans, (),
// and quoted data.
const isLiteral = (x: Sexp): boolean =>
    x.t === 'int' || x.t === 'float' || x.t === 'str' || x.t === 'nil'
    || isSym(x, '#true') || isSym(x, '#false') || isForm(x, 'quote');

const datum = (x: Sexp): Sexp => isForm(x, 'quote') ? (x as { cdr: { car: Sexp } }).cdr.car : x;

// ---- patterns -------------------------------------------------------------

export function parsePattern(ts: Ts, i: number): [Sexp, number] {
    const t = ts[i];
    if (t === undefined) throw new CompileError('expected a pattern, not the end of the file', ts.at(-1)?.pos ?? null);
    switch (t.kind) {
        case 'name':
            if (!t.plain) throw new CompileError(`a pattern binds a plain name, not '${t.text}'`, t.pos);
            return [sym(t.text, t.pos), i + 1];
        case 'num':  return [number(t.text, t.pos), i + 1];
        case 'str':  return [str(t.text, t.pos), i + 1];
        case 'quoted': return [quote(t.datum, t.pos), i + 1];
        case 'kw':
            if (isP(ts[i + 1], '(')) return parseListPattern(ts, i + 2, ')', [quote(sym(t.text, t.pos), t.pos)], t.pos);
            return [quote(sym(t.text, t.pos), t.pos), i + 1];
        case 'word':
            if (t.text === 'true')  return [sym('#true', t.pos), i + 1];
            if (t.text === 'false') return [sym('#false', t.pos), i + 1];
            break;
        case 'punct':
            if (t.text === '-' && ts[i + 1]?.kind === 'num') return [number('-' + ts[i + 1]!.text, t.pos), i + 2];
            if (t.text === '[') return parseListPattern(ts, i + 1, ']', [], t.pos);
            if (t.text === '#(') {
                const [xs, j] = parseListPattern(ts, i + 1, ')', [sym(TUPLE, t.pos)], t.pos);
                if (toArr(xs).some((x) => isSym(x, '&'))) throw new CompileError("a tuple pattern has no '...'", t.pos);
                return [xs, j];
            }
    }
    throw new CompileError(`expected a pattern, not ${describe(t)}`, t.pos);
}

function toArr(xs: Sexp): readonly Sexp[] {
    const out: Sexp[] = [];
    for (; xs.t === 'pair'; xs = xs.cdr) out.push(xs.car);
    return out;
}

// [p, q, ...rest] as (p q & rest), after head.
function parseListPattern(ts: Ts, i: number, close: string, head: readonly Sexp[], at: Pos): [Sexp, number] {
    const xs: Sexp[] = [...head];
    while (!isP(ts[i], close)) {
        if (isP(ts[i], '...')) {
            const [rest, j] = parsePattern(ts, i + 1);
            xs.push(sym('&', ts[i]!.pos), rest);
            return [form(at, ...xs), expect(ts, j, close, "after a pattern's '...'")];
        }
        const [p, j] = parsePattern(ts, i);
        xs.push(p);
        i = isP(ts[j], close) ? j : expect(ts, j, ',', 'between patterns');
    }
    return [xs.length === 0 ? NIL : form(at, ...xs), i + 1];
}
