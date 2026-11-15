#!/usr/bin/env python3
"""What a change to the runtime costs and saves (the memory review, D294 to D298).

    bench/memory.py LABEL [ROOT [only,...]]   measure the checkout at ROOT (this one, without it)
    bench/memory.py --compare A B ...         the labels' results, side by side, against the first

Builds the benchmarks, and the programs in bench/memory/ (always this checkout's), with ROOT's slightc
for x86-64, and prints for each benchmark callgrind's instructions at a small size, and the best time
of three and the peak resident set at a bigger one; and for the memory programs, the resident set while
they sleep at the end, read from /proc, and its peak. One island. Results are kept in
build/bench/memory/LABEL/res.json. To compare with an earlier commit: git worktree add DIR COMMIT, and
give DIR as ROOT. Linux's alone (/proc, valgrind), and the times are a shared machine's: the
instructions are what to trust. `only` picks benchmarks by name, and `mem` the memory programs.
"""
import os, re, resource, subprocess, sys, time, json
HERE = os.path.dirname(os.path.abspath(__file__))
S    = os.path.join(HERE, 'memory')
KEPT = os.path.join(HERE, '..', 'build', 'bench', 'memory')

def compare(labels):
    R = {l: json.load(open(os.path.join(KEPT, l, 'res.json'))) for l in labels}
    b = R[labels[0]]
    def table(title, key, show):
        print('%-26s' % title, *['%15s' % l for l in labels])
        for k, v in b.items():
            if key in v: print('%-26s' % k, *[show(R[l][k], v) if k in R[l] else '%15s' % '-' for l in labels])
    table('instr (M), vs first', 'ir',   lambda r, v: '%8.1f %+5.1f%%' % (r['ir'], 100 * (r['ir'] / v['ir'] - 1)))
    table('best time, s',        'ir',   lambda r, v: '%15.3f' % r['wall'])
    table('peak MB',             'ir',   lambda r, v: '%15.1f' % r['rss'])
    table('at rest MB (peak)',   'held', lambda r, v: '%8.1f (%4.0f)' % (r['held'], r['peak']))

if len(sys.argv) > 1 and sys.argv[1] == '--compare':
    compare(sys.argv[2:])
    sys.exit(0)
if len(sys.argv) < 2: sys.exit(__doc__)
label = sys.argv[1]
root  = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.abspath(os.path.join(HERE, '..'))
only  = set(sys.argv[3].split(',')) if len(sys.argv) > 3 else None
out   = os.path.join(KEPT, label)
os.makedirs(out, exist_ok=True)
B = [  # name, source, wall args, callgrind args, env
    ('ping-pong',    'bench/ping-pong/ping-pong.slight',       '2000000',  '200000', {}),
    ('payload',      'bench/payload/payload.slight',           '1000000',  '100000', {}),
    ('thread-ring',  'bench/thread-ring/thread-ring.slight',   '10000000', '1000000', {}),
    ('fan-in',       'bench/fan-in/fan-in.slight',             '5000000',  '500000', {}),
    ('spawn',        'bench/spawn/spawn.slight',               '1000000',  '100000', {}),
    ('idle-memory',  'bench/idle-memory/idle-memory.slight',   '1000000',  '100000', {}),
    ('binary-trees', 'bench/binary-trees/binary-trees.slight', '16',       '14', {}),
    ('deriv',        'bench/deriv/deriv.slight',               '1000000',  '100000', {}),
    ('nqueens',      'bench/nqueens/nqueens.slight',           '11',       '9', {}),
    ('n-body',       'bench/n-body/n-body.slight',             '500000',   '50000', {}),
    ('tak',          'bench/tak/tak.slight',                   '9',        '7', {}),
    ('bank',         'bench/bank/bank.slight',                 '250000',   '25000', {'SLIGHT_CLOCK': 'virtual'}),
    ('workers-t',      S + '/workers.slight',  '1000 3000', '1000 300', {'SLIGHT_CLOCK': 'virtual'}),
    ('workers-bare-t', S + '/workers-bare.slight', '1000 3000', '1000 300', {'SLIGHT_CLOCK': 'virtual'}),
    ('workers-few-t',  S + '/workers.slight',  '10 300000', '10 30000', {'SLIGHT_CLOCK': 'virtual'}),
]
M = [  # name, source in S, args
    ('idle-state 100000x1',   'idle',  '100000 1'),
    ('idle-state 100000x100', 'idle',  '100000 100'),
    ('idle-none 100000',      'idle',  '100000 0'),
    ('workers 1000x10000',    'workers',  '1000 10000'),
    ('workers-bare 1000x10000', 'workers-bare', '1000 10000'),
    ('quiet-after-64MB',      'quiet', '4000000 1000'),
    ('one-heap 16MBx5',       'one-heap',  '1000000 5'),
    ('burst-ended 20000x1000', 'burst', '20000 1000'),
]
def env(extra):
    e = {k: v for k, v in os.environ.items() if not k.startswith('SLIGHT_')}
    e['SLIGHT_ISLANDS'] = '1'
    e.update(extra)
    return e
