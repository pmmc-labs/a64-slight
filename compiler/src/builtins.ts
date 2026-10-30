// The builtins' types, for the checker (check.ts, D247), as
// docs/LANGUAGE.md has them. Their arities are code generation's
// (codegen.ts), which a call that gets one wrong never gets past; here
// is what each argument must be, and what comes back.
//
// An argument's type is what the builtin takes without faulting: a value
// that can't be one faults, and the kind of fault follows from the type
// (faultFor). A result is a type, or worked out from the arguments'
// types, as the arithmetic's is. Where a builtin takes more than it says
// it does, its type says so; where it isn't clear, any, since a wrong
// type here is a warning on a program that works.

import {
    ANY, BOOL, CONS_ANY, FLOAT, FUN_ANY, INT, LIST_ANY, NEVER, NIL_T, NUM, PID, STR, SYM, TUPLE_ANY,
    addressOf, arrayOf, consOf, fits, headOf, lit, listOf, meet, shape, tagged, tailOf, tupleElement, tupleOf, union,
    type Defs, type Ty,
} from './types.ts';

export type Builtin = {
    readonly params: readonly Ty[];
    readonly rest: Ty | null;         // what each argument past params is, for those that take any number
    readonly result: Ty | ((args: readonly Ty[], defs: Defs) => Ty);
};

const fn = (params: readonly Ty[], result: Builtin['result']): Builtin => ({ params, rest: null, result });
const variadic = (rest: Ty, result: Builtin['result']): Builtin => ({ params: [], rest, result });

// + - *: two integers make one, a float either side a float. With
// anything not known, nothing is: any, not num, so a value worked out
// from one isn't held to be a float where an integer goes (--strict).
function arith(args: readonly Ty[], defs: Defs): Ty {
    const ts = args.map((a) => meet(a, NUM, defs));
    if (ts.some((t) => t.k === 'never')) return NEVER;
    if (args.some((t) => t.k === 'any')) return ANY;
    if (ts.every((t) => fits(t, INT, defs) && t.k !== 'any')) return INT;
    if (ts.some((t) => t.k === 'float')) return FLOAT;
    return NUM;
}

// What math/abs keeps: the type it's given.
const keepNum = (args: readonly Ty[], defs: Defs): Ty => {
    const t = args[0] ?? ANY;
    return t.k === 'any' ? ANY : meet(t, NUM, defs);
};

const either = (args: readonly Ty[], defs: Defs): Ty => union(...args.map((a) => (a.k === 'any' ? ANY : meet(a, NUM, defs))));

const okOr = (ok: Ty, error: Ty): Ty => union(tagged('ok', ok), tagged('error', error));

// Arrays of numbers (D253): any array; an argument's array type, from
// what it's given; its elements; and the array a kind names.
const ARRAY: Ty = arrayOf(NUM);
const arrayArg = (t: Ty | undefined, defs: Defs): Ty => {
    const a = meet(t ?? ANY, ARRAY, defs);
    return a.k === 'never' ? ARRAY : a;
};
const elementsOf = (t: Ty): Ty => (t.k === 'array' ? t.of : t.k === 'or' ? union(...t.of.map(elementsOf)) : NUM);
const KIND: Ty = union(lit('float'), lit('int'));
const ofKind = (args: readonly Ty[]): Ty => {
    const k = args[0];
    return k?.k === 'lit' && k.name === 'float' ? arrayOf(FLOAT) : k?.k === 'lit' && k.name === 'int' ? arrayOf(INT) : ARRAY;
};
// Element by element: the arrays among the operands.
const operands = (args: readonly Ty[], defs: Defs): Ty => {
    const as = args.map((a) => meet(a, ARRAY, defs)).filter((a) => a.k !== 'never');
    return as.length === 0 ? ARRAY : union(...as);
};
const ARRAY_OR_NUM: Ty = union(ARRAY, NUM);

// The prelude's Json (lib/prelude.slight).
const JSON: Ty = { k: 'alias', name: 'Json', args: [] };

// What join and read give for a process.
export const RESULT_ANY = union(okOr(ANY, ANY), lit('ended'));

const MATH_FLOAT = ['sqrt', 'cbrt', 'exp', 'expm1', 'log', 'log2', 'log10', 'log1p',
    'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh'];

const STR_TO_STR = ['trim', 'trim-start', 'trim-end', 'upper', 'lower', 'to-well-formed'];

