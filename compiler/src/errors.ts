// Compile errors. The compiler stops at the first one, the way a slight
// process ends at a `raise`: this class is the one exception to "no classes",
// so that `instanceof` can tell compile errors from compiler bugs.

import type { Pos } from './sexp.ts';

export class CompileError extends Error {
    readonly pos: Pos | null;
    constructor(message: string, pos: Pos | null) {
        super(pos === null ? message : `${pos.file}:${pos.line}:${pos.col}: ${message}`);
        this.pos = pos;
    }
}
