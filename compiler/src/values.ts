// How values are represented at runtime: the compiler's side of
// runtime/rt.h. Generated code uses rt.h's names where it can (RT_TRUE,
// RT_NIL, ...); these are for what the compiler has to compute itself.
// tests/values.test.ts checks that the two agree.

// The most arguments a call can pass: in registers, on every target; and
// what a process waiting in recv keeps of them (rt_proc_t.args).
export const MAX_ARGS = 8;

export const intWord    = (n: bigint): bigint => n << 1n;
export const symbolWord = (id: number): bigint => (BigInt(id) << 3n) | 5n;

// A box's tag; a tuple's header word, for n elements, which a tuple
// pattern compares with (D178); and where element i is, from a tagged
// pointer to the tuple.
export const TAG_BOXED     = 3;
export const tupleHeader   = (n: number): bigint => (BigInt(n) << 8n) | 4n;
export const tupleElement  = (i: number): number => 8 * (1 + i) - TAG_BOXED;

// The symbols every program has, in id order (RT_FALSE is symbol 0, RT_TRUE symbol 1).
export const RESERVED_SYMBOLS: readonly string[] = ['#false', '#true'];

// The fault kinds, in RT_FAULT_ order (from 1).
export const FAULT_KINDS: readonly string[] = [
    'not-an-int', 'overflow', 'not-a-bool', 'no-clause', 'stack', 'not-a-cons', 'not-a-list', 'heap',
    'not-a-string', 'not-a-symbol', 'out-of-range', 'not-a-number', 'div-by-zero', 'not-a-function',
    'arity', 'not-a-pid', 'join-self', 'not-a-device', 'not-json', 'not-sexp', 'no-match', 'not-a-tuple',
];

// The names of keys from :keypress, then the modifiers, in RT_KEY_ order.
export const KEY_NAMES: readonly string[] = [
    'ArrowUp', 'ArrowDown', 'ArrowRight', 'ArrowLeft', 'Home', 'End', 'Insert', 'Delete', 'PageUp', 'PageDown',
    'Enter', 'Escape', 'Backspace', 'Tab', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12',
    'Unidentified', 'ctrl', 'alt', 'shift',
];

// What a device and its owner say to each other, in RT_DEV_ order.
export const DEVICE_WORDS: readonly string[] = [
    'open', 'line', 'eof', 'write', 'accept', 'read', 'lines', 'chunks', 'chunk', 'json', 'json/items', 'sexp',
    'source', 'form', 'doc',
];

// Why a device failed, from errno, in RT_ERR_ order.
export const ERRNO_NAMES: readonly string[] = [
    'enoent', 'eacces', 'eperm', 'eexist', 'eisdir', 'enotdir', 'enametoolong', 'eloop', 'erofs', 'enospc',
    'efbig', 'emfile', 'enfile', 'eio', 'econnrefused', 'econnreset', 'epipe', 'etimedout', 'eaddrinuse',
    'eaddrnotavail', 'ehostunreach', 'enetunreach', 'enotfound', 'too-big', 'bad-json', 'bad-sexp', 'io-error',
];

// What JSON's values need beyond #true and #false, in RT_JSON_ order.
export const JSON_WORDS: readonly string[] = ['null', 'object'];

// What the s-expression reader makes, in RT_SEXP_ order.
export const SEXP_WORDS: readonly string[] = ['symbol', 'quote'];

// What :system is asked and answers with, in RT_SYS_ order (D171).
export const SYSTEM_WORDS: readonly string[] = [
    'processes', 'process', 'runtime', 'end', 'ended', 'pid', 'parent', 'state', 'in', 'mail', 'reductions',
    'devices', 'joining', 'running', 'ready', 'recv', 'sleeping', 'pids', 'timers', 'stacks', 'clock',
];

// How a command ends, and the signals' names, in RT_EXEC_ order (D185).
export const EXEC_WORDS: readonly string[] = [
    'exited', 'signal', 'hup', 'int', 'quit', 'ill', 'trap', 'abrt', 'bus', 'fpe', 'kill', 'usr1', 'segv', 'usr2',
    'pipe', 'alrm', 'term',
];

// The symbols the runtime makes (ending a process, faults, keys,
// devices, data), which follow the reserved ones: RT_SYM_OK and on in rt.h.
export const RUNTIME_SYMBOLS: readonly string[] = [
    'ok', 'error', 'exit', 'killed', ...FAULT_KINDS, ...KEY_NAMES, ...DEVICE_WORDS, ...ERRNO_NAMES, ...JSON_WORDS, ...SEXP_WORDS,
    ...SYSTEM_WORDS, ...EXEC_WORDS,
];
