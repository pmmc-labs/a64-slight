#!/usr/bin/env python3
"""examples/mold.expected, and days for examples/mold.slight.

    python3 t/models/mold.py solve examples/mold-day.sexp
    python3 t/models/mold.py gen SEED AIRCRAFT LEGS [SLACK] > day.sexp
    python3 t/models/mold.py check day.sexp

A day is aircraft, each with the airport it starts at, the minutes it
may fly before maintenance and what a minute of it costs, and legs, each
from an airport to another, leaving and landing at a minute. An
assignment gives every leg an aircraft; it's valid if each aircraft can
fly its legs in order, each leaving from where the last landed (or where
the aircraft started) no sooner than the turn after it, and their
minutes together are within the aircraft's.

solve counts the valid assignments, and finds the least cost, as a
schedule is flown: the legs in the order they leave, each tried on every
aircraft that's there and free, the aircraft's place, time and minutes
kept as it goes. That's not how examples/mold.slight looks for them (it
picks each leg's predecessor, and propagates), so the two agreeing says
something. check counts them a third way, every way of giving legs to
aircraft tried whole, for a day small enough.
"""
import sys
# Not this directory's modules for the library's: where math isn't built
# into Python (Homebrew's, say), random would get t/models/math.py.
sys.path = sys.path[1:]
import itertools, random

def tokens(text):
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c == ';':
            while i < len(text) and text[i] != '\n': i += 1
        elif c in '()':
            out.append(c); i += 1
        elif c == '"':
            j = text.index('"', i + 1); out.append(text[i:j + 1]); i = j + 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in '()': j += 1
            out.append(text[i:j]); i = j
    return out

def read(path):
    """(turn, aircraft, legs): aircraft (tail, start, limit, rate), legs
    (flight, from, to, dep, arr) in the order they leave."""
    toks, recs, cur = tokens(open(path).read()), [], None
    for t in toks:
        if t == '(': cur = []
        elif t == ')': recs.append(cur); cur = None
        else: cur.append(t[1:-1] if t[0] == '"' else int(t) if t.lstrip('-').isdigit() else t)
    turn = [r[1] for r in recs if r[0] == ':turn'][0]
    aircraft = [tuple(r[1:]) for r in recs if r[0] == ':aircraft']
    legs = sorted((tuple(r[1:]) for r in recs if r[0] == ':leg'), key=lambda l: l[3])
    return turn, aircraft, legs

def solve(turn, aircraft, legs):
    at = [a[1] for a in aircraft]; free = [None] * len(aircraft); used = [0] * len(aircraft)
    count, best = 0, None
    def go(i, cost):
        nonlocal count, best
        if i == len(legs):
            count += 1
            best = cost if best is None else min(best, cost)
            return
        _, frm, to, dep, arr = legs[i]
        for a, (_, _, limit, rate) in enumerate(aircraft):
            if at[a] != frm: continue
            if free[a] is not None and free[a] + turn > dep: continue
            if used[a] + (arr - dep) > limit: continue
            was = at[a], free[a], used[a]
            at[a], free[a], used[a] = to, arr, used[a] + (arr - dep)
            go(i + 1, cost + (arr - dep) * rate)
            at[a], free[a], used[a] = was
    go(0, 0)
    return count, best

def check(turn, aircraft, legs):
    count, best = 0, None
    for tails in itertools.product(range(len(aircraft)), repeat=len(legs)):
        ok, cost = True, 0
        for a, (_, start, limit, rate) in enumerate(aircraft):
            mine = [l for l, t in zip(legs, tails) if t == a]
            where, when, used = start, None, 0
            for _, frm, to, dep, arr in mine:
                if frm != where or (when is not None and when + turn > dep): ok = False; break
                where, when, used = to, arr, used + (arr - dep)
            if not ok or used > limit: ok = False; break
            cost += used * rate
        if ok:
            count += 1
            best = cost if best is None else min(best, cost)
    return count, best

def gen(seed, m, n, slack):
    """A day with an answer: each aircraft flown from airport to airport,
    its legs kept, and its minutes given slack per cent over what it
    flew. Few airports, so many aircraft could take each leg."""
    r = random.Random(seed)
    airports, turn = ["ORD", "BOS", "DEN"], 30
    aircraft, legs, per = [], [], [n // m + (1 if a < n % m else 0) for a in range(m)]
    for a in range(m):
        start = where = r.choice(airports); when = 300 + r.randrange(0, 90, 5); used = 0
        for _ in range(per[a]):
            to = r.choice([p for p in airports if p != where])
            block = r.randrange(60, 150, 5)
            legs.append((where, to, when, when + block))
            where, when, used = to, when + block + turn + r.randrange(0, 60, 5), used + block
        aircraft.append((f"N{101 + a}", start, used * (100 + slack) // 100, r.randrange(30, 60, 5)))
    legs.sort(key=lambda l: (l[2], l[0], l[1]))
    out = ["; A day for examples/mold.slight, made by t/models/mold.py gen "
           f"{seed} {m} {n} {slack}.", "", "(:turn 30)", "",
           "; Each aircraft: its tail, where it starts, the minutes it may fly before",
           "; maintenance, and what a minute of it costs."]
    out += [f'(:aircraft "{t}" "{s}" {l} {c})' for t, s, l, c in aircraft]
    out += ["", "; Each leg: its flight, from, to, and the minutes it leaves and lands."]
    out += [f'(:leg "SL{201 + i}" "{f}" "{t}" {d} {a})' for i, (f, t, d, a) in enumerate(legs)]
    return "\n".join(out) + "\n"

def main(argv):
    if argv[:1] == ["gen"]:
        seed, m, n = map(int, argv[1:4])
        sys.stdout.write(gen(seed, m, n, int(argv[4]) if len(argv) > 4 else 25))
    elif argv[:1] in (["solve"], ["check"]) and len(argv) == 2:
        turn, aircraft, legs = read(argv[1])
        count, best = (solve if argv[0] == "solve" else check)(turn, aircraft, legs)
        print(f"{len(legs)} legs, {len(aircraft)} aircraft")
        print(f"{count} assignments")
        print(f"best {best}" if count else "no best")
    else:
        sys.exit(__doc__)

main(sys.argv[1:])
