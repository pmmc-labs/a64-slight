#!/usr/bin/env python3
"""Runs examples/airline.slight with sets of options, and times them (D214).

    bench/airline.py                                10 and 100 random what-ifs on a made-up day
    bench/airline.py --random 10,100,1000 --seed 1,2,3
    bench/airline.py --day small --random 40 --seed 7
    bench/airline.py --random 50 --rounds 4         four rounds of 50 more, each from the history the last kept
    bench/airline.py --random day,100 --every 15,30,60
    bench/airline.py --recover --remedies 10,50,200
    bench/airline.py --day small --recover --disruption '(:close "ORD" 600 720)' --remedies all

An option given a list, comma-separated, is swept: every combination of
them is run, --repeat times each, and each run is a row of the table, as
it finishes. The program is run with --time, and the table has what it
says of itself (how long the day took to read, the base to run, each
branch or remedy, the checks and the ranking) beside what the run cost
the machine (wall and CPU seconds and the peak resident set, measured by
bench/rusage.c); and what it came to (the minutes simulated, how many
branches rejoined a run, the best remedy). --json writes all of it, every
branch's time too.

The day is gen:SEED (the default, gen:1), the size of a small airline's,
made by examples/airline-gen.slight with its record appended; small,
examples/airline-day.sexp; or a file. A --recover with no file of its own
is told of one disruption: for the small day t/data/airline-recover.sexp's;
for another, --disruption, or a delay of 60 minutes of the first leg into
the first hub from 09:00.

Everything goes under build/bench/airline/: the binaries, the days made,
the files written for --recover and --rounds, and each run's output.
Timings mean something only natively: a --target that isn't this
machine's runs under qemu. On a made-up day, --recover keeps every
remedy's run whole, and runs out of heap somewhere past 2,000 of them
(D214), so --remedies all is for the small day.
"""

import argparse
import datetime
import itertools
import json
import os
import platform
import re
import signal
import statistics
import subprocess
import sys
import tempfile
import threading
import time

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD  = os.path.join(ROOT, 'build', 'bench', 'airline')
RUSAGE = os.path.join(BUILD, 'rusage')
SMALL  = os.path.join(ROOT, 'examples', 'airline-day.sexp')


def native():
    return 'aarch64' if platform.machine() in ('arm64', 'aarch64') else 'x86_64'


def prefix(target):
    return [] if target == native() else ['qemu-' + target]


def listed(text, conv=int):
    return [conv(x) for x in text.split(',') if x]


def what_ifs(x):
    return x if x in ('day', 'all') else int(x)


# --- building ----------------------------------------------------------------

