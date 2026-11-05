// Tests for the driver (bin/slightc.ts): its options, the name of the
// runtime's build, and how it ends when something goes wrong.

import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { ARCHES, DEFAULTS, parseArgs, runtimeKey } from '../../bin/slightc.ts';

const SLIGHTC = fileURLToPath(new URL('../../bin/slightc.ts', import.meta.url));

// slightc run in dir: its exit code and what it wrote to stderr.
function slightc(dir: string, ...args: readonly string[]): [number | null, string] {
    const r = spawnSync(process.execPath, [SLIGHTC, ...args], { cwd: dir, encoding: 'utf8' });
    return [r.status, r.stderr];
}

function inTemp(f: (dir: string) => void): void {
    const dir = mkdtempSync(join(tmpdir(), 'slightc-'));
    try {
        f(dir);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
}

test('the options', () => {
    assert.deepEqual(parseArgs(['-o', 'out', '--target', 'x86_64', 'a.slight', 'b.slight']),
        { ...DEFAULTS, out: 'out', arch: 'x86_64', files: ['a.slight', 'b.slight'] });
    assert.deepEqual(parseArgs(['--check', '--strict', 'a.slight']), { ...DEFAULTS, check: true, strict: true, files: ['a.slight'] });
    assert.equal(parseArgs(['--target', 'mips']), '--target takes aarch64 or x86_64');
    assert.equal(parseArgs(['-o']), '-o needs a file name');
});

test("-o takes a file, not an option, and -- ends the options", () => {
    // -o -x would write -x.S, which clang takes for an option
    assert.equal(parseArgs(['-o', '-x', 'a.slight']), '-o needs a file name, not -x (write ./-x for a file named so)');
    assert.deepEqual(parseArgs(['-o', './-x', 'a.slight']), { ...DEFAULTS, out: './-x', files: ['a.slight'] });
    assert.match(parseArgs(['-x.slight']) as string, /^unknown option -x.slight \(-- ends the options/);
    assert.deepEqual(parseArgs(['-S', '--', '-x.slight', '--check']), { ...DEFAULTS, asmOnly: true, files: ['-x.slight', '--check'] });
});

test("the runtime's build is named for the compiler's version too, so an upgrade in place doesn't reuse it", () => {
    const arch = ARCHES['x86_64']!;
    const old  = runtimeKey(arch, ['clang'], 'clang version 18.1.3\n');
    assert.equal(runtimeKey(arch, ['clang'], 'clang version 18.1.3\n'), old);
    assert.notEqual(runtimeKey(arch, ['clang'], 'clang version 19.1.0\n'), old);
    assert.notEqual(runtimeKey(arch, ['clang-18'], 'clang version 18.1.3\n'), old);
    assert.match(old, /^x86_64-[0-9a-f]{16}$/);
});

test('how slightc ends: usage and files 2, a compile error 1, its own fault 2', () => {
    inTemp((dir) => {
        writeFileSync(join(dir, '-dash.slight'), '(pprint 1)\n');
        writeFileSync(join(dir, 'bad.slight'), '(defun f)\n');
        // deep enough to run Node out of stack, a fault in the compiler
        writeFileSync(join(dir, 'deep.slight'), `${'(list '.repeat(100000)}${')'.repeat(100000)}\n`);
        assert.deepEqual(slightc(dir, '--check', '--', '-dash.slight'), [0, '']);
        const [dash, said] = slightc(dir, '--check', '-dash.slight');
        assert.equal(dash, 2);
        assert.match(said, /^slightc: unknown option -dash.slight/);
        const [o, saidO] = slightc(dir, '-o', '-x', '-S', 'bad.slight');
        assert.equal(o, 2);
        assert.match(saidO, /^slightc: -o needs a file name, not -x/);
        assert.deepEqual(slightc(dir, '--check', 'bad.slight'), [1, 'bad.slight:1:1: defun is (defun name (params...) body...)\n']);
        const [nodir, saidNodir] = slightc(dir, '-S', '-o', join('nowhere', 'a.S'), '--', '-dash.slight');
        assert.equal(nodir, 2);
        assert.match(saidNodir, /^slightc: couldn't write nowhere\/a.S: ENOENT/);
        const [isDir, saidDir] = slightc(dir, '--check', '.');
        assert.equal(isDir, 2);
        assert.match(saidDir, /^slightc: couldn't read \.: it isn't a file/);
        const [deep, saidDeep] = slightc(dir, '--check', 'deep.slight');
        assert.equal(deep, 2);
        assert.match(saidDeep, /^slightc: internal error: RangeError: Maximum call stack size exceeded\n/);
    });
});
