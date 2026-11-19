#!/usr/bin/env python3
"""Runs examples/mold.slight every way it keeps its mold, and times them.

    bench/mold.py                       a made-up day, on 1 island and on every core
    bench/mold.py --day 15:8:28:40      the day t/models/mold.py gen 15 8 28 40 makes
    bench/mold.py --day day.sexp --islands 1,2,4,8
    bench/mold.py --repeat 5 --system   and with :system asked whether the mold is quiet
    bench/mold.py --day 17:10:30:20 --no-every
                                        a bigger day, without a process for every choice
    bench/mold.py --split 4             the first four choices split, in place of two
    bench/mold.py --best                only the cheapest wanted: the search with its bound
    bench/mold.py --day examples/mold-oc.ssim --first
                                        an SSIM day: the first day that flies, one search
    bench/mold.py --check               an aircraft's minutes the simulation's to check

A row is a way of keeping the mold, on a number of islands: the mold as
a value (--values), as actors turning back by their undo stacks, and
each with its first choices split across the islands (--split, 2 unless
given: more choices are more and smaller branches, which more islands
want, the time being the busiest island's), then
with every choice a process (--split 99), which copies the mold at each
and never turns back. Its time is the median of --repeat runs, with what
the run cost the machine: CPU seconds, which go past the time taken when
more than one island works, and the peak resident set. (A process for
every choice holds every choice of a level at once, 1.5 GB for 50,000:
--no-every leaves those rows out, as a bigger day needs.) Decisions a
second is the search's decisions over the time, starting the program
included; "÷ values" is the time over the first row's, the mold as a
value on one island.

Every run's count and best are checked against t/models/mold.py solve,
and its decisions and conflicts against the first run's: the search is
the same however the mold is kept, so they must be. With --best the
best is checked against t/models/mold.py best, and the decisions only
where there's one search: several find their bounds in the order the
islands happen to run, so a row's decisions are its first run's.

The program runs on the virtual clock, which is how a mold of actors
knows it's quiet, so the times are measured from outside. Everything
goes under build/bench/mold/. Timings mean something only natively, and
on a machine with nothing else to do.
"""

import argparse
import os
import re
import resource
import statistics
import subprocess
import sys
import time

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'bench', 'mold')
MODEL = os.path.join(ROOT, 't', 'models', 'mold.py')

def ways(split, system):
    return [
        ('a value',                         ['--values']),
        (f'a value, split {split}',         ['--values', '--split', split]),
        ('a value, every choice',           ['--values', '--split', '99']),
        ('actors, undo',                    []),
        (f'actors, split {split}',          ['--split', split]),
        ('actors, every choice',            ['--split', '99']),
    ] + ([
        ('actors, undo, :system',           ['--settle', 'system']),
        (f'actors, split {split}, :system', ['--settle', 'system', '--split', split]),
    ] if system else [])

