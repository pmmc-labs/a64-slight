// How values are represented at runtime: the compiler's side of
// runtime/rt.h. Generated code uses rt.h's names where it can (RT_TRUE,
// RT_NIL, ...); these are for what the compiler has to compute itself.
// tests/values.test.ts checks that the two agree.

export const intWord    = (n: bigint): bigint => n << 1n;
export const symbolWord = (id: number): bigint => (BigInt(id) << 3n) | 5n;

// The symbols every program has, in id order (RT_FALSE is symbol 0, RT_TRUE symbol 1).
export const RESERVED_SYMBOLS: readonly string[] = ['#false', '#true'];

// The fault kinds, in RT_FAULT_ order (from 1).
export const FAULT_KINDS: readonly string[] = [
    'not-an-int', 'overflow', 'not-a-bool', 'no-clause', 'stack', 'not-a-cons', 'not-a-list', 'heap',
    'not-a-string', 'not-a-symbol', 'out-of-range', 'not-a-number', 'div-by-zero', 'not-a-function',
    'arity', 'not-a-pid', 'join-self',
];

// The names of keys from :keypress, then the modifiers, in RT_KEY_ order.
export const KEY_NAMES: readonly string[] = [
    'ArrowUp', 'ArrowDown', 'ArrowRight', 'ArrowLeft', 'Home', 'End', 'Insert', 'Delete', 'PageUp', 'PageDown',
    'Enter', 'Escape', 'Backspace', 'Tab', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12',
    'Unidentified', 'ctrl', 'alt', 'shift',
];

// The symbols the runtime makes (ending a process, faults, keys), which
// follow the reserved ones: RT_SYM_OK and on in rt.h.
export const RUNTIME_SYMBOLS: readonly string[] = ['ok', 'error', 'exit', 'killed', ...FAULT_KINDS, ...KEY_NAMES];
