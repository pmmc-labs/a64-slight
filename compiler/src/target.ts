// What a target supplies to the code generator (codegen.ts): the
// instructions for each shape of code it emits. The generator decides what
// to emit and where values live: the accumulator, which every expression
// leaves its value in, the left operand of a binary operation, and the
// slots of the function's frame. A target says how, in its own registers
// and syntax: aarch64.ts and x86_64.ts.
//
// Shapes, not instructions: a target is free to do a shape in any number of
// instructions, with whatever scratch registers it likes, as long as it
// keeps the accumulator wherever a shape doesn't say it changes, and leaves
// the offending value in the accumulator when it branches to a fault.

// Lines of assembly, as a tree, so that joining pieces is cheap.
export type Code = string | readonly Code[];

// The conditions a comparison can ask for, of left against the accumulator.
export type Cond = 'eq' | 'ne' | 'lt' | 'le' | 'gt' | 'ge';

// What a call passes, in order: the accumulator, the left operand, the
// current process, a frame slot, an assembler constant (a number or an
// rt.h name), the address of a label, the frame's slots (a receive
// function's arguments, for rt_recv), or the address of slot si.
export type Operand =
    | { readonly t: 'acc' }
    | { readonly t: 'left' }
    | { readonly t: 'proc' }
    | { readonly t: 'slot'; readonly si: number }
    | { readonly t: 'imm'; readonly v: string }
    | { readonly t: 'addr'; readonly label: string }
    | { readonly t: 'frame' }
    | { readonly t: 'slotAddr'; readonly si: number };

export const ACC: Operand   = { t: 'acc' };
export const LEFT: Operand  = { t: 'left' };
export const PROC: Operand  = { t: 'proc' };
export const FRAME: Operand = { t: 'frame' };
export const inSlot   = (si: number): Operand => ({ t: 'slot', si });
export const imm      = (v: string | number): Operand => ({ t: 'imm', v: `${v}` });
export const addr     = (label: string): Operand => ({ t: 'addr', label });
export const slotAddr = (si: number): Operand => ({ t: 'slotAddr', si });