def build(name, src):
    exe = os.path.join(out, name)
    r = subprocess.run(['node', os.path.join(root, 'bin/slightc.ts'), '--target', 'x86_64', '-o', exe, src],
                       capture_output=True, text=True, cwd=root)
    if r.returncode: sys.exit(r.stderr)
    return exe
def wall(exe, args, e):
    best, rss, outp = 1e9, 0, None
    for _ in range(3):
        t = time.time()
        pid = os.fork()
        if pid == 0:
            fd = os.open(os.path.join(out, 'last.out'), os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
            os.dup2(fd, 1); os.dup2(fd, 2)
            os.execve(exe, [exe] + args.split(), e)
        _, st, ru = os.wait4(pid, 0)
        best = min(best, time.time() - t)
        rss = max(rss, ru.ru_maxrss)
        o = open(os.path.join(out, 'last.out')).read()
        o = re.sub(r'^#.*\n', '', o, flags=re.M)
        if outp is None: outp = o
        if st != 0 or o != outp: print('!! %s differs or failed: %r' % (exe, o[:200]))
    return best, rss / 1024, outp.strip().replace('\n', ' ')[:40]
def callgrind(exe, args, e):
    r = subprocess.run(['valgrind', '--tool=callgrind', '--callgrind-out-file=' + os.path.join(out, 'cg.out'), exe] + args.split(),
                       capture_output=True, text=True, env=e)
    m = re.search(r'Collected : (\d+)', r.stderr)
    return int(m.group(1)) / 1e6 if m else -1
def resident(exe, args, e):
    p = subprocess.Popen([exe] + args.split(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=e)
    hist = []
    while p.poll() is None:
        try:
            for l in open('/proc/%d/status' % p.pid):
                if l.startswith('VmRSS'): hist.append(int(l.split()[1]))
        except Exception: pass
        time.sleep(0.05)
    return (hist[-8] if len(hist) > 8 else (hist[0] if hist else 0)) / 1024, max(hist) / 1024
res = {}
print('%-26s %10s %8s %9s  %s' % (label, 'Minstr', 'wall s', 'peak MB', 'out'))
for name, src, a, ca, x in B:
    if only and name not in only: continue
    exe = build(name, src)
    ir = callgrind(exe, ca, env(x))
    w, rss, o = wall(exe, a, env(x))
    res[name] = dict(ir=ir, wall=w, rss=rss)
    print('%-26s %10.1f %8.3f %9.1f  %s' % (name, ir, w, rss, o), flush=True)
print('%-26s %10s %10s' % ('', 'held MB', 'peak MB'))
for name, prog, a in M:
    if only and name.split()[0] not in only and 'mem' not in only: continue
    exe = build(prog, os.path.join(S, prog + '.slight'))
    held, peak = resident(exe, a, env({}))
    res[name] = dict(held=held, peak=peak)
    print('%-26s %10.1f %10.1f' % (name, held, peak), flush=True)
try: old = json.load(open(os.path.join(out, 'res.json')))
except Exception: old = {}
old.update(res)
json.dump(old, open(os.path.join(out, 'res.json'), 'w'), indent=1)
