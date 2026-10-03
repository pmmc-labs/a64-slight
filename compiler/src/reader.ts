// The reader: source text to s-expressions with positions.
//
// Ported from ts-cpi's reader (reference/ts-cpi/src/reader.ts), with
// slight's differences:
//   - `#true` and `#false` read as symbols (booleans are reserved symbols)
//   - integers are 63-bit
//   - strings also accept \r and \e (ESC), for terminal escape sequences
//   - quasiquote and unquote (` , ,@) are not part of slight
//
// `:name` reads as (quote name), positioned at the colon, as in ts-slight
// and ts-cpi; `'x` reads as (quote x).

import { CompileError } from './errors.ts';
import { NIL, cons, float, fitsInt, int, reverse, str, sym, INT_MAX, INT_MIN, type Pos, type Sexp } from './sexp.ts';

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

type Token =
    | { readonly kind: 'open' | 'close' | 'quote'; readonly pos: Pos }
    | { readonly kind: 'keyword' | 'atom' | 'string'; readonly text: string; readonly pos: Pos };

type Cursor = { readonly src: string; readonly file: string; readonly i: number; readonly line: number; readonly col: number };

const peek  = (c: Cursor): string => c.src[c.i] ?? '';
const atEnd = (c: Cursor): boolean => c.i >= c.src.length;
const posOf = (c: Cursor): Pos => ({ file: c.file, line: c.line, col: c.col });
const step  = (c: Cursor): Cursor => peek(c) === '\n'
    ? { ...c, i: c.i + 1, line: c.line + 1, col: 1 }
    : { ...c, i: c.i + 1, col: c.col + 1 };

const isSpace     = (ch: string): boolean => ch === ' ' || ch === '\t' || ch === '\n' || ch === '\r';
const isDelimiter = (ch: string): boolean => isSpace(ch) || '()\';"`,'.includes(ch);
const isDigit     = (ch: string): boolean => ch >= '0' && ch <= '9';

function skipWhile(c: Cursor, pred: (ch: string) => boolean): Cursor {
    while (!atEnd(c) && pred(peek(c))) c = step(c);
    return c;
}

function tokenize(src: string, file: string): readonly Token[] {
    const tokens: Token[] = [];
    let c: Cursor = { src, file, i: 0, line: 1, col: 1 };
    while (!atEnd(c)) {
        const [token, next] = nextToken(c);
        if (token !== null) tokens.push(token);
        c = next;
    }
    return tokens;
}

// The token starting at c (null for whitespace and comments), and the cursor after it.
function nextToken(c: Cursor): [Token | null, Cursor] {
    const ch  = peek(c);
    const pos = posOf(c);
    if (isSpace(ch))  return [null, skipWhile(c, isSpace)];
    if (ch === ';')   return [null, skipWhile(c, (x) => x !== '\n')];
    if (ch === '(')   return [{ kind: 'open', pos }, step(c)];
    if (ch === ')')   return [{ kind: 'close', pos }, step(c)];
    if (ch === "'")   return [{ kind: 'quote', pos }, step(c)];
    if (ch === '`' || ch === ',') throw new CompileError(`quasiquote and unquote (${ch}) are not part of slight`, pos);
    if (ch === '"')   return readString(step(c), pos);
    if (ch === ':') {
        const end  = skipWhile(step(c), (x) => !isDelimiter(x));
        const text = c.src.slice(c.i + 1, end.i);
        // `:12`, `:1.5`, `:#x`, `::x` and a lone `:` are mistakes, not keywords
        if (text === '' || numberKind(text) !== null || text.startsWith('#') || text.startsWith(':')) {
            throw new CompileError(`invalid keyword ':${text}'`, pos);
        }
        return [{ kind: 'keyword', text, pos }, end];
    }
    const end = skipWhile(c, (x) => !isDelimiter(x));
    return [{ kind: 'atom', text: c.src.slice(c.i, end.i), pos }, end];
}

const ESCAPES: Readonly<Record<string, string>> = { '"': '"', '\\': '\\', n: '\n', t: '\t', r: '\r', e: '\x1b' };

// c is just past the opening quote.
function readString(c: Cursor, start: Pos): [Token, Cursor] {
    let text = '';
    for (;;) {
        if (atEnd(c)) throw new CompileError('unterminated string', start);
        const ch = peek(c);
        if (ch === '"') return [{ kind: 'string', text, pos: start }, step(c)];
        if (ch !== '\\') {
            text += ch;
            c = step(c);
            continue;
        }
        const escPos = posOf(c);
        c = step(c);
        const esc = peek(c);
        if (esc === 'u') {
            const [codePoint, next] = readUnicodeEscape(step(c), escPos);
            text += codePoint;
            c = next;
        } else if (ESCAPES[esc] !== undefined) {
            text += ESCAPES[esc];
            c = step(c);
        } else {
            throw new CompileError(atEnd(c) ? 'unterminated string' : `invalid escape '\\${esc}'`, atEnd(c) ? start : escPos);
        }
    }
}