export type Target = {
    readonly name: string;

    // The top of the .S file, and of each function.
    readonly fileStart: Code;
    readonly functionStart: (entry: string, name: string) => Code;

    // A function's entry: build a frame of `size` bytes (a multiple of 16),
    // branch to `overflow` if the stack is nearly full, store the
    // parameters and then a closure's captured values (`free`) in the first
    // slots, count a reduction and branch to `preempt` when they're used
    // up, and end with the label `${preempt}_done`, where the preemption
    // comes back to.
    readonly prologue: (size: number, overflow: string, params: readonly string[],
                        free: readonly string[], preempt: string) => Code;
    // Take down the frame and return the accumulator.
    readonly ret: Code;

    // Call fn (a compiled function or the runtime's) with these arguments;
    // in tail position the frame comes down first and the call is a jump.
    // The result is in the accumulator.
    readonly call: (fn: string, args: readonly Operand[], tail: boolean) => Code;
    // Call the value in slot fnSlot with the arguments in argSlots: fault
    // to notFn unless it's a closure, and to arity unless it takes that
    // many.
    readonly closureCall: (fnSlot: number, argSlots: readonly number[], notFn: string, arity: string, tail: boolean) => Code;
    readonly jump: (label: string) => Code;

    // Into the accumulator: a word (with a comment), an rt.h constant
    // (RT_NIL, RT_TRUE, RT_FALSE), a label's address with a tag, a field of
    // the current process, or a slot.
    readonly loadWord: (word: bigint, note: string) => Code;
    readonly loadConst: (name: string) => Code;
    readonly loadTagged: (label: string, tag: string) => Code;
    readonly loadProc: (field: string) => Code;
    readonly loadSlot: (si: number, note?: string) => Code;
    readonly storeSlot: (si: number, note?: string) => Code;
    // Slot to slot, leaving the accumulator alone.
    readonly copySlot: (from: number, to: number, note: string) => Code;
    // The left operand, from a slot.
    readonly loadLeft: (si: number) => Code;

    // Tests that branch, leaving the accumulator as it was: to ifFalse on
    // #false and to notBool on anything but #true; unless an integer;
    // unless the accumulator and the left operand are both integers; unless
    // a list (a cons or ()); unless a cons; unless (); unless the left
    // operand is this word; if the left operand and the accumulator are the
    // same word.
    readonly testBool: (ifFalse: string, notBool: string) => Code;
    readonly branchUnlessInt: (label: string) => Code;
    readonly branchUnlessInts: (label: string) => Code;
    readonly branchUnlessList: (label: string) => Code;
    readonly branchUnlessCons: (label: string) => Code;
    readonly branchUnlessNil: (label: string) => Code;
    readonly branchUnlessLeftIs: (word: bigint, label: string) => Code;
    readonly branchIfSame: (label: string) => Code;

    // Booleans into the accumulator: a type predicate of it (int? nil?
    // cons? pid? sym? bool?), a comparison of the left operand with it, and
    // its negation (it's #true or #false).
    readonly predicate: (name: string) => Code;
    readonly compareBool: (cond: Cond) => Code;
    readonly notBool: Code;

    // Two integers, left and the accumulator, into the accumulator: + - *
    // branching to overflow when the result doesn't fit; div and %,
    // branching to zero when the accumulator is 0 (and div to overflow).
    readonly arith: (op: '+' | '-' | '*', overflow: string) => Code;
    readonly divide: (op: 'div' | '%', zero: string, overflow: string) => Code;

    // The accumulator's car or cdr (it's a cons), for car, cdr and c[ad]r;
    // and the steps of a recv pattern, walking a message in the
    // accumulator: its car into the left operand, its cdr into the
    // accumulator, its car into a slot.
    readonly cxr: (step: 'a' | 'd') => Code;
    readonly leftFromCar: Code;
    readonly accFromCdr: Code;
    readonly storeCar: (si: number, note: string) => Code;

    // Allocation: `bytes` from the process's heap, at the label `again`,
    // into a register the next shapes know, keeping the accumulator and the
    // left operand; returns the inline code and an out-of-line stub that
    // asks the runtime for a new chunk (site says where, for the heap
    // fault). Then fill what was allocated: a cons cell (head in a slot,
    // tail in the accumulator), the cells of a list (its elements in n
    // slots from firstSlot), or a closure's box; each leaves the tagged
    // value in the accumulator.
    readonly allocate: (bytes: number, again: string, site: string) => readonly [Code, Code];
    readonly consCell: (headSlot: number) => Code;
    readonly listCells: (firstSlot: number, n: number) => Code;
    readonly makeClosure: (entry: string, arity: number, name: string,
                           free: readonly (readonly [number, string])[]) => Code;
};

// The moves that put a call's arguments in the argument registers, in an
// order that never overwrites a register before it's read: moves from one
// register to another first, saving a register that's in a cycle to the
// first argument register no pending move uses; then the rest in order.
// `register` says which register an operand is in, if it's in one.
export function placeArgs(args: readonly Operand[], regs: readonly string[],
                          register: (op: Operand) => string | null,
                          move: (dst: string, op: Operand) => Code,
                          regMove: (dst: string, src: string) => Code): Code {
    type Move = { readonly dst: string; readonly src: string };
    let pending: readonly Move[] = args.flatMap((op, i) => {
        const src = register(op);
        return src !== null && src !== regs[i] ? [{ dst: regs[i]!, src }] : [];
    });
    const out: Code[] = [];
    while (pending.length > 0) {
        const ready = pending.find((m) => !pending.some((o) => o !== m && o.src === m.dst));
        if (ready !== undefined) {
            out.push(regMove(ready.dst, ready.src));
            pending = pending.filter((m) => m !== ready);
            continue;
        }
        const first = pending[0]!;
        const temp  = regs.find((r) => !pending.some((m) => m.src === r || m.dst === r))!;
        out.push(regMove(temp, first.dst));
        pending = pending.map((m) => (m.src === first.dst ? { dst: m.dst, src: temp } : m));
    }
    args.forEach((op, i) => {
        if (register(op) === null) out.push(move(regs[i]!, op));
    });
    return out;
}
