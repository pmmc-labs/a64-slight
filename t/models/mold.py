#!/usr/bin/env python3
"""examples/mold.expected, and days for examples/mold.slight.

    python3 t/models/mold.py solve examples/mold-day.sexp
    python3 t/models/mold.py best examples/mold-day.sexp
    python3 t/models/mold.py fly examples/mold-oc.ssim
    python3 t/models/mold.py first examples/mold-oc.ssim
    python3 t/models/mold.py gen SEED AIRCRAFT LEGS [SLACK] > day.sexp
    python3 t/models/mold.py check day.sexp

A day is aircraft, each with its type, the airport it starts at, the
minutes it may fly before maintenance and what a minute of it costs, and
legs, each of a type, from an airport to another, leaving and landing at
a minute. An assignment gives every leg an aircraft of its type; it's
valid if each aircraft can fly its legs in order, each leaving from
where the last landed (or where the aircraft started) no sooner than its
type's turn after it, and their minutes together are within the
aircraft's.

A day is a .sexp file, as gen makes one, all of one type; or an SSIM
file, an airline's schedule as airlines exchange them, whose legs say
which flight their aircraft flies next. From those, the lines the
schedule plans: a line goes on to the next flight if that leaves after
this one lands, and ends for the night if not. There's an aircraft for
each line, starting where the line does; a type's turn is the shortest
its lines plan; and times are minutes of the day in UTC, a landing after
midnight past 1440. An SSIM file has no maintenance in it, nor costs: an
aircraft may fly any number of minutes, and a minute costs 1.

solve counts the valid assignments, and finds the least cost, as a
schedule is flown: the legs in the order they leave, each tried on every
aircraft that's there and free, the aircraft's place, time and minutes
kept as it goes. That's not how examples/mold.slight looks for them (it
picks each leg's predecessor, and propagates), so the two agreeing says
something. check counts them a third way, every way of giving legs to
aircraft tried whole, for a day small enough. best finds only the least
cost, as examples/mold.slight --best does, the same way as solve but
leaving a schedule once what it has cost, and the rest at the cheapest
aircraft's rate, is no less than the best found: a bound of its own,
weaker than the program's, so the two agree only if both are sound.

fly flies the day as planned, each aircraft its line, and says what each
did, as examples/mold.slight --fly does with its simulation. first says
what a day that flies comes to, its legs and their minutes, which are
the same whoever flies them: the plan is one, if there's a plan, and
solve's count says whether there's one if not.
"""
import sys
# Not this directory's modules for the library's: where math isn't built
# into Python (Homebrew's, say), random would get t/models/math.py.
sys.path = sys.path[1:]
import itertools, random

FOREVER = 100000

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

def read_sexp(path):
    toks, recs, cur = tokens(open(path).read()), [], None
    for t in toks:
        if t == '(': cur = []
        elif t == ')': recs.append(cur); cur = None
        else: cur.append(t[1:-1] if t[0] == '"' else int(t) if t.lstrip('-').isdigit() else t)
    turns = {"": [r[1] for r in recs if r[0] == ':turn'][0]}
    aircraft = [(r[1], "", r[2], r[3], r[4]) for r in recs if r[0] == ':aircraft']
    legs = [(r[1], r[2], r[3], r[4], r[5], "") for r in recs if r[0] == ':leg']
    return turns, aircraft, legs, None

def read_ssim(path):
    """An SSIM file's type 3 records, one a leg: the columns are the
    standard's (chapter 7), counted here from 0."""
    def minutes(hhmm, offset, days):
        local = int(hhmm[:2]) * 60 + int(hhmm[2:])
        ahead = (int(offset[1:3]) * 60 + int(offset[3:])) * (-1 if offset[0] == '-' else 1)
        return local - ahead + 1440 * int(days)
    legs, onward = [], {}
    for line in open(path).read().split('\n'):
        if line[:1] != '3': continue
        flight = line[2:5].strip() + line[5:9]
        legs.append((flight, line[36:39], line[54:57],
                     minutes(line[43:47], line[47:52], line[192]),
                     minutes(line[57:61], line[65:70], line[193]), line[72:75]))
        onward[flight] = line[137:140].strip() + line[140:144]
    by = {l[0]: l for l in legs}
    after = {f: g for f, g in onward.items() if g in by and by[g][3] >= by[f][4]}
    follows = set(after.values())
    turns, aircraft, plan = {}, [], {}
    for f, g in after.items():
        kind = by[f][5]
        turns[kind] = min(turns.get(kind, FOREVER), by[g][3] - by[f][4])
    for l in legs:
        if l[0] in follows: continue
        tail = f"{l[5]}-{len(aircraft) + 1:02d}"
        aircraft.append((tail, l[5], l[1], FOREVER, 1))
        f = l[0]
        while f is not None:
            plan[f] = tail; f = after.get(f)
    for kind in {l[5] for l in legs}: turns.setdefault(kind, 0)
    return turns, aircraft, legs, plan