export const BUILTINS: Readonly<Record<string, Builtin>> = {
    '+': fn([NUM, NUM], arith),
    '-': fn([NUM, NUM], arith),
    '*': fn([NUM, NUM], arith),
    '/': fn([NUM, NUM], FLOAT),
    'div': fn([INT, INT], INT),
    '%': fn([INT, INT], INT),
    '==': fn([NUM, NUM], BOOL), '!=': fn([NUM, NUM], BOOL),
    '<': fn([NUM, NUM], BOOL), '<=': fn([NUM, NUM], BOOL),
    '>': fn([NUM, NUM], BOOL), '>=': fn([NUM, NUM], BOOL),
    'eq?': fn([ANY, ANY], BOOL), 'ne?': fn([ANY, ANY], BOOL),
    'hash': fn([ANY], INT),
    'pprint': fn([ANY], NIL_T),

    'bit/and': fn([INT, INT], INT), 'bit/or': fn([INT, INT], INT), 'bit/xor': fn([INT, INT], INT),
    'bit/shl': fn([INT, INT], INT), 'bit/shr': fn([INT, INT], INT), 'bit/not': fn([INT], INT),
    'bit/count': fn([INT], INT),

    'math/ceil': fn([NUM], INT), 'math/floor': fn([NUM], INT), 'math/round': fn([NUM], INT), 'math/trunc': fn([NUM], INT),
    'math/abs': fn([NUM], keepNum),
    'math/min': fn([NUM, NUM], either), 'math/max': fn([NUM, NUM], either),
    'math/pow': fn([NUM, NUM], arith),
    'math/atan2': fn([NUM, NUM], FLOAT), 'math/hypot': fn([NUM, NUM], FLOAT),
    'math/mod': fn([INT, INT], INT),
    'math/nan?': fn([ANY], BOOL),
    ...Object.fromEntries(MATH_FLOAT.map((name) => [`math/${name}`, fn([NUM], FLOAT)])),
    'int->float': fn([INT], FLOAT),
    'float->str': fn([NUM, INT], STR),
    'rand/int': fn([INT], INT), 'rand/float': fn([], FLOAT), 'rand/seed': fn([INT], NIL_T),

    'cons': fn([ANY, LIST_ANY], (args, defs) => consOf(args[0] ?? ANY, meet(args[1] ?? ANY, LIST_ANY, defs))),
    'list': variadic(ANY, (args) => shape(args)),
    'tuple': variadic(ANY, (args) => tupleOf(args)),
    'apply': fn([FUN_ANY, LIST_ANY], (args) => resultOf(args[0] ?? ANY)),

    'str': variadic(ANY, STR),
    '~': variadic(STR, STR),
    'str/len': fn([STR], INT),
    'str/at': fn([STR, INT], STR),
    'str/code-at': fn([STR, INT], INT),
    'str/slice': fn([STR, INT, INT], STR),
    'str/index-of': fn([STR, STR, INT], INT),
    'str/last-index-of': fn([STR, STR, INT], INT),
    'str/includes?': fn([STR, STR], BOOL),
    'str/starts-with?': fn([STR, STR], BOOL),
    'str/ends-with?': fn([STR, STR], BOOL),
    // with no limit, at least one piece: (str/split "" ",") is ("")
    'str/split': fn([STR, STR, INT], (args) => (args.length === 2 ? consOf(STR, listOf(STR)) : listOf(STR))),
    'str/join': fn([STR, LIST_ANY], STR),
    'str/repeat': fn([STR, INT], STR),
    'str/pad-start': fn([STR, INT, STR], STR),
    'str/pad-end': fn([STR, INT, STR], STR),
    ...Object.fromEntries(STR_TO_STR.map((name) => [`str/${name}`, fn([STR], STR)])),
    'str/replace': fn([STR, STR, STR], STR),
    'str/replace-all': fn([STR, STR, STR], STR),
    'str/compare': fn([STR, STR], INT),
    'str/<': fn([STR, STR], BOOL),
    'str/chars': fn([STR], listOf(STR)),
    'str/codes': fn([STR], listOf(INT)),
    'str/well-formed?': fn([STR], BOOL),
    'codes->str': fn([listOf(INT)], STR),
    'str->int': fn([STR, INT], union(INT, lit('#false'))),
    'int->str': fn([INT, INT], STR),
    'str->float': fn([STR], union(FLOAT, lit('#false'))),
    'str->sym': fn([STR], union(SYM, lit('#false'))),
    'sym->str': fn([SYM], STR),
    'str->bytes': fn([STR], listOf(INT)),
    'bytes->str': fn([listOf(INT)], STR),
    'bytes/len': fn([STR], INT),
    'bytes/at': fn([STR, INT], INT),
    'bytes/slice': fn([STR, INT, INT], STR),
    'bytes/index-of': fn([STR, STR, INT], INT),

    'tty/write': variadic(ANY, NIL_T),
    'tty/screen/rows': fn([], INT), 'tty/screen/cols': fn([], INT),
    'json/parse': fn([STR], okOr(JSON, tagged('bad-json', INT))),
    'json/print': fn([ANY], STR),
    'sexp/parse': fn([STR], okOr(ANY, tagged('bad-sexp', INT))),
    'sexp/print': fn([ANY], STR),

    'tuple/len': fn([TUPLE_ANY], INT),
    'tuple/get': fn([TUPLE_ANY, INT], (args, defs) => tupleElement(args[0] ?? ANY, null, defs)),
    'tuple/set': fn([TUPLE_ANY, INT, ANY], (args, defs) => {
        const t = meet(args[0] ?? ANY, TUPLE_ANY, defs);
        return t.k === 'tuple' && t.of !== null ? tupleOf(t.of.map((e) => union(e, args[2] ?? ANY))) : TUPLE_ANY;
    }),
    'tuple/make': fn([INT, ANY], TUPLE_ANY),
    'tuple/insert': fn([TUPLE_ANY, INT, ANY], TUPLE_ANY),
    'tuple/remove': fn([TUPLE_ANY, INT], TUPLE_ANY),
    'tuple/slice': fn([TUPLE_ANY, INT, INT], TUPLE_ANY),
    'list->tuple': fn([LIST_ANY], TUPLE_ANY),
    'tuple->list': fn([TUPLE_ANY], (args, defs) => listOf(tupleElement(args[0] ?? ANY, null, defs))),

    'array/make': fn([KIND, INT, NUM], ofKind),
    'list->array': fn([KIND, listOf(NUM)], ofKind),
    'array->list': fn([ARRAY], (args, defs) => listOf(elementsOf(arrayArg(args[0], defs)))),
    'array/kind': fn([ARRAY], KIND),
    'array/len': fn([ARRAY], INT),
    'array/get': fn([ARRAY, INT], (args, defs) => elementsOf(arrayArg(args[0], defs))),
    'array/set': fn([ARRAY, INT, NUM], (args, defs) => arrayArg(args[0], defs)),
    'array/slice': fn([ARRAY, INT, INT], (args, defs) => arrayArg(args[0], defs)),
    'array/concat': fn([ARRAY, ARRAY], (args, defs) => arrayArg(args[0], defs)),
    'array/add': fn([ARRAY_OR_NUM, ARRAY_OR_NUM], operands),
    'array/sub': fn([ARRAY_OR_NUM, ARRAY_OR_NUM], operands),
    'array/mul': fn([ARRAY_OR_NUM, ARRAY_OR_NUM], operands),
    'array/div': fn([ARRAY_OR_NUM, ARRAY_OR_NUM], arrayOf(FLOAT)),
    'array/sum': fn([ARRAY], (args, defs) => elementsOf(arrayArg(args[0], defs))),
    'array/min': fn([ARRAY], (args, defs) => elementsOf(arrayArg(args[0], defs))),
    'array/max': fn([ARRAY], (args, defs) => elementsOf(arrayArg(args[0], defs))),
    'array/dot': fn([ARRAY, ARRAY], (args, defs) => elementsOf(arrayArg(args[0], defs))),
    'array/matmul': fn([ARRAY, ARRAY, INT, INT, INT], (args, defs) => arrayArg(args[0], defs)),

    'send': fn([PID, ANY], NIL_T),
    'join': fn([PID], RESULT_ANY),
    'join/race': fn([listOf(PID)], shape([PID, RESULT_ANY])),
    'address': fn([], addressOf(ANY)),
    'read': fn([PID], ANY),
    'read/any': fn([listOf(PID)], shape([PID, ANY])),
    'open': fn([SYM, ANY], okOr(PID, ANY)),
    'monitor': fn([PID], NIL_T),
    'kill': fn([PID], NIL_T),
    'raise': fn([ANY], NEVER),
    'after': fn([INT, PID, ANY], INT),
    'timer/cancel': fn([ANY], BOOL),
    'disconnect': fn([PID], NIL_T),
    'socket/peer': fn([PID], ANY),
    'sleep': fn([INT], NIL_T),
    'clock/us': fn([], INT),
    'clock/now': fn([], INT),
    'fs/list': fn([STR], okOr(listOf(STR), ANY)),
    'fs/stat': fn([STR], okOr(listOf(ANY), ANY)),
    'fs/delete': fn([STR], okOr(NIL_T, ANY)),
    'fs/rename': fn([STR, STR], okOr(NIL_T, ANY)),
    'fs/mkdir': fn([STR], okOr(NIL_T, ANY)),
    'bytes/pack': fn([LIST_ANY, LIST_ANY], STR),
    'crypto/sha256': fn([STR], STR),
    'crypto/sha512': fn([STR], STR),
    'crypto/hmac-sha256': fn([STR, STR], STR),
    'crypto/hmac-sha512': fn([STR, STR], STR),
    'crypto/equal?': fn([STR, STR], BOOL),
    'crypto/random-bytes': fn([INT], STR),
    'bytes->base64': fn([STR], STR),
    'base64->bytes': fn([STR], union(STR, lit('#false'))),
    'bytes->base64url': fn([STR], STR),
    'base64url->bytes': fn([STR], union(STR, lit('#false'))),
    'bytes->hex': fn([STR], STR),
    'hex->bytes': fn([STR], union(STR, lit('#false'))),
    'log': fn([union(lit('debug'), lit('info'), lit('warn'), lit('error')), STR, LIST_ANY], NIL_T),
    'bytes/unpack': fn([LIST_ANY, STR, INT], union(tagged('ok', LIST_ANY, INT), tagged('error', lit('short')))),
};

