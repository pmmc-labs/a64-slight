#!/usr/bin/env node

// slightc: compile .slight files to a native binary, for AArch64 (the
// default) or x86-64.
//
//   node bin/slightc.ts [-o out] [-S] [--target aarch64|x86_64] file.slight ...
//
//   -o out    the binary to write (default: a.out). The assembly goes next
//             to it, as out.S.
//   -S        stop after writing the assembly, to out (default: a.S)
//   --target  the architecture to compile for (default: aarch64)
//
// When the target is the machine's own architecture it links natively.
// Otherwise it cross-compiles: on macOS with -arch (an x86-64 binary runs
// under Rosetta 2), and elsewhere for Linux with clang (run the result
// with qemu-aarch64 or qemu-x86_64). SLIGHT_CC overrides the compiler
// command.
//
// Exit codes: 0 on success, 1 on a compile error, 2 on bad usage or a
// failed toolchain step.

import { spawnSync } from 'node:child_process';
import { accessSync, constants, readFileSync, realpathSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

import { AARCH64 } from '../compiler/src/aarch64.ts';
import { compileProgram } from '../compiler/src/codegen.ts';
import { CompileError } from '../compiler/src/errors.ts';
import { expandProgram, type Loader } from '../compiler/src/expand.ts';
import type { Target } from '../compiler/src/target.ts';
import { X86_64 } from '../compiler/src/x86_64.ts';

const RUNTIME_DIR = fileURLToPath(new URL('../runtime/', import.meta.url));
const LIB_DIR     = fileURLToPath(new URL('../lib', import.meta.url));
const CFLAGS      = ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-I', RUNTIME_DIR];

// Each target: its code generator, its part of the runtime in assembly,
// Node's name for it (process.arch), and clang's.
type Arch = { readonly target: Target; readonly asm: string; readonly node: string; readonly clang: string };
const ARCHES: Readonly<Record<string, Arch>> = {
    aarch64: { target: AARCH64, asm: 'rt_asm_aarch64.S', node: 'arm64', clang: 'aarch64' },
    x86_64:  { target: X86_64, asm: 'rt_asm_x86_64.S', node: 'x64', clang: 'x86_64' },
};

const runtimeSources = (arch: Arch): readonly string[] =>
    ['rt.c', 'process.c', 'strings.c', 'numbers.c', 'tty.c', arch.asm].map((f) => join(RUNTIME_DIR, f));

function usage(message: string): never {
    process.stderr.write(`slightc: ${message}\nusage: node bin/slightc.ts [-o out] [-S] [--target aarch64|x86_64] file.slight ...\n`);
    process.exit(2);
}

// Files, for the expander: a file is known by its real path.
const loader: Loader = {
    key: (path) => {
        try {
            return realpathSync(path);
        } catch {
            return null;
        }
    },
    text: (key) => readFileSync(key, 'utf8'),
};

type Options = { readonly out: string | null; readonly asmOnly: boolean; readonly arch: string; readonly files: readonly string[] };

function parseArgs(args: readonly string[], opts: Options): Options {
    const [arg, ...rest] = args;
    if (arg === undefined) return opts;
    if (arg === '-S') return parseArgs(rest, { ...opts, asmOnly: true });
    if (arg === '--target') {
        const [arch, ...more] = rest;
        if (arch === undefined || !Object.hasOwn(ARCHES, arch)) usage(`--target takes ${Object.keys(ARCHES).join(' or ')}`);
        return parseArgs(more, { ...opts, arch });
    }
    if (arg === '-o') {
        const [out, ...more] = rest;
        if (out === undefined) usage('-o needs a file name');
        return parseArgs(more, { ...opts, out });
    }
    if (arg.startsWith('-')) usage(`unknown option ${arg}`);
    return parseArgs(rest, { ...opts, files: [...opts.files, arg] });
}

function compilerCommand(arch: Arch): readonly string[] {
    const override = process.env['SLIGHT_CC'];
    if (override) return override.split(/\s+/).filter((s) => s !== '');
    const cc = process.platform === 'darwin' ? 'cc' : 'clang';
    if (process.arch === arch.node) return [cc];
    if (process.platform === 'darwin') return [cc, '-arch', arch.clang === 'aarch64' ? 'arm64' : arch.clang];
    // No --sysroot: clang finds Debian/Ubuntu's cross toolchain itself, and
    // with --sysroot lld can't follow the absolute paths in the sysroot's
    // libm.a (a linker script).
    return ['clang', `--target=${arch.clang}-linux-gnu`, '-fuse-ld=lld', '-static'];
}

function main(): void {
    const opts = parseArgs(process.argv.slice(2), { out: null, asmOnly: false, arch: 'aarch64', files: [] });
    const arch = ARCHES[opts.arch]!;
    if (opts.files.length === 0) usage('no input files');
    for (const file of opts.files) {
        try {
            accessSync(file, constants.R_OK);
        } catch (e) {
            usage(`couldn't read ${file}: ${(e as Error).message}`);
        }
    }

    let asm: string;
    try {
        const { prelude, program } = expandProgram(opts.files, LIB_DIR, loader);
        asm = compileProgram(program, prelude, arch.target);
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

    const [cc, ...ccArgs] = compilerCommand(arch);
    const result = spawnSync(cc!, [...ccArgs, ...CFLAGS, asmFile, ...runtimeSources(arch), '-lm', '-o', out], { stdio: 'inherit' });
    if (result.error) usage(`couldn't run ${cc}: ${result.error.message}`);
    if (result.status !== 0) process.exit(2);
}

main();
