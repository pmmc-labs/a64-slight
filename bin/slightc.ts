#!/usr/bin/env node

// slightc: compile .slight files to a native AArch64 binary.
//
//   node bin/slightc.ts [-o out] [-S] file.slight ...
//
//   -o out   the binary to write (default: a.out). The assembly goes next
//            to it, as out.S.
//   -S       stop after writing the assembly, to out (default: a.S)
//
// On Apple Silicon and AArch64 Linux it links natively. Anywhere else it
// cross-compiles for AArch64 Linux with clang (run the result with
// qemu-aarch64). SLIGHT_CC overrides the compiler command.
//
// Exit codes: 0 on success, 1 on a compile error, 2 on bad usage or a
// failed toolchain step.

import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

import { compileProgram } from '../compiler/src/codegen.ts';
import { CompileError } from '../compiler/src/errors.ts';
import { read } from '../compiler/src/reader.ts';
import { NIL, append, type Sexp } from '../compiler/src/sexp.ts';

const RUNTIME_DIR = fileURLToPath(new URL('../runtime/', import.meta.url));
const PRELUDE     = fileURLToPath(new URL('../lib/prelude.slight', import.meta.url));
const RUNTIME_SRC = ['rt.c', 'process.c', 'strings.c', 'numbers.c', 'tty.c', 'rt_asm.S'].map((f) => join(RUNTIME_DIR, f));
const CFLAGS      = ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-I', RUNTIME_DIR];

function usage(message: string): never {
    process.stderr.write(`slightc: ${message}\nusage: node bin/slightc.ts [-o out] [-S] file.slight ...\n`);
    process.exit(2);
}

function readSource(file: string): string {
    try {
        return readFileSync(file, 'utf8');
    } catch (e) {
        usage(`couldn't read ${file}: ${(e as Error).message}`);
    }
}

type Options = { readonly out: string | null; readonly asmOnly: boolean; readonly files: readonly string[] };

function parseArgs(args: readonly string[], opts: Options): Options {
    const [arg, ...rest] = args;
    if (arg === undefined) return opts;
    if (arg === '-S') return parseArgs(rest, { ...opts, asmOnly: true });
    if (arg === '-o') {
        const [out, ...more] = rest;
        if (out === undefined) usage('-o needs a file name');
        return parseArgs(more, { ...opts, out });
    }
    if (arg.startsWith('-')) usage(`unknown option ${arg}`);
    return parseArgs(rest, { ...opts, files: [...opts.files, arg] });
}

function compilerCommand(): readonly string[] {
    const override = process.env['SLIGHT_CC'];
    if (override) return override.split(/\s+/).filter((s) => s !== '');
    if (process.arch === 'arm64') return [process.platform === 'darwin' ? 'cc' : 'clang'];
    // No --sysroot: clang finds Debian/Ubuntu's cross toolchain itself, and
    // with --sysroot lld can't follow the absolute paths in the sysroot's
    // libm.a (a linker script).
    return ['clang', '--target=aarch64-linux-gnu', '-fuse-ld=lld', '-static'];
}

function main(): void {
    const opts = parseArgs(process.argv.slice(2), { out: null, asmOnly: false, files: [] });
    if (opts.files.length === 0) usage('no input files');

    let asm: string;
    try {
        const forms = opts.files.reduce<Sexp>((acc, file) => append(acc, read(readSource(file), file)), NIL);
        asm = compileProgram(forms, read(readSource(PRELUDE), 'lib/prelude.slight'));
    } catch (e) {
        if (!(e instanceof CompileError)) throw e;
        process.stderr.write(`${e.message}\n`);
        process.exit(1);
    }

    if (opts.asmOnly) {
        writeFileSync(opts.out ?? 'a.S', asm);
        return;
    }
    const out     = opts.out ?? 'a.out';
    const asmFile = `${out}.S`;
    writeFileSync(asmFile, asm);

    const [cc, ...ccArgs] = compilerCommand();
    const result = spawnSync(cc!, [...ccArgs, ...CFLAGS, asmFile, ...RUNTIME_SRC, '-lm', '-o', out], { stdio: 'inherit' });
    if (result.error) usage(`couldn't run ${cc}: ${result.error.message}`);
    if (result.status !== 0) process.exit(2);
}

main();