def read(path):
    """(turns, aircraft, legs, plan): a turn for each type; aircraft
    (tail, type, start, limit, rate); legs (flight, from, to, dep, arr,
    type) in the order they leave; and each flight's tail as planned,
    or None."""
    turns, aircraft, legs, plan = (read_sexp if path.endswith('.sexp') else read_ssim)(path)
    return turns, aircraft, sorted(legs, key=lambda l: l[3]), plan

def solve(turns, aircraft, legs, bound=False):
    at = [a[2] for a in aircraft]; free = [None] * len(aircraft); used = [0] * len(aircraft)
    count, best = 0, None
    least = min(a[4] for a in aircraft)
    rest = [sum(l[4] - l[3] for l in legs[i:]) for i in range(len(legs) + 1)]
    def go(i, cost):
        nonlocal count, best
        if bound and best is not None and cost + rest[i] * least >= best:
            return
        if i == len(legs):
            count += 1
            best = cost if best is None else min(best, cost)
            return
        _, frm, to, dep, arr, kind = legs[i]
        for a, (_, mine, _, limit, rate) in enumerate(aircraft):
            if at[a] != frm or mine != kind: continue
            if free[a] is not None and free[a] + turns[kind] > dep: continue
            if used[a] + (arr - dep) > limit: continue
            was = at[a], free[a], used[a]
            at[a], free[a], used[a] = to, arr, used[a] + (arr - dep)
            go(i + 1, cost + (arr - dep) * rate)
            at[a], free[a], used[a] = was
    go(0, 0)
    return count, best

def check(turns, aircraft, legs):
    count, best = 0, None
    for tails in itertools.product(range(len(aircraft)), repeat=len(legs)):
        ok, cost = True, 0
        for a, (_, kind, start, limit, rate) in enumerate(aircraft):
            mine = [l for l, t in zip(legs, tails) if t == a]
            where, when, used = start, None, 0
            for _, frm, to, dep, arr, wants in mine:
                if frm != where or wants != kind or (when is not None and when + turns[kind] > dep): ok = False; break
                where, when, used = to, arr, used + (arr - dep)
            if not ok or used > limit: ok = False; break
            cost += used * rate
        if ok:
            count += 1
            best = cost if best is None else min(best, cost)
    return count, best

def fly(turns, aircraft, legs, plan):
    """The day as planned, a leg at a time in the order they leave: what
    each aircraft did, (tail, start, end, legs, minutes), or the flight
    the plan stops at."""
    did = {a[0]: [a[2], None, 0, 0] for a in aircraft}
    facts = {a[0]: a for a in aircraft}
    for flight, frm, to, dep, arr, kind in legs:
        tail = plan[flight]; where, when, n, used = did[tail]
        _, mine, _, limit, _ = facts[tail]
        if (where != frm or mine != kind or (when is not None and when + turns[kind] > dep)
                or used + (arr - dep) > limit):
            return flight
        did[tail] = [to, arr, n + 1, used + (arr - dep)]
    return [(a[0], a[2], did[a[0]][0], did[a[0]][2], did[a[0]][3]) for a in aircraft]

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
    elif argv[:1] in (["solve"], ["check"], ["best"], ["fly"], ["first"]) and len(argv) == 2:
        turns, aircraft, legs, plan = read(argv[1])
        print(f"{len(legs)} legs, {len(aircraft)} aircraft")
        whole = f"{len(legs)} legs, {sum(l[4] - l[3] for l in legs)} minutes"
        if argv[0] == "fly":
            did = fly(turns, aircraft, legs, plan)
            if isinstance(did, str):
                print(f"the plan stops at {did}")
            else:
                for tail, start, end, n, used in did:
                    print(f"{tail} {start} to {end}: {n} legs, {used} minutes")
                print(f"the plan flies: {whole}")
        elif argv[0] == "first":
            flies = (not isinstance(fly(turns, aircraft, legs, plan), str)) if plan else solve(turns, aircraft, legs)[0] > 0
            print(f"a day that flies: {whole}" if flies else "no day flies")
        else:
            count, best = (check(turns, aircraft, legs) if argv[0] == "check" else
                           solve(turns, aircraft, legs, argv[0] == "best"))
            if argv[0] != "best": print(f"{count} assignments")
            print(f"best {best}" if count else "no best")
    else:
        sys.exit(__doc__)

main(sys.argv[1:])