// \u{hex}: c is just past the `u`.
function readUnicodeEscape(c: Cursor, escPos: Pos): [string, Cursor] {
    if (peek(c) !== '{') throw new CompileError('invalid unicode escape, expected \\u{hex}', escPos);
    const end = skipWhile(step(c), (x) => x !== '}' && x !== '"');
    if (peek(end) !== '}') throw new CompileError('unterminated unicode escape', escPos);
    const hex   = c.src.slice(c.i + 1, end.i);
    const isHex = (ch: string): boolean => isDigit(ch) || (ch.toLowerCase() >= 'a' && ch.toLowerCase() <= 'f');
    const n     = hex !== '' && hex.length <= 6 && [...hex].every(isHex) ? parseInt(hex, 16) : NaN;
    if (!(n <= 0x10ffff)) throw new CompileError(`invalid unicode escape '\\u{${hex}}'`, escPos);
    return [String.fromCodePoint(n), step(end)];
}

// -?D+ is an int. -?D+.D+ is a float, with an optional exponent e[+-]?D+:
// both sides of the point need digits, as in ts-cpi. Anything else is null.
function numberKind(text: string): 'int' | 'float' | null {
    const pastDigits = (i: number): number => isDigit(text[i] ?? '') ? pastDigits(i + 1) : i;
    const end = (i: number, kind: 'int' | 'float'): 'int' | 'float' | null => i === text.length ? kind : null;

    const a = text.startsWith('-') ? 1 : 0;
    const b = pastDigits(a);
    if (b === a) return null;
    if (text[b] !== '.') return end(b, 'int');
    const c = pastDigits(b + 1);
    if (c === b + 1) return null;
    if (text[c] !== 'e' && text[c] !== 'E') return end(c, 'float');
    const d = text[c + 1] === '-' || text[c + 1] === '+' ? c + 2 : c + 1;
    const e = pastDigits(d);
    return e === d ? null : end(e, 'float');
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

// Every top-level form in `src`, as a list.
export function read(src: string, file: string): Sexp {
    const tokens = tokenize(src, file);
    let forms: Sexp = NIL;
    let i = 0;
    while (i < tokens.length) {
        const [form, next] = parseDatum(tokens, i);
        forms = cons(form, forms);
        i = next;
    }
    return reverse(forms);
}

function parseDatum(tokens: readonly Token[], i: number): [Sexp, number] {
    const t = tokens[i]!;
    switch (t.kind) {
        case 'open':
            return parseList(tokens, i + 1, t.pos);
        case 'close':
            throw new CompileError("unexpected ')'", t.pos);
        case 'quote': {
            if (i + 1 >= tokens.length) throw new CompileError("expected something to quote after '", t.pos);
            const [quoted, next] = parseDatum(tokens, i + 1);
            return [quote(quoted, t.pos), next];
        }
        case 'keyword':
            return [quote(sym(t.text, t.pos), t.pos), i + 1];
        case 'string':
            return [str(t.text, t.pos), i + 1];
        case 'atom':
            return [parseAtom(t.text, t.pos), i + 1];
    }
}

const quote = (x: Sexp, pos: Pos): Sexp => cons(sym('quote', pos), cons(x, NIL), pos);

// i is just past the `(`.
function parseList(tokens: readonly Token[], i: number, open: Pos): [Sexp, number] {
    let items: Sexp = NIL;
    for (;;) {
        const t = tokens[i];
        if (t === undefined) throw new CompileError('unterminated list', open);
        if (t.kind === 'close') {
            const xs = reverse(items);
            return [xs.t === 'pair' ? cons(xs.car, xs.cdr, open) : NIL, i + 1];
        }
        const [item, next] = parseDatum(tokens, i);
        items = cons(item, items);
        i = next;
    }
}

function parseAtom(text: string, pos: Pos): Sexp {
    if (text === '.') throw new CompileError('dotted pairs are not part of slight', pos);
    if (text.startsWith('#')) {
        if (text === '#true' || text === '#false') return sym(text, pos);
        throw new CompileError(`unknown literal '${text}'`, pos);
    }
    switch (numberKind(text)) {
        case 'int': {
            const n = BigInt(text);
            if (!fitsInt(n)) throw new CompileError(`integer out of range (${INT_MIN} to ${INT_MAX}): ${text}`, pos);
            return int(n, pos);
        }
        case 'float': {
            const n = Number(text);
            if (!Number.isFinite(n)) throw new CompileError(`float out of range: ${text}`, pos);
            return float(n, pos);
        }
        case null:
            return sym(text, pos);
    }
}