// The type predicates, and what each says yes to: in a test, they narrow.
export const PREDICATES: Readonly<Record<string, Ty>> = {
    'int?': INT, 'float?': FLOAT, 'num?': NUM, 'str?': STR, 'nil?': NIL_T, 'cons?': CONS_ANY,
    'sym?': SYM, 'bool?': BOOL, 'tuple?': TUPLE_ANY, 'pid?': PID, 'lambda?': FUN_ANY, 'array?': ARRAY,
};

// Names for constants (codegen.ts's CONSTANTS).
export const CONSTANTS: Readonly<Record<string, Ty>> = {
    '\\n': STR, '\\r': STR, '\\t': STR, '\\e': STR, 'math/PI': FLOAT, 'math/E': FLOAT, 'math/INF': FLOAT,
};

// car, cdr, and c[ad]{2,4}r: the steps, first taken first, or null.
export function cxrSteps(name: string): readonly ('a' | 'd')[] | null {
    const middle = name.slice(1, -1);
    const isCxr = name.length >= 3 && name.length <= 6 && name.startsWith('c') && name.endsWith('r')
        && [...middle].every((ch) => ch === 'a' || ch === 'd');
    return isCxr ? ([...middle].reverse() as ('a' | 'd')[]) : null;
}

// What c[ad]r of a value of type t gives; and, at the first step whose
// value can't be a cons, which step that is (for the warning).
export function cxr(t: Ty, steps: readonly ('a' | 'd')[], defs: Defs): { readonly result: Ty; readonly failsAt: number | null } {
    let cur = t;
    for (const [i, s] of steps.entries()) {
        if (cur.k === 'never') return { result: NEVER, failsAt: null };
        const c = meet(cur, CONS_ANY, defs);
        if (c.k === 'never') return { result: NEVER, failsAt: i };
        cur = s === 'a' ? headOf(c, defs) : tailOf(c, defs);
    }
    return { result: cur, failsAt: null };
}

