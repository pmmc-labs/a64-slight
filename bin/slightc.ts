#!/usr/bin/env node

// slightc: compile .slight files to a native binary, for AArch64 (the
// default) or x86-64.
//
//   node bin/slightc.ts [-o out] [-S] [--target aarch64|x86_64] file.slight ...
//   node bin/slightc.ts --runtime [--target aarch64|x86_64]
//
//   -o out     the binary to write (default: a.out). The assembly goes next
//              to it, as out.S.
//   -S         stop after writing the assembly, to out (default: a.S)
//   --target   the architecture to compile for (default: aarch64)
//   --runtime  only build the runtime, if it isn't built already: for
//              before several compiles start at once (t/run.sh)
//
// The runtime is compiled once, and kept under build/runtime/ (D150).
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
import { createHash } from 'node:crypto';
import { accessSync, constants, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, realpathSync,
         renameSync, rmSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { basename, join } from 'node:path';

import { AARCH64 } from '../compiler/src/aarch64.ts';
import { compileProgram } from '../compiler/src/codegen.ts';
import { CompileError } from '../compiler/src/errors.ts';
import { expandProgram, type Loader } from '../compiler/src/expand.ts';
import type { Target } from '../compiler/src/target.ts';
import { X86_64 } from '../compiler/src/x86_64.ts';

const RUNTIME_DIR = fileURLToPath(new URL('../runtime/', import.meta.url));
const LIB_DIR     = fileURLToPath(new URL('../lib', import.meta.url));
const CACHE_DIR   = fileURLToPath(new URL('../build/runtime/', import.meta.url));
const CFLAGS      = ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-I', RUNTIME_DIR];

// Each target: its code generator, its part of the runtime in assembly,
// Node's name for it (process.arch), and clang's.
type Arch = { readonly target: Target; readonly asm: string; readonly node: string; readonly clang: string };
const ARCHES: Readonly<Record<string, Arch>> = {
    aarch64: { target: AARCH64, asm: 'rt_asm_aarch64.S', node: 'arm64', clang: 'aarch64' },
    x86_64:  { target: X86_64, asm: 'rt_asm_x86_64.S', node: 'x64', clang: 'x86_64' },
};

const runtimeSources = (arch: Arch): readonly string[] =>
    ['rt.c', 'process.c', 'strings.c', 'numbers.c', 'tty.c', 'json.c', arch.asm].map((f) => join(RUNTIME_DIR, f));

function usage(message: string): never {
    process.stderr.write(`slightc: ${message}\nusage: node bin/slightc.ts [-o out] [-S] [--target aarch64|x86_64] file.slight ...\n`
                         + '       node bin/slightc.ts --runtime [--target aarch64|x86_64]\n');
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

type Options = {
    readonly out: string | null; readonly asmOnly: boolean; readonly runtimeOnly: boolean;
    readonly arch: string; readonly files: readonly string[];
};

function parseArgs(args: readonly string[], opts: Options): Options {
    const [arg, ...rest] = args;
    if (arg === undefined) return opts;
    if (arg === '-S') return parseArgs(rest, { ...opts, asmOnly: true });
    if (arg === '--runtime') return parseArgs(rest, { ...opts, runtimeOnly: true });
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

// The compiler command, and what only linking takes. SLIGHT_CC is the
// whole command, for both.
type Compiler = { readonly cc: readonly string[]; readonly link: readonly string[] };

function compilerCommand(arch: Arch): Compiler {
    const override = process.env['SLIGHT_CC'];
    if (override) return { cc: override.split(/\s+/).filter((s) => s !== ''), link: [] };
    const cc = process.platform === 'darwin' ? 'cc' : 'clang';
    if (process.arch === arch.node) return { cc: [cc], link: [] };
    if (process.platform === 'darwin') return { cc: [cc, '-arch', arch.clang === 'aarch64' ? 'arm64' : arch.clang], link: [] };
    // No --sysroot: clang finds Debian/Ubuntu's cross toolchain itself, and
    // with --sysroot lld can't follow the absolute paths in the sysroot's
    // libm.a (a linker script).
    return { cc: ['clang', `--target=${arch.clang}-linux-gnu`], link: ['-fuse-ld=lld', '-static'] };
}

// Runs a toolchain step: whether it worked.
function run(command: readonly string[], cwd?: string): boolean {
    const [cmd, ...args] = command;
    const result = spawnSync(cmd!, args, { stdio: 'inherit', cwd });
    if (result.error) process.stderr.write(`slightc: couldn't run ${cmd}: ${result.error.message}\n`);
    return result.status === 0;
}

// The runtime's object files, compiled once and kept in build/runtime/, in
// a directory named for a hash of the compiler command, the flags and
// every file in runtime/: an edit to any of them, or another compiler,
// gets a build of its own, so nothing goes stale (D150). Compiles that
// start at once (the golden tests run in parallel) may each build it: each
// builds in a directory of its own and renames it into place, and the
// first rename wins.
function runtimeObjects(arch: Arch, compiler: Compiler): readonly string[] {
    const hash = createHash('sha256').update(JSON.stringify([compiler.cc, CFLAGS]));
    for (const f of readdirSync(RUNTIME_DIR).sort()) hash.update(f).update(readFileSync(join(RUNTIME_DIR, f)));
    const dir     = join(CACHE_DIR, `${arch.clang}-${hash.digest('hex').slice(0, 16)}`);
    const sources = runtimeSources(arch);
    const objects = sources.map((src) => join(dir, basename(src).replace(/\.[cS]$/, '.o')));
    if (existsSync(dir)) return objects;

    mkdirSync(CACHE_DIR, { recursive: true });
    const tmp = mkdtempSync(join(CACHE_DIR, 'tmp-'));
    const ok  = run([...compiler.cc, ...CFLAGS, '-c', ...sources], tmp);
    try {
        if (ok) renameSync(tmp, dir);
    } catch (e) {
        if (!existsSync(dir)) throw e;
    } finally {
        rmSync(tmp, { recursive: true, force: true });
    }
    if (!ok) process.exit(2);
    return objects;
}

function main(): void {
    const opts = parseArgs(process.argv.slice(2), { out: null, asmOnly: false, runtimeOnly: false, arch: 'aarch64', files: [] });
    const arch = ARCHES[opts.arch]!;
    if (opts.runtimeOnly) {
        if (opts.files.length > 0 || opts.asmOnly || opts.out !== null) usage('--runtime takes only --target');
        runtimeObjects(arch, compilerCommand(arch));
        return;
    }
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

    const compiler = compilerCommand(arch);
    if (!run([...compiler.cc, ...compiler.link, ...CFLAGS, asmFile, ...runtimeObjects(arch, compiler), '-lm', '-o', out])) {
        process.exit(2);
    }
}

main();
