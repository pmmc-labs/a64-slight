#!/usr/bin/env python3
"""Runs the benchmarks (D194): slight's programs and the same ones in other
languages, on this machine.

    bench/run.py                    every benchmark, in every language found
    bench/run.py --quick            small sizes, to see that everything works
    bench/run.py --only fib,n-body --langs slight,c --runs 10

Each program is built once (slight's for this machine's own target), run
once to check its output, then timed --runs times, the programs taking
turns, so that the machine's drift falls on all of them alike. One whose
check took long gets fewer runs, but at least 3 (or --runs, if that's
fewer), to keep each to about --budget seconds.

Go, Erlang and Rust run twice: on one core, as slight does
(GOMAXPROCS=1, +S 1:1, tokio's current-thread runtime), and with their
defaults, every core (go-mc, erlang-mc, rust-mc). One core is the
comparison; the summary gives every core a table of its own, what it
gains or costs each language (D237). Chez Scheme is found as
chez, chezscheme or scheme, and its programs are compiled ahead of time,
as slight's are. Rust, with tokio, is in the actor programs alone, their
floor as C is the sequential ones' (D224), and in startup.

The results go to bench/results/, as JSON and as a Markdown summary, named
for the date and the machine.
"""

import argparse
import collections
import datetime
import json
import os
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import threading
import time

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'bench')

# --- what's run --------------------------------------------------------------

ALL    = ('slight', 'c', 'go', 'erlang', 'node', 'perl', 'python', 'chez')
ACTORS = ('slight', 'go', 'erlang', 'rust')

Benchmark = collections.namedtuple('Benchmark', 'langs quick full expected')


def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a


def binary_trees(n):
    top = max(6, n)
    return ([2 ** (top + 2) - 1]
            + [2 ** (top - d + 4) * (2 ** (d + 1) - 1) for d in range(4, top + 1, 2)]
            + [2 ** (top + 1) - 1])


# Gabriel's tak, of 3n, 2n and n, remembering what it's worked out, as
# the programs don't.
def tak(n):
    known = {}

    def t(x, y, z):
        if not y < x:
            return z
        if (x, y, z) not in known:
            known[(x, y, z)] = t(t(x - 1, y, z), t(y - 1, z, x), t(z - 1, x, y))
        return known[(x, y, z)]
    return [t(3 * n, 2 * n, n)]


# The ways n queens can be placed (OEIS A000170).
QUEENS = [1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200, 73712, 365596, 2279184]


# Gabriel's deriv, done here too: what the programs print is the length of
# each derivative, summed, and how many atoms the last one has.
DERIV_OF = ['+', ['*', 3, 'x', 'x'], ['*', 'a', 'x', 'x'], ['*', 'b', 'x'], 5]


def deriv(n):
    def d(a):
        if not isinstance(a, list):
            return 1 if a == 'x' else 0
        if a[0] in ('+', '-'):
            return [a[0]] + [d(e) for e in a[1:]]
        if a[0] == '*':
            return ['*', a, ['+'] + [['/', d(e), e] for e in a[1:]]]
        return ['-', ['/', d(a[1]), a[2]], ['/', a[1], ['*', a[2], a[2], d(a[2])]]]

    def atoms(a):
        return sum(atoms(e) for e in a) if isinstance(a, list) else 1
    return [n * len(d(DERIV_OF)), atoms(d(DERIV_OF))]