// What calling a value of type t gives.
export function resultOf(t: Ty): Ty {
    switch (t.k) {
        case 'fun': return t.result;
        case 'or':  return union(...t.of.map(resultOf));
        case 'never': return NEVER;
        default:    return ANY;
    }
}

// The fault a value of type a that isn't a t makes, when a builtin
// wants a t: for a list of the wrong things, the things' fault.
export function faultOf(a: Ty, t: Ty, defs: Defs): string | null {
    if (t.k === 'list' && meet(a, LIST_ANY, defs).k !== 'never') return faultFor(t.of);
    return faultFor(t);
}

export function faultFor(t: Ty): string | null {
    switch (t.k) {
        case 'int':     return 'not-an-int';
        case 'float':   return 'not-a-number';
        case 'str':     return 'not-a-string';
        case 'sym': case 'lit': return 'not-a-symbol';
        case 'tuple':   return 'not-a-tuple';
        case 'array':   return 'not-an-array';
        case 'list': case 'nil': return 'not-a-list';
        case 'cons':    return 'not-a-cons';
        case 'fun':     return 'not-a-function';
        case 'pid': case 'task': case 'actor': case 'address': return 'not-a-pid';
        case 'or':
            if (t.of.every((x) => x.k === 'int' || x.k === 'float')) return 'not-a-number';
            if (t.of.every((x) => x.k === 'lit' && (x.name === '#true' || x.name === '#false'))) return 'not-a-bool';
            if (t.of.every((x) => x.k === 'lit')) return 'out-of-range';  // a keyword that isn't one of them (log's level)
            return null;
        default: return null;
    }
}