def build(target):
    os.makedirs(os.path.join(BUILD, target), exist_ok=True)
    done = subprocess.run(['cc', '-O2', '-o', RUSAGE, os.path.join(ROOT, 'bench', 'rusage.c')], capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit('building bench/rusage.c failed:\n' + done.stderr)
    out = {}
    for name in ('airline', 'airline-gen'):
        out[name] = os.path.join(BUILD, target, name)
        done = subprocess.run(['node', os.path.join(ROOT, 'bin', 'slightc.ts'), '--target', target, '-o', out[name],
                               os.path.join(ROOT, 'examples', name + '.slight')], cwd=ROOT, capture_output=True, text=True)
        if done.returncode != 0:
            sys.exit(f'building examples/{name}.slight failed:\n' + done.stdout + done.stderr)
    return out


# A made-up day, with its record, kept till the generator or the program
# changes.
def day_file(spec, bins, target):
    if spec == 'small':
        return SMALL
    if not spec.startswith('gen:'):
        return os.path.abspath(spec)
    seed = int(spec[4:])
    path = os.path.join(BUILD, f'day-{seed}.sexp')
    sources = [os.path.join(ROOT, 'examples', f) for f in ('airline-gen.slight', 'airline.slight')]
    if os.path.exists(path) and os.path.getmtime(path) > max(map(os.path.getmtime, sources)):
        return path
    env = clean_env({})
    made = subprocess.run(prefix(target) + [bins['airline-gen'], str(seed)], capture_output=True, env=env, check=True)
    with open(path, 'wb') as f:
        f.write(made.stdout)
    record = subprocess.run(prefix(target) + [bins['airline'], '--record', path], capture_output=True, env=env, check=True)
    with open(path, 'ab') as f:
        f.write(record.stdout)
    return path


# The file --recover reads, for a day without one of its own.
def recover_file(day, disruption, check, top):
    if day == SMALL and not disruption:
        return os.path.join(ROOT, 't', 'data', 'airline-recover.sexp')
    if not disruption:
        text  = open(day).read()
        hubs  = re.search(r'^; The hubs: (.*)\.$', text, re.M)
        legs  = re.findall(r'^\(:leg "(\S+)" "\S+" "(\S+)" "(\S+)" +(\d+)', text, re.M)
        into  = [l for l in legs if int(l[3]) >= 540 and (not hubs or l[2] == hubs.group(1).split()[0])]
        disruption = f'(:delay "{(into or legs)[0][0]}" 60)'
    path = os.path.join(BUILD, 'recover-' + slug(os.path.basename(day) + disruption) + '.sexp')
    with open(path, 'w') as f:
        f.write(f'(:disruption "d" {disruption})\n(:check {check})\n(:top {top})\n')
    return path


def slug(text):
    return re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')


# --- running -----------------------------------------------------------------

def clean_env(extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith('SLIGHT_')}
    env.update(extra)
    return env


# One run: its wall time, CPU time and peak memory, as bench/rusage.c
# measures them (a process forked from Python counts Python's memory in its
# peak), its status and its output, stdout and stderr together. The helper
# and the program are a process group of their own, so a timeout kills
# both.
def run_once(cmd, env, timeout):
    with tempfile.TemporaryFile() as out, tempfile.NamedTemporaryFile(mode='r') as report:
        p = subprocess.Popen([RUSAGE, report.name] + cmd, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                             env=env, cwd=ROOT, start_new_session=True)
        timer = threading.Timer(timeout, lambda: os.killpg(p.pid, signal.SIGKILL))
        timer.start()
        status = p.wait()
        timer.cancel()
        out.seek(0)
        wall, cpu, rss = (report.read().split() + ['0', '0', '0'])[:3]
        return dict(wall=float(wall), cpu=float(cpu), rss=int(rss), status=status, out=out.read().decode(errors='replace'))


MS = r'([\d.]+) ms'


def found(pattern, text, conv=float, default=None):
    m = re.search(pattern, text, re.M)
    return conv(m.group(1)) if m else default


# What a run says of itself, with --time.
def parsed(mode, out):
    r = dict(read_ms=found(r'^time: the day (?:and its history )?read in ' + MS, out),
             day=found(r'read in [\d.]+ ms: (.*)$', out, str))
    if mode == 'explore':
        branch_ms = [float(x) for x in re.findall(r'^    time: ' + MS + r' for \d+ minutes', out, re.M)]
        final = re.search(r'^"(\d+) minutes for the base, and (\d+) for its branches, of which (\d+) their own"$', out, re.M)
        r.update(base_ms=found(r'^base: .*\n.*\n    time: ' + MS, out),
                 branches_ms=found(r'^time: the branches in ' + MS, out),
                 before_ms=found(r'^time: the branches in [\d.]+ ms, ' + MS, out),
                 branch_ms=branch_ms, ran=len(branch_ms),
                 kept=len(re.findall(r', kept$', out, re.M)),
                 rejoined=len(re.findall(r"^    the same as .*, then .*'s again$", out, re.M)),
                 base_minutes=int(final.group(1)) if final else None,
                 minutes=int(final.group(2)) if final else None,
                 own=int(final.group(3)) if final else None)
    else:
        made = re.search(r'^time: (\d+) remedies made in ' + MS + r'; (\d+) run in ' + MS + r', (\d+) minutes$', out, re.M)
        final = re.search(r'^"(\d+) remedies, (\d+) checked, (\d+) that pass, and the best (\S+)"$', out, re.M)
        r.update(base_ms=found(r'^time: the base in ' + MS, out),
                 made=int(made.group(1)) if made else None, make_ms=float(made.group(2)) if made else None,
                 ran=int(made.group(3)) if made else None, run_ms=float(made.group(4)) if made else None,
                 minutes=int(made.group(5)) if made else None,
                 check_ms=found(r'^time: \d+ checked in ' + MS, out),
                 rank_ms=found(r'^time: \d+ ranked in ' + MS, out),
                 rejoined=found(r'; (\d+) of the remedies rejoin', out, int),
                 checked=int(final.group(2)) if final else None, passed=int(final.group(3)) if final else None,
                 best=final.group(4) if final else None,
                 best_cost=found(r'^    1\. \S+ .*: cost (\d+):', out, int))
    return r


# --- the table ---------------------------------------------------------------

def num(x, f='%.1f'):
    return '-' if x is None else f % x


def spread(xs):
    if not xs:
        return '-'
    s = sorted(xs)
    f = '%.0f' if statistics.median(s) >= 10 else '%.1f'
    return '/'.join(f % x for x in (statistics.median(s), s[min(len(s) - 1, int(0.95 * len(s)))], s[-1]))


EXPLORE = [('seed', 5, lambda c, r: str(c['seed'])), ('what-ifs', 8, lambda c, r: str(c['random'])),
           ('every', 5, lambda c, r: str(c['every'])), ('round', 5, lambda c, r: c['round_label']),
           ('wall s', 7, lambda c, r: '%.2f' % r['wall']), ('cpu s', 7, lambda c, r: '%.2f' % r['cpu']),
           ('peak MB', 7, lambda c, r: '%.0f' % (r['rss'] / 1e6)), ('read ms', 7, lambda c, r: num(r['read_ms'])),
           ('base ms', 7, lambda c, r: num(r['base_ms'])), ('branch ms', 9, lambda c, r: num(r['branches_ms'])),
           ('ran', 5, lambda c, r: str(r['ran'])), ('kept', 4, lambda c, r: str(r['kept'])),
           ('ms each p50/p95/max', 19, lambda c, r: spread(r['branch_ms'])),
           ('minutes', 8, lambda c, r: num(r['minutes'], '%d')), ('own', 8, lambda c, r: num(r['own'], '%d')),
           ('µs/min', 6, lambda c, r: num(r['branches_ms'] * 1000 / r['minutes'], '%.0f') if r['minutes'] else '-'),
           ('rejoined', 8, lambda c, r: f"{r['rejoined']}/{r['ran']}"),
           ('history', 7, lambda c, r: '%.1fM' % (c['history_bytes'] / 1e6) if c.get('history_bytes') else '-')]

RECOVER = [('seed', 5, lambda c, r: str(c['seed'])), ('remedies', 8, lambda c, r: str(c['remedies'])),
           ('every', 5, lambda c, r: str(c['every'])),
           ('wall s', 7, lambda c, r: '%.2f' % r['wall']), ('cpu s', 7, lambda c, r: '%.2f' % r['cpu']),
           ('peak MB', 7, lambda c, r: '%.0f' % (r['rss'] / 1e6)), ('base ms', 7, lambda c, r: num(r['base_ms'])),
           ('made', 6, lambda c, r: num(r['made'], '%d')), ('make ms', 7, lambda c, r: num(r['make_ms'])),
           ('ran', 5, lambda c, r: num(r['ran'], '%d')), ('run ms', 8, lambda c, r: num(r['run_ms'])),
           ('ms each', 7, lambda c, r: num(r['run_ms'] / r['ran']) if r['ran'] else '-'),
           ('minutes', 8, lambda c, r: num(r['minutes'], '%d')),
           ('rejoined', 8, lambda c, r: f"{num(r['rejoined'], '%d')}/{num(r['ran'], '%d')}"),
           ('check ms', 8, lambda c, r: num(r['check_ms'])), ('rank ms', 7, lambda c, r: num(r['rank_ms'])),
           ('checked', 7, lambda c, r: num(r['checked'], '%d')), ('passed', 6, lambda c, r: num(r['passed'], '%d')),
           ('best', 12, lambda c, r: '-' if not r['best'] or r['best'] == 'none' else f"{r['best']} {r['best_cost']}")]


def row(cols, c, r):
    return '  '.join(f(c, r).rjust(w) for _, w, f in cols)


def header(cols):
    return '  '.join(name.rjust(w) for name, w, _ in cols)


# --- the program -------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog='\n'.join(__doc__.split('\n')[2:]))
    ap.add_argument('--day', default='gen:1', help='gen:SEED (default gen:1), small, or a file')
    ap.add_argument('--random', default='10,100', help="what-ifs made at random, a list (default 10,100); 'day' for the day's own")
    ap.add_argument('--seed', default='1', help='seeds for the what-ifs or the remedies picked, a list (default 1)')
    ap.add_argument('--every', default='30', help='minutes between checkpoints, a list (default 30; 0 for none)')
    ap.add_argument('--rounds', type=int, default=1,
                    help='explore a round at a time: round k runs k times --random what-ifs, from the history the last kept')
    ap.add_argument('--recover', nargs='?', const='', metavar='FILE', help='recover from a disruption, in place of exploring')
    ap.add_argument('--disruption', help='the disruption for --recover with no file, as an s-expression')
    ap.add_argument('--remedies', default='20,100', help="with --recover, remedies picked by seed, a list (default 20,100); 'all'")
    ap.add_argument('--check', type=int, default=12, help='with --recover and no file: how many to check (default 12)')
    ap.add_argument('--top', type=int, default=3, help='with --recover and no file: how many ranked to show (default 3)')
    ap.add_argument('--repeat', type=int, default=1, help='runs of each; the table has the fastest, the JSON every one')
    ap.add_argument('--target', default=native(), choices=('aarch64', 'x86_64'), help="the target (default this machine's)")
    ap.add_argument('--sched', type=int, help='SLIGHT_SCHED, processes run in an order from this seed')
    ap.add_argument('--heap-max', help='SLIGHT_HEAP_MAX, each process\'s heap cap (default the runtime\'s, 1G)')
    ap.add_argument('--timeout', type=float, default=1800, help='seconds before a run is killed (default 1800)')
    ap.add_argument('--json', metavar='FILE', help='write the runs, and every branch\'s time, as JSON')
    args = ap.parse_args()

    if args.target != native():
        print(f'note: {args.target} runs under qemu here, so its times mean little', file=sys.stderr)
    bins = build(args.target)
    day = day_file(args.day, bins, args.target)
    env = clean_env({k: v for k, v in (('SLIGHT_SCHED', args.sched), ('SLIGHT_HEAP_MAX', args.heap_max)) if v is not None})
    env = {k: str(v) for k, v in env.items()}
    os.makedirs(os.path.join(BUILD, 'runs'), exist_ok=True)

    mode = 'explore' if args.recover is None else 'recover'
    seeds, everys = listed(args.seed), listed(args.every)
    if mode == 'explore':
        configs = [dict(random=n, seed=s, every=e) for n, s, e in itertools.product(listed(args.random, what_ifs), seeds, everys)]
        cols = EXPLORE
    else:
        rfile = args.recover or recover_file(day, args.disruption, args.check, args.top)
        configs = [dict(remedies=n, seed=s, every=e) for n, s, e in itertools.product(listed(args.remedies, what_ifs), seeds, everys)]
        cols = RECOVER

    # the day's own what-ifs, or all the remedies, are the same for any seed
    configs = [dict(c, seed='-') if 'day' in c.values() or 'all' in c.values() else c for c in configs]
    configs = [c for i, c in enumerate(configs) if c not in configs[:i]]
    print(f'the day: {os.path.relpath(day, ROOT)}; {args.target}' + (' (under qemu)' if prefix(args.target) else '')
          + f'; {len(configs)} set{"s" if len(configs) > 1 else ""} of options'
          + (f', {args.rounds} rounds each' if mode == 'explore' and args.rounds > 1 else '')
          + (f', each run {args.repeat} times' if args.repeat > 1 else ''))
    if mode == 'recover':
        print(f'recovering, as {os.path.relpath(rfile, ROOT)} says: ' + ' '.join(l for l in open(rfile).read().split('\n')
                                                                               if l and not l.startswith(';')))
    print(header(cols))
    results, failed = [], 0
    try:
        for c in configs:
            history = os.path.join(BUILD, f'history-{c["seed"]}-{c["every"]}.sexp')
            if os.path.exists(history):
                os.remove(history)
            rounds = args.rounds if mode == 'explore' and c['random'] != 'day' else 1
            for k in range(1, rounds + 1):
                opts = ['--time', '--every', str(c['every'])]
                if mode == 'explore':
                    if c['random'] != 'day':
                        opts += ['--random', str(c['random'] * k), '--seed', str(c['seed'])]
                    # each repeat of a round from the last round's history,
                    # not from what the one before it kept
                    if rounds > 1:
                        opts += (['--from', history] if k > 1 else []) + ['--keep', history + '.next']
                else:
                    opts += ['--recover', rfile] + (['--seed', str(c['seed'])] if c['seed'] != '-' else [])
                    opts += [] if c['remedies'] == 'all' else ['--remedies', str(c['remedies'])]
                cmd = prefix(args.target) + [bins['airline']] + opts + [day]
                runs = [run_once(cmd, env, args.timeout) for _ in range(args.repeat)]
                best = min(runs, key=lambda r: r['wall'])
                if os.path.exists(history + '.next'):
                    os.replace(history + '.next', history)
                name = slug(f'{os.path.basename(day)} {mode} '
                            + ' '.join(f'{k2} {v}' for k2, v in c.items()) + (f' round {k}' if rounds > 1 else '')) + '.out'
                with open(os.path.join(BUILD, 'runs', name), 'w') as f:
                    f.write(best['out'])
                c2 = dict(c, round=k, round_label=f'{k}/{rounds}' if rounds > 1 else '-',
                          history_bytes=os.path.getsize(history) if rounds > 1 and os.path.exists(history) else None)
                if best['status'] != 0:
                    failed += 1
                    tail = '\n'.join('    ' + l for l in best['out'].rstrip().split('\n')[-5:])
                    print(f'FAIL (exit {best["status"]}): {" ".join(opts)}\n{tail}\n    (all of it in {os.path.join(BUILD, "runs", name)})')
                    results.append(dict(options=c2, command=cmd, status=best['status']))
                    continue
                r = dict(best, **parsed(mode, best['out']))
                print(row(cols, c2, r), flush=True)
                results.append(dict(options=c2, command=cmd, status=0, output=os.path.join(BUILD, 'runs', name),
                                    runs=[dict(wall=x['wall'], cpu=x['cpu'], rss=x['rss']) for x in runs],
                                    wall_median=statistics.median(x['wall'] for x in runs),
                                    **{k2: v for k2, v in r.items() if k2 not in ('out', 'status')}))
    except KeyboardInterrupt:
        print('stopped', file=sys.stderr)

    if results and results[-1].get('day'):
        said = ("ms each p50/p95/max is a branch's time, and µs/min the branches' over the minutes they simulated"
                if mode == 'explore' else "made is every remedy the program made, and ran those it picked and ran")
        print(f'({results[-1]["day"]}; {said}; the outputs are in {os.path.relpath(os.path.join(BUILD, "runs"), ROOT)}/)')
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(dict(date=datetime.datetime.now().isoformat(timespec='seconds'), mode=mode, target=args.target,
                           machine=platform.machine(), day=day, args=vars(args), results=results), f, indent=4)
            f.write('\n')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
