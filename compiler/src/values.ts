// How values are represented at runtime: the compiler's side of
// runtime/rt.h. Generated code uses rt.h's names where it can (RT_TRUE,
// RT_NIL, ...); these are for what the compiler has to compute itself.
// tests/values.test.ts checks that the two agree.

// The most arguments a call can pass: in registers, on every target; and
// what a process waiting in recv keeps of them (rt_proc_t.args).
export const MAX_ARGS = 8;

// The top level's parameters, in the order the runtime's main gives the
// root them: the program's arguments and its environment (D144, D186).
// Only the top level can name them.
export const TOP_PARAMS: readonly string[] = ['@ARGV', '%ENV'];

export const intWord    = (n: bigint): bigint => n << 1n;
export const symbolWord = (id: number): bigint => (BigInt(id) << 3n) | 5n;

// A box's tag; a tuple's header word, for n elements, which a tuple
// pattern compares with (D178); and where element i is, from a tagged
// pointer to the tuple.
export const TAG_BOXED     = 3;
export const tupleHeader   = (n: number): bigint => (BigInt(n) << 8n) | 4n;
export const tupleElement  = (i: number): number => 8 * (1 + i) - TAG_BOXED;

// A float's box: its header (8 bytes of double, and the float type), which
// a float is told by, all of it compared at once; and where the double is,
// from a tagged pointer to the box (D198).
export const FLOAT_HEADER = (8n << 8n) | 2n;
export const FLOAT_VALUE  = 8 - TAG_BOXED;

// The symbols every program has, in id order (RT_FALSE is symbol 0, RT_TRUE symbol 1).
export const RESERVED_SYMBOLS: readonly string[] = ['#false', '#true'];

// The fault kinds, in RT_FAULT_ order (from 1).
export const FAULT_KINDS: readonly string[] = [
    'not-an-int', 'overflow', 'not-a-bool', 'no-clause', 'stack', 'not-a-cons', 'not-a-list', 'heap',
    'not-a-string', 'not-a-symbol', 'out-of-range', 'not-a-number', 'div-by-zero', 'not-a-function',
    'arity', 'not-a-pid', 'join-self', 'not-a-device', 'not-json', 'not-sexp', 'no-match', 'not-a-tuple',
    'not-an-actor', 'not-a-task', 'not-an-array',
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
    'enotempty', 'exdev', 'ebusy',
];

// What JSON's values need beyond #true and #false, in RT_JSON_ order.
export const JSON_WORDS: readonly string[] = ['null', 'object'];

// What the s-expression reader makes, in RT_SEXP_ order.
export const SEXP_WORDS: readonly string[] = ['symbol', 'quote'];

// What :system is asked and answers with, in RT_SYS_ order (D171).
export const SYSTEM_WORDS: readonly string[] = [
    'processes', 'process', 'runtime', 'end', 'ended', 'pid', 'parent', 'state', 'in', 'mail', 'reductions',
    'devices', 'joining', 'running', 'ready', 'recv', 'sleeping', 'pids', 'timers', 'stacks', 'clock', 'heap-max',
    'reading',
];

// How a command ends, and the signals' names, in RT_EXEC_ order (D185).
export const EXEC_WORDS: readonly string[] = [
    'exited', 'signal', 'hup', 'int', 'quit', 'ill', 'trap', 'abrt', 'bus', 'fpe', 'kill', 'usr1', 'segv', 'usr2',
    'pipe', 'alrm', 'term',
];

// Who's at the other end of a Unix connection, in RT_PEER_ order (D235).
export const PEER_WORDS: readonly string[] = ['uid', 'gid'];

// What kind a process is, in :system's info (D243), in RT_KIND_ order.
export const KIND_WORDS: readonly string[] = ['kind', 'task', 'actor'];

// What open opens for a task (D243), in RT_SOURCE_ order.
export const SOURCE_WORDS: readonly string[] = ['fs/read', 'fs/write', 'fs/append', 'tcp', 'unix', 'exec', 'system'];

// What :system says of the process table (D244), in RT_TABLE_ order.
export const TABLE_WORDS: readonly string[] = ['records'];

// What fs/stat says of a file (D254), in RT_STAT_ order.
export const STAT_WORDS: readonly string[] = ['file', 'dir', 'link', 'other', 'size', 'modified', 'mode'];

// bytes/pack's and bytes/unpack's specs, and :short (D254), in RT_PACK_ order.
export const PACK_WORDS: readonly string[] = [
    'u8', 'i8', 'u16be', 'u16le', 'i16be', 'i16le', 'u32be', 'u32le', 'i32be', 'i32le', 'u64be', 'u64le', 'i64be',
    'i64le', 'f32be', 'f32le', 'f64be', 'f64le', 'bytes', 'short',
];

// log's levels below :error (D254), in RT_LOG_ order.
export const LOG_WORDS: readonly string[] = ['debug', 'info', 'warn'];

// An array's kinds (D253), in RT_ARRAY_ order; :int is EXEC_WORDS'.
export const ARRAY_WORDS: readonly string[] = ['float'];

// What :system says of islands (D256), in RT_ISLE_ order.
export const ISLAND_WORDS: readonly string[] = ['island', 'started', 'sent', 'received', 'shared', 'islands'];

// The symbols the runtime makes (ending a process, faults, keys,
// devices, data), which follow the reserved ones: RT_SYM_OK and on in rt.h.
export const RUNTIME_SYMBOLS: readonly string[] = [
    'ok', 'error', 'exit', 'killed', ...FAULT_KINDS, ...KEY_NAMES, ...DEVICE_WORDS, ...ERRNO_NAMES, ...JSON_WORDS, ...SEXP_WORDS,
    ...SYSTEM_WORDS, ...EXEC_WORDS, ...PEER_WORDS, ...KIND_WORDS, ...SOURCE_WORDS, ...TABLE_WORDS, ...STAT_WORDS, ...PACK_WORDS, ...LOG_WORDS,
    ...ARRAY_WORDS, ...ISLAND_WORDS,
];