def run(binary, args, day, islands):
    """One run: (seconds, cpu seconds, what it printed)."""
    env = dict(os.environ, SLIGHT_CLOCK='virtual', SLIGHT_ISLANDS=str(islands))
    args = [a for a in args if a]
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter()
    p = subprocess.Popen([binary, '--stats', *args, day], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    out = p.communicate()[0]
    took = time.perf_counter() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if p.returncode != 0:
        sys.exit(f'{" ".join(args)} on {islands} islands exited {p.returncode}:\n{out}')
    cpu = (after.ru_utime + after.ru_stime) - (before.ru_utime + before.ru_stime)
    return took, cpu, out

def peak_mb(binary, args, day, islands):
    """The peak resident set of one run, in MB: a child of its own, since
    RUSAGE_CHILDREN's is the most of any child so far."""
    code = ('import os,resource,subprocess,sys\n'
            'subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n'
            'print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)\n')
    env = dict(os.environ, SLIGHT_CLOCK='virtual', SLIGHT_ISLANDS=str(islands))
    args = [a for a in args if a]
    kb = int(subprocess.run([sys.executable, '-c', code, binary, *args, day], env=env,
                            capture_output=True, text=True).stdout)
    return kb / (1024 if sys.platform != 'darwin' else 1024 * 1024)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--day', default='15:8:28:40', help='SEED:AIRCRAFT:LEGS:SLACK for the model to make, or a file')
    ap.add_argument('--islands', default=f'1,{os.cpu_count()}', help='the numbers of islands, comma-separated')
    ap.add_argument('--repeat', type=int, default=3)
    ap.add_argument('--split', default='2', help='how many choices deep the split rows split')
    ap.add_argument('--system', action='store_true', help='and the rows that ask :system')
    ap.add_argument('--best', action='store_true', help='only the cheapest wanted, the search with its bound')
    ap.add_argument('--first', action='store_true', help='only the first day that flies wanted: one search, no split')
    ap.add_argument('--check', action='store_true', help="an aircraft's minutes checked by the simulation, no rule of the mold")
    ap.add_argument('--slack', help="an SSIM day's aircraft may fly their lines' minutes and this per cent more")
    ap.add_argument('--no-every', action='store_true', help='without the rows with a process for every choice')
    ap.add_argument('--target', default={'arm64': 'aarch64', 'aarch64': 'aarch64'}.get(os.uname().machine, 'x86_64'))
    o = ap.parse_args()

    os.makedirs(BUILD, exist_ok=True)
    binary = os.path.join(BUILD, 'mold')
    subprocess.run(['node', os.path.join(ROOT, 'bin', 'slightc.ts'), '--target', o.target, '-o', binary,
                    os.path.join(ROOT, 'examples', 'mold.slight')], check=True)
    if os.path.exists(o.day):
        day = o.day
    else:
        day = os.path.join(BUILD, 'day-' + o.day.replace(':', '-') + '.sexp')
        with open(day, 'w') as f:
            subprocess.run([sys.executable, MODEL, 'gen', *o.day.split(':')], stdout=f, check=True)
    want = subprocess.run([sys.executable, MODEL, 'first' if o.first else 'best' if o.best else 'solve', day],
                          capture_output=True, text=True, check=True).stdout
    more = [a for a in ['--best' if o.best else '', '--first' if o.first else '',
                        *(['--check', 'minutes'] if o.check else []),
                        *(['--slack', o.slack] if o.slack else [])] if a]

    print('; '.join(want.splitlines()))
    print()
    print('| the mold | islands | time | CPU | MB | decisions | conflicts | bounded | stops | decisions/s | ÷ values | forks | nodes |')
    print('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
    first = base = None
    for name, args in ways(o.split, o.system):
        if (o.no_every and '99' in args) or (o.first and '--split' in args):
            continue
        for islands in [int(k) for k in o.islands.split(',')]:
            if islands > 1 and '--split' not in args:
                continue                    # one search is one island's, however many there are
            args = [*more, *args]
            runs = [run(binary, args, day, islands) for _ in range(o.repeat)]
            out = runs[0][2]
            if any(r[2][:len(want)] != want for r in runs):
                sys.exit(f'{name} on {islands} islands:\n{out}and not\n{want}')
            m = re.search(r'decisions (\d+), conflicts (\d+), bounded (\d+), stops (\d+), forks (\d+), nodes (\d+)', out)
            decisions, conflicts, bounded, stops, forks, nodes = map(int, m.groups())
            if first is None:
                first = (decisions, conflicts)
            elif (decisions, conflicts) != first and not (o.best and '--split' in args):
                sys.exit(f'{name} on {islands} islands made {decisions} decisions and {conflicts} conflicts, not {first}')
            took = statistics.median(r[0] for r in runs)
            cpu = statistics.median(r[1] for r in runs)
            base = base or took
            print(f'| {name} | {islands} | {took:.3f} | {cpu:.3f} | {peak_mb(binary, args, day, islands):.1f} '
                  f'| {decisions:,} | {conflicts:,} | {bounded:,} | {stops:,} '
                  f'| {decisions / took:,.0f} | {took / base:.1f} | {forks:,} | {nodes:,} |', flush=True)
    print()
    if o.best:
        print(f'{first[0]:,} decisions, {first[1]:,} of them conflicts, with one search.')
    else:
        print(f'{first[0]:,} decisions, {first[1]:,} of them conflicts, every way.')

main()
