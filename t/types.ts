// The type checks (D247), run by `make types`.
//
// Every program the golden tests compile (t/, examples/ and the guide's
// code) is checked as slightc --check checks it, and must say nothing,
// but for the warnings it lists: a line "; warns: 12:5 30:9" ("// warns:"
// in a .sly file) gives the line and column of each one it means to have,
// as a test of a fault does. A warning not listed fails, and so does one
// listed that doesn't come.
//
// Then t/types/NAME.slight (or .sly): the checker's own tests. What
// slightc --check prints for each, with --strict if a line says
// "; check: strict", is diffed against NAME.expected.

import { readdirSync, readFileSync, realpathSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

import { checkProgram, formatWarning } from '../compiler/src/check.ts';
import { CompileError } from '../compiler/src/errors.ts';
import { expandProgram, type Loader } from '../compiler/src/expand.ts';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const LIB  = join(ROOT, 'lib');
process.chdir(ROOT);

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

// What slightc --check prints for a file.
function check(file: string, strict: boolean): string[] {
    try {
        const { prelude, program } = expandProgram([file], LIB, loader, true);
        return checkProgram(prelude, program, { strict }).map(formatWarning);
    } catch (e) {
        if (e instanceof CompileError) return [e.message];
        throw e;
    }
}

const header = (text: string, file: string, name: string): string[] => {
    const lead = file.endsWith('.sly') ? '//' : ';';
    return text.split('\n').filter((l) => l.startsWith(`${lead} ${name}:`)).flatMap((l) => l.slice(lead.length + name.length + 2).trim().split(/\s+/)).filter((w) => w !== '');
};

const programs = (dir: string, withExpected: boolean): string[] =>
    readdirSync(dir).filter((f) => f.endsWith('.slight') || f.endsWith('.sly')).sort()
        .map((f) => join(dir, f))
        .filter((f) => !withExpected || existsSync(f.replace(/\.(slight|sly)$/, '.expected')));

const failures: string[] = [];
let count = 0;

for (const file of [...programs('t', true), ...programs('examples', true), ...programs('docs/guide/code', true)]) {
    count++;
    const text = readFileSync(file, 'utf8');
    const want = header(text, file, 'warns').sort();
    const lines = check(file, false);
    // where each warning is, as line:col, if it's in the file itself;
    // one elsewhere (in a library it includes) is never meant
    const at = lines.map((l) => (l.startsWith(`${file}:`) ? l.split(':').slice(1, 3).join(':') : null));
    const unlisted = lines.filter((_, i) => at[i] === null || !want.includes(at[i]!));
    const missing = want.filter((w) => !at.includes(w));
    if (missing.length > 0 || unlisted.length > 0) {
        failures.push([
            `FAIL ${file}`,
            ...unlisted.map((l) => `  not listed: ${l}`),
            ...missing.map((m) => `  listed, but no warning at ${m}`),
        ].join('\n'));
    }
}

for (const file of programs('t/types', false)) {
    count++;
    const text = readFileSync(file, 'utf8');
    const strict = header(text, file, 'check').includes('strict');
    const got = check(file, strict).map((l) => `${l}\n`).join('');
    const expected = file.replace(/\.(slight|sly)$/, '.expected');
    const want = existsSync(expected) ? readFileSync(expected, 'utf8') : '(no .expected)\n';
    if (got !== want) failures.push(`FAIL ${file}\n--- expected\n${want}--- got\n${got}`);
}

for (const f of failures) console.log(f);
console.log(`types: ${count} programs, ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);