# What each prints, worked out here; n-body's and spectral-norm's are
# whatever C prints (or, without C, what most print), since there's no
# shorter way to them.
BENCHMARKS = {
    'startup':       Benchmark(ALL + ('rust',), '0',     '0',        lambda n: [0]),
    'fib':           Benchmark(ALL,    '25',    '38',       lambda n: [fib(n)]),
    'tak':           Benchmark(ALL,    '6',     '10',       tak),
    'binary-trees':  Benchmark(ALL,    '12',    '18',       binary_trees),
    'nqueens':       Benchmark(ALL,    '8',     '13',       lambda n: [QUEENS[n]]),
    'deriv':         Benchmark(ALL,    '1000',  '2000000',  deriv),
    'n-body':        Benchmark(ALL,    '1000',  '1000000',  None),
    'spectral-norm': Benchmark(ALL,    '100',   '1000',     None),
    'ping-pong':     Benchmark(ACTORS, '10000', '2000000',  lambda n: [n]),
    'payload':       Benchmark(ACTORS, '10000', '1000000',  lambda n: [n, 5050]),
    'thread-ring':   Benchmark(ACTORS, '10000', '10000000', lambda n: [n % 503 + 1]),
    'fan-in':        Benchmark(ACTORS, '10000', '10000000', lambda n: [n // 100 * 5050]),
    'spawn':         Benchmark(ACTORS, '10000', '1000000',  lambda n: [n * (n - 1) // 2]),
    'idle-memory':   Benchmark(ACTORS, '10000', '1000000',  lambda n: [n]),
    'parallel-fib':  Benchmark(ACTORS, '15',    '30',       lambda k: [64 * fib(k)]),
}

Impl = collections.namedtuple('Impl', 'lang env flags')

IMPLS = {
    'slight':    Impl('slight', {}, []),
    'c':         Impl('c', {}, []),
    'go':        Impl('go', {'GOMAXPROCS': '1'}, []),
    'go-mc':     Impl('go', {}, []),
    'erlang':    Impl('erlang', {}, ['+S', '1:1']),
    'erlang-mc': Impl('erlang', {}, []),
    'node':      Impl('node', {}, []),
    'perl':      Impl('perl', {}, []),
    'python':    Impl('python', {}, []),
    'chez':      Impl('chez', {}, []),
    'rust':      Impl('rust', {}, []),
    'rust-mc':   Impl('rust', {'BENCH_MC': '1'}, []),
}


# Chez Scheme's command: Homebrew's is chez, Debian's scheme (and
# chezscheme). MIT Scheme is a scheme too, so a scheme counts only if it
# says it's Chez, whose --version is just its version.
def chez():
    for name in ('chez', 'chezscheme', 'scheme'):
        if shutil.which(name):
            try:
                done = subprocess.run([name, '--version'], capture_output=True, text=True, timeout=10)
            except (OSError, subprocess.SubprocessError):
                continue
            if re.match(r'\d+\.\d+', (done.stdout + done.stderr).strip()):
                return name
    return 'chez'


CHEZ = chez()

# The commands each language needs, and the one that says its version.
TOOLS = {
    'slight': (['node', 'clang'], ['clang', '--version']),
    'c':      (['cc'], ['cc', '--version']),
    'go':     (['go'], ['go', 'version']),
    'erlang': (['erl', 'erlc'], ['erl', '-noshell', '-eval',
                                 'io:format("OTP ~s, erts ~s", [erlang:system_info(otp_release), erlang:system_info(version)]), halt().']),
    'node':   (['node'], ['node', '--version']),
    'perl':   (['perl'], ['perl', '-e', 'print "perl $^V"']),
    'python': (['python3'], ['python3', '--version']),
    'chez':   ([CHEZ], [CHEZ, '--version']),
    'rust':   (['cargo', 'rustc'], ['rustc', '--version']),
}

EXT = {'slight': 'slight', 'c': 'c', 'go': 'go', 'erlang': 'erl', 'node': 'js', 'perl': 'pl', 'python': 'py', 'chez': 'ss',
       'rust': 'rs'}


# The tokio that bench/Cargo.lock pins.
def tokio_version():
    found = re.search(r'name = "tokio"\nversion = "([^"]+)"', open(os.path.join(ROOT, 'bench', 'Cargo.lock')).read())
    return found.group(1) if found else '?'


def slight_target():
    return 'aarch64' if platform.machine() in ('arm64', 'aarch64') else 'x86_64'


def source(bench, lang):
    name = bench.replace('-', '_') if lang == 'erlang' else bench
    return os.path.join(ROOT, 'bench', bench, name + '.' + EXT[lang])


def output_of(bench, lang):
    return os.path.join(BUILD, 'rust', 'release', bench) if lang == 'rust' else os.path.join(BUILD, lang, bench)


# The command that builds bench in lang, or None if it runs from source.
def build_command(bench, lang):
    src, out = source(bench, lang), output_of(bench, lang)
    if lang == 'slight':
        return ['node', os.path.join(ROOT, 'bin', 'slightc.ts'), '--target', slight_target(), '-o', out, src]
    if lang == 'c':
        return ['cc', '-O2', '-o', out, src, '-lm']
    if lang == 'go':
        return ['go', 'build', '-o', out, src]
    if lang == 'erlang':
        return ['erlc', '-o', os.path.join(BUILD, 'erlang'), src]
    if lang == 'chez':
        return [CHEZ, '--script', os.path.join(ROOT, 'bench', 'chez-compile.ss'), src, out]
    if lang == 'rust':
        return ['cargo', 'build', '--release', '--locked', '--quiet', '--manifest-path', os.path.join(ROOT, 'bench', 'Cargo.toml'),
                '--target-dir', os.path.join(BUILD, 'rust'), '--bin', bench]
    return None


# Erlang allows 262,144 processes unless told otherwise, and a higher limit
# costs memory from the start (47 MB for 4M), so it's raised only where
# that many are wanted, and only as far as they need.
MANY = ('spawn', 'idle-memory')


def process_limit(n):
    limit = 262144
    while limit < n + 1024:
        limit *= 2
    return limit


def run_command(bench, name, arg):
    impl = IMPLS[name]
    if impl.lang in ('slight', 'c', 'go', 'rust'):
        return [output_of(bench, impl.lang), arg]
    if impl.lang == 'chez':
        return [CHEZ, '--program', output_of(bench, 'chez'), arg]
    if impl.lang == 'erlang':
        limit = ['+P', str(process_limit(int(arg)))] if bench in MANY else []
        return (['erl'] + impl.flags + limit + ['-noshell', '-pa', os.path.join(BUILD, 'erlang'),
                '-run', bench.replace('-', '_'), 'main', arg])
    return [{'node': 'node', 'perl': 'perl', 'python': 'python3'}[impl.lang], source(bench, impl.lang), arg]


def run_env(name):
    env = {k: v for k, v in os.environ.items() if not k.startswith('SLIGHT_') and k not in ('GOMAXPROCS', 'BENCH_MC')}
    env.update(IMPLS[name].env)
    return env


# --- running -----------------------------------------------------------------

RUSAGE = os.path.join(BUILD, 'rusage')


# One run: its wall time, CPU time (user and system) and peak memory, as
# bench/rusage.c measures them; and its output and status. The helper and
# the program are a process group of their own, so a timeout kills both.
def run_once(cmd, env, timeout):
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err, \
         tempfile.NamedTemporaryFile(mode='r') as report:
        p     = subprocess.Popen([RUSAGE, report.name] + cmd, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                 env=env, cwd=ROOT, start_new_session=True)
        timer = threading.Timer(timeout, lambda: os.killpg(p.pid, signal.SIGKILL))
        timer.start()
        status = p.wait()
        timer.cancel()
        out.seek(0)
        err.seek(0)
        wall, cpu, rss = (report.read().split() + ['0', '0', '0'])[:3]
        return {
            'wall':   float(wall),
            'cpu':    float(cpu),
            'rss':    int(rss),
            'status': status,
            'out':    out.read().decode(errors='replace'),
            'err':    err.read().decode(errors='replace'),
        }


def lines(text):
    return [line.rstrip() for line in text.strip().split('\n')]


# The first line a command prints: on stdout, or on stderr if it prints
# nothing there (as Chez's --version).
def capture(cmd):
    try:
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return (done.stdout.strip() or done.stderr.strip()).split('\n')[0]
    except (OSError, subprocess.SubprocessError):
        return ''


def machine():
    system = platform.system()
    if system == 'Darwin':
        cpu    = capture(['sysctl', '-n', 'machdep.cpu.brand_string'])
        memory = int(capture(['sysctl', '-n', 'hw.memsize']) or 0)
        os_    = 'macOS ' + platform.mac_ver()[0]
    else:
        info   = open('/proc/cpuinfo').read() if os.path.exists('/proc/cpuinfo') else ''
        found  = re.search(r'^model name\s*:\s*(.*)$', info, re.M)
        cpu    = found.group(1) if found else platform.processor()
        mem    = re.search(r'^MemTotal:\s*(\d+) kB', open('/proc/meminfo').read(), re.M) if os.path.exists('/proc/meminfo') else None
        memory = int(mem.group(1)) * 1024 if mem else 0
        os_    = system + ' ' + platform.release()
    return {'os': os_, 'arch': platform.machine(), 'cpu': cpu, 'cores': os.cpu_count(), 'memory': memory}


def slug(text):
    text = re.sub(r'\((r|tm)\)', '', text.lower())
    return re.sub(r'[^a-z0-9]+', '-', text).strip('-')


# The commit slight was built from, with a + if its code (not the
# benchmarks or the docs) has changed since.
def commit():
    head  = capture(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'])
    dirty = capture(['git', '-C', ROOT, 'status', '--porcelain', '--', 'bin', 'compiler', 'runtime', 'lib'])
    return head + ('+' if dirty else '')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--quick', action='store_true', help='small sizes, to see that everything works')
    ap.add_argument('--only', help='benchmarks to run, comma-separated')
    ap.add_argument('--langs', help='implementations to run, comma-separated (' + ', '.join(IMPLS) + ')')
    ap.add_argument('--runs', type=int, default=5, help='timed runs of each (default 5)')
    ap.add_argument('--budget', type=float, default=60, help='seconds a slow one may take, which cuts its runs, to no fewer than 3')
    ap.add_argument('--timeout', type=float, default=600, help='seconds before a run is killed')
    ap.add_argument('--no-save', action='store_true', help="don't write bench/results/")
    ap.add_argument('--summary', metavar='FILE', help='write the Markdown summary of a saved run (FILE.json) again, and run nothing')
    args = ap.parse_args()

    if args.summary:
        base = args.summary[:-5] if args.summary.endswith('.json') else args.summary
        with open(base + '.json') as f:
            text = summary(json.load(f))
        with open(base + '.md', 'w') as f:
            f.write(text)
        print(text)
        return

    benches = args.only.split(',') if args.only else list(BENCHMARKS)
    names   = args.langs.split(',') if args.langs else list(IMPLS)
    for b in benches:
        if b not in BENCHMARKS:
            sys.exit('no benchmark called ' + b)
    for n in names:
        if n not in IMPLS:
            sys.exit('no implementation called ' + n)

    found    = {lang: all(shutil.which(c) for c in TOOLS[lang][0]) for lang in TOOLS}
    versions = {lang: capture(TOOLS[lang][1]) for lang in TOOLS if found[lang]}
    versions['slight'] = commit() + ', its runtime built by ' + versions.get('slight', '')
    if 'chez' in versions:
        versions['chez'] = 'Chez Scheme ' + versions['chez']
    if 'rust' in versions:
        versions['rust'] += ', tokio ' + tokio_version()
    missing  = sorted({IMPLS[n].lang for n in names if not found[IMPLS[n].lang]})
    if missing:
        print('not found, so left out: ' + ', '.join(missing))
    names = [n for n in names if found[IMPLS[n].lang]]

    # Build.
    os.makedirs(BUILD, exist_ok=True)
    done = subprocess.run(['cc', '-O2', '-o', RUSAGE, os.path.join(ROOT, 'bench', 'rusage.c')], capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit('building bench/rusage.c failed:\n' + done.stderr)
    builds = {}
    for b in benches:
        for lang in sorted({IMPLS[n].lang for n in names if IMPLS[n].lang in BENCHMARKS[b].langs}):
            cmd = build_command(b, lang)
            if cmd is None:
                continue
            os.makedirs(os.path.dirname(output_of(b, lang)), exist_ok=True)
            start = time.perf_counter()
            done  = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            took  = time.perf_counter() - start
            if done.returncode != 0:
                sys.exit('building %s in %s failed:\n%s%s' % (b, lang, done.stdout, done.stderr))
            artifact = output_of(b, lang) if lang != 'erlang' else os.path.join(BUILD, 'erlang', b.replace('-', '_') + '.beam')
            builds['%s/%s' % (lang, b)] = {'seconds': round(took, 3), 'bytes': os.path.getsize(artifact)}

    # Check each, once: its output, and how long it takes.
    results = {}
    jobs    = []
    for b in benches:
        bench = BENCHMARKS[b]
        size  = bench.quick if args.quick else bench.full
        entry = results[b] = {'size': size, 'impls': {}}
        checks = {}
        for n in names:
            if IMPLS[n].lang not in bench.langs:
                continue
            r = run_once(run_command(b, n, size), run_env(n), args.timeout)
            checks[n] = r
            print('%-13s %-10s %8.3fs  %s' % (b, n, r['wall'], 'status %d' % r['status'] if r['status'] else 'ran'))
        if bench.expected:
            expected = [str(v) for v in bench.expected(int(size))]
        else:
            ok   = [lines(r['out']) for r in checks.values() if r['status'] == 0]
            ref  = checks.get('c')
            expected = lines(ref['out']) if ref and ref['status'] == 0 else (collections.Counter(map(tuple, ok)).most_common(1)[0][0] if ok else [])
            expected = list(expected)
        entry['expected'] = expected
        for n, r in checks.items():
            wrong = r['status'] != 0 or lines(r['out']) != expected
            entry['impls'][n] = {'ok': not wrong, 'runs': []}
            if wrong:
                entry['impls'][n]['error'] = (r['out'] + r['err'])[-2000:]
                print('%-13s %-10s WRONG: %s' % (b, n, ' '.join(lines(r['out'] + r['err']))[-300:]))
            else:
                jobs.append((b, n, size, min(args.runs, max(3, int(args.budget / max(r['wall'], 0.001))))))

    # Time them, taking turns.
    rounds = max([j[3] for j in jobs], default=0)
    for i in range(rounds):
        print('round %d of %d' % (i + 1, rounds))
        for b, n, size, runs in jobs:
            if i >= runs:
                continue
            r = run_once(run_command(b, n, size), run_env(n), args.timeout)
            if r['status'] != 0 or lines(r['out']) != results[b]['expected']:
                results[b]['impls'][n]['ok']    = False
                results[b]['impls'][n]['error'] = (r['out'] + r['err'])[-2000:]
            results[b]['impls'][n]['runs'].append({k: r[k] for k in ('wall', 'cpu', 'rss')})

    stamp  = datetime.datetime.now()
    report = {
        'date':     stamp.isoformat(timespec='seconds'),
        'machine':  machine(),
        'versions': versions,
        'args':     {'quick': args.quick, 'runs': args.runs, 'budget': args.budget},
        'builds':   builds,
        'results':  results,
    }
    text = summary(report)
    print()
    print(text)
    if not args.no_save:
        name = '%s-%s-%s' % (stamp.strftime('%Y-%m-%d-%H%M'), slug(report['machine']['os'].split()[0]), slug(report['machine']['cpu']))
        if args.quick:
            name += '-quick'
        os.makedirs(os.path.join(ROOT, 'bench', 'results'), exist_ok=True)
        base = os.path.join(ROOT, 'bench', 'results', name)
        with open(base + '.json', 'w') as f:
            json.dump(report, f, indent=4)
            f.write('\n')
        with open(base + '.md', 'w') as f:
            f.write(text)
        print('wrote ' + os.path.relpath(base, ROOT) + '.json and .md')


# --- the summary -------------------------------------------------------------

def seconds(s):
    return '%.3f' % s if s < 10 else '%.1f' % s


def megabytes(b):
    return '%.1f' % (b / 1e6)


# Each implementation on every core, and the one-core one it's compared
# with: one core is the comparison, every core a table of its own (D237).
MC = {'go-mc': 'go', 'erlang-mc': 'erlang', 'rust-mc': 'rust'}


def median(r, key='wall'):
    return statistics.median(x[key] for x in r['runs']) if r and r['ok'] and r['runs'] else None


def summary(report):
    m   = report['machine']
    out = ['# Benchmarks, %s' % report['date'].replace('T', ' '), '',
           '%s, %s, %d cores, %.0f GB, %s.' % (m['cpu'], m['arch'], m['cores'], m['memory'] / 2 ** 30, m['os']), '']
    out += ['- %s: %s' % kv for kv in report['versions'].items()]
    out += ['', 'Times are in seconds, the median of each one\'s runs (and the fastest), and include starting '
                '(see startup: Erlang\'s VM takes about a seventh of a second). CPU is user and system time '
                'together, which goes past the time taken when more than one core works; memory is the peak '
                'resident set, in MB. "÷ slight" is the median over slight\'s, so under 1 is faster than slight. '
                'Go, Erlang and Rust are on one core here, as slight is; their defaults, every core, are in a table '
                'of their own, at the end.', '']
    startup = report['results'].get('startup', {}).get('impls', {})
    for b, entry in report['results'].items():
        impls = entry['impls']
        base  = median(impls.get('slight'))
        extra = b == 'idle-memory' and startup
        out  += ['## %s %s' % (b, entry['size']), '',
                 '| | time | fastest | CPU | memory | ÷ slight |' + (' bytes a process |' if extra else ''),
                 '|---|---:|---:|---:|---:|---:|' + ('---:|' if extra else '')]
        rows = []
        for n, r in impls.items():
            if n in MC:
                continue
            if not r['ok'] or not r['runs']:
                rows.append((float('inf'), '| %s | wrong output or failed | | | | |%s' % (n, ' |' if extra else '')))
                continue
            wall = statistics.median(x['wall'] for x in r['runs'])
            cpu  = statistics.median(x['cpu'] for x in r['runs'])
            rss  = statistics.median(x['rss'] for x in r['runs'])
            row  = '| %s | %s | %s | %s | %s | %s |' % (
                n, seconds(wall), seconds(min(x['wall'] for x in r['runs'])), seconds(cpu), megabytes(rss),
                '%.2f' % (wall / base) if base else '')
            if extra:
                s    = startup.get(n)
                per  = (rss - statistics.median(x['rss'] for x in s['runs'])) / int(entry['size']) if s and s['ok'] and s['runs'] else None
                row += ' %s |' % ('%.0f' % per if per is not None else '')
            rows.append((wall, row))
        out += [row for _, row in sorted(rows, key=lambda r: r[0])] + ['']
    out += every_core(report)
    if report['builds']:
        out += ['## Builds', '', '| | seconds | bytes |', '|---|---:|---:|']
        out += ['| %s | %.2f | %d |' % (k, v['seconds'], v['bytes']) for k, v in sorted(report['builds'].items())]
        out += ['']
    return '\n'.join(out)


# What every core gains or costs Go, Erlang and Rust: each one's time on
# every core, over its time on one core and over slight's.
def every_core(report):
    rows = []
    for b, entry in report['results'].items():
        impls = entry['impls']
        first = True
        for n, one in MC.items():
            if n not in impls:
                continue
            r = impls[n]
            if not r['ok'] or not r['runs']:
                rows.append('| %s | %s | wrong output or failed | | | | |' % (b if first else '', n))
            else:
                wall, solo, base = median(r), median(impls.get(one)), median(impls.get('slight'))
                rows.append('| %s | %s | %s | %s | %s | %s | %s |' % (
                    b if first else '', n, seconds(wall), seconds(median(r, 'cpu')), megabytes(median(r, 'rss')),
                    '%.2f' % (wall / solo) if solo else '', '%.2f' % (wall / base) if base else ''))
            first = False
    if not rows:
        return []
    return (['## Every core', '',
             'Go, Erlang and Rust with their defaults, every core. "÷ one core" is the median over the same '
             'language\'s on one core, so under 1 is what every core gains, over 1 what it costs; "÷ slight" is '
             'the median over slight\'s, on its one core.', '',
             '| | | time | CPU | memory | ÷ one core | ÷ slight |', '|---|---|---:|---:|---:|---:|---:|']
            + rows + [''])


if __name__ == '__main__':
    main()
