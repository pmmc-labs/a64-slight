# The expected output of examples/airline.slight, worked out from the
# example's rules (its header says them), independently of it:
#
#     python3 t/models/airline.py > examples/airline.expected
#
# A day of a small airline, from examples/airline-day.sexp: the base, then
# each branch, rerun from the start with its change and those of its
# ancestors, compared minute by minute with each ancestor, oldest first,
# from the end of the changes between them, and stopped at the first
# match. Everything is an integer: minutes, kilograms, passengers.
#
# With --record it prints the (:flew flight off on) lines the base gives,
# which is where the data file's record came from: a made-up day has no
# record of its own, so its record is the model's. (A real one would be
# calibrated against the record until it agreed.)
import sys

TURN, CONNECT, START, END = 40, 30, 360, 1440
FUEL_OFF, FUEL_MINUTE = 300, 40

BRANCHES = [
    ('b1', 'base', ['delay', 'SL201', 55]),
    ('b2', 'base', ['close', 'ORD', 690, 840]),
    ('b3', 'base', ['out-of-service', 'N104', 600]),
    ('b4', 'b2', ['quick-turn', 'ORD', 780, 900, 20]),
]


def parse(text):
    """The s-expressions in text, as lists, strings and ints; a keyword is
    its name, without the colon."""
    tokens, i = [], 0
    while i < len(text):
        c = text[i]
        if c == ';':
            while i < len(text) and text[i] != '\n':
                i += 1
        elif c in ' \t\r\n':
            i += 1
        elif c in '()':
            tokens.append(c)
            i += 1
        elif c == '"':
            j = text.index('"', i + 1)
            tokens.append(('str', text[i + 1:j]))
            i = j + 1
        else:
            j = i
            while j < len(text) and text[j] not in ' \t\r\n();"':
                j += 1
            word = text[i:j]
            tokens.append(int(word) if word.lstrip('-').isdigit() else word.lstrip(':'))
            i = j
    stack = [[]]
    for t in tokens:
        if t == '(':
            stack.append([])
        elif t == ')':
            done = stack.pop()
            stack[-1].append(done)
        else:
            stack[-1].append(t[1] if isinstance(t, tuple) else t)
    return stack[0]


def clock(m):
    return f'{m // 60:02d}:{m % 60:02d}'


def signed(n):
    return f'+{n}' if n >= 0 else str(n)


class Day:
    def __init__(self, records):
        self.aircraft = [(r[1], r[2]) for r in records if r[0] == 'aircraft']
        self.legs = {r[1]: dict(flight=r[1], tail=r[2], frm=r[3], to=r[4], dep=r[5], block=r[6], pax=r[7])
                     for r in records if r[0] == 'leg'}
        self.order = [r[1] for r in records if r[0] == 'leg']
        self.connects = [(r[1], r[2], r[3]) for r in records if r[0] == 'connect']
        self.wind = {r[1]: r[2] for r in records if r[0] == 'wind'}
        self.closed = [(r[1], r[2], r[3]) for r in records if r[0] == 'closed']
        self.flew = {r[1]: (r[2], r[3]) for r in records if r[0] == 'flew'}


def window(change, day):
    """The minutes in which a change makes the inputs differ: [from, to)."""
    kind = change[0]
    if kind == 'delay':
        dep = day.legs[change[1]]['dep']
        return dep, dep + change[2]
    if kind == 'close':
        return change[2], change[3]
    if kind == 'out-of-service':
        return change[2], END
    if kind == 'quick-turn':
        return change[2], change[3]
    raise ValueError(change)


def run(day, changes, targets):
    """The day, with changes, minute by minute from START: (digests,
    totals, flown, stop, rejoined): the digests and the totals so far, by
    minute up to stop; when each flight left and landed; and the name of
    the target matched at stop, or None if it ran to the end. targets is
    a list of (name, digests, from), oldest first."""
    closed = list(day.closed) + [(c[1], c[2], c[3]) for c in changes if c[0] == 'close']
    delay = {c[1]: c[2] for c in changes if c[0] == 'delay'}
    oos = {c[1]: c[2] for c in changes if c[0] == 'out-of-service'}
    quick = [(c[1], c[2], c[3], c[4]) for c in changes if c[0] == 'quick-turn']

    def is_open(airport, t):
        return not any(a == airport and f <= t < u for a, f, u in closed)

    def turn(airport, t):
        for a, f, u, m in quick:
            if a == airport and f <= t < u:
                return m
        return TURN

    # each aircraft is [status, legs]: status ('gate', airport, ready),
    # ('air', leg, due) or ('withdrawn', airport); legs, those still ahead
    planes = {}
    for tail, base in day.aircraft:
        planes[tail] = [('gate', base, 0), [day.legs[f] for f in day.order if day.legs[f]['tail'] == tail]]

    totals = dict(fuel=0, late=0, missed=0, cancelled=0)
    pending = {}                        # (in, out) -> (passengers, ready), once in has landed
    gone = set()                        # flights that have left or been cancelled
    flown = {}
    digests, by_minute = [], []
    for t in range(START, END):
        arrivals, departures, cancels = [], [], []
        for tail in sorted(planes):
            status, legs = planes[tail]
            if status[0] == 'air':
                leg, due = status[1], status[2]
                if t >= due and is_open(leg['to'], t):
                    arrivals.append((leg['flight'], t))
                    status = ('gate', leg['to'], t + turn(leg['to'], t))
                else:
                    totals['fuel'] += FUEL_MINUTE
            if status[0] == 'gate' and tail in oos and t >= oos[tail]:
                cancels.extend(l['flight'] for l in legs)
                status, legs = ('withdrawn', status[1]), []
            if status[0] == 'gate' and legs:
                leg = legs[0]
                if (t >= leg['dep'] and t >= status[2] and t >= leg['dep'] + delay.get(leg['flight'], 0)
                        and is_open(leg['frm'], t)):
                    departures.append((leg['flight'], t))
                    status, legs = ('air', leg, t + leg['block'] + day.wind.get(leg['flight'], 0)), legs[1:]
                    totals['fuel'] += FUEL_OFF + FUEL_MINUTE
            planes[tail] = [status, legs]

        for flight, at in sorted(arrivals):
            leg = day.legs[flight]
            flown[flight] = (flown[flight][0], at)
            totals['late'] += max(0, at - (leg['dep'] + leg['block'])) * leg['pax']
            for i, o, n in day.connects:
                if i == flight and o not in gone:
                    pending[(i, o)] = (n, at + CONNECT)
        for flight, at in sorted(departures):
            flown[flight] = (at, None)
            gone.add(flight)
            for i, o, n in day.connects:
                if o == flight:
                    p = pending.pop((i, o), None)
                    if p is None or p[1] > at:
                        totals['missed'] += n
        for flight in sorted(cancels):
            gone.add(flight)
            totals['cancelled'] += day.legs[flight]['pax']
            for i, o, n in day.connects:
                if o == flight:
                    pending.pop((i, o), None)

        state = []
        for tail in sorted(planes):
            status, legs = planes[tail]
            if status[0] == 'gate':
                state.append(('gate', tail, status[1], legs[0]['flight'] if legs else None, max(0, status[2] - t)))
            elif status[0] == 'air':
                state.append(('air', tail, status[1]['flight'], max(0, status[2] - t)))
            else:
                state.append(('withdrawn', tail, status[1]))
        digest = (tuple(state), tuple(sorted((i, o, n, max(0, r - t)) for (i, o), (n, r) in pending.items())))
        digests.append(digest)
        by_minute.append(dict(totals))
        for name, theirs, frm in targets:
            if t >= frm and theirs[t - START] == digest:
                return digests, by_minute, flown, t, name
    return digests, by_minute, flown, END - 1, None


def main():
    day = Day(parse(open('examples/airline-day.sexp').read()))
    digests, by_minute, flown, _, _ = run(day, [], [])
    if '--record' in sys.argv:
        for f in day.order:
            print(f'(:flew "{f}" {flown[f][0]} {flown[f][1]})')
        return

    agree = sum(1 for f in day.order if day.flew.get(f) == flown.get(f))
    print(f'the record: {len(day.order)} legs, {agree} of which the base flies as it says')
    keys = ['fuel', 'late', 'missed', 'cancelled']
    base_final = by_minute[-1]
    print(f'base: {clock(START)} to {clock(END)}')
    print(f"    fuel {base_final['fuel']} kg, {base_final['late']} passenger-minutes late, "
          f"{base_final['missed']} connections missed, {base_final['cancelled']} seats cancelled")

    # each run: its changes (its ancestors' and its own), and its digests
    # and totals for the whole day, a rejoined one's from where it rejoined
    # being its ancestor's (the totals moved by what it had changed)
    runs = {'base': dict(changes=[], parent=None, digests=digests, totals=by_minute)}
    history, simulated, own = [], 0, 0
    for name, parent, change in BRANCHES:
        path = [parent]
        while runs[path[-1]]['parent'] is not None:
            path.append(runs[path[-1]]['parent'])
        ancestors = list(reversed(path))            # oldest first
        frm, to = window(change, day)
        targets = []
        for a in ancestors:
            # the changes between a and this branch: those of the runs after
            # a on the path, and this one's
            between = [b for b in BRANCHES if b[0] in ancestors[ancestors.index(a) + 1:]] + [(name, parent, change)]
            targets.append((a, runs[a]['digests'], max(window(b[2], day)[1] for b in between)))
        changes = runs[parent]['changes'] + [change]
        mine, totals, _, stop, rejoined = run(day, changes, targets)
        same = all(mine[t - START] == runs[parent]['digests'][t - START] for t in range(START, frm))
        simulated += stop - START + 1
        own += stop - frm + 1

        if rejoined is None:
            full_digests, full_totals = mine, totals
        else:
            theirs = runs[rejoined]
            shift = {k: totals[-1][k] - theirs['totals'][stop - START][k] for k in keys}
            full_digests = mine + theirs['digests'][stop - START + 1:]
            full_totals = totals + [{k: m[k] + shift[k] for k in keys} for m in theirs['totals'][stop - START + 1:]]
        runs[name] = dict(changes=changes, parent=parent, digests=full_digests, totals=full_totals)

        final = full_totals[-1]
        d = {k: final[k] - base_final[k] for k in keys}
        shown = ' '.join(f'"{x}"' if isinstance(x, str) else str(x) for x in change[1:])
        print(f'{name}, from {parent}: ({change[0]} {shown})')
        if not same:
            print(f'    not the same as {parent} before {clock(frm)}')
        if rejoined is None:
            print(f'    the same as {parent} till {clock(frm)}, its own to the end of the day')
        else:
            print(f'    the same as {parent} till {clock(frm)}, its own till {clock(stop)}, then {rejoined}\'s again')
        print(f"    against base: fuel {signed(d['fuel'])} kg, {signed(d['late'])} passenger-minutes late, "
              f"{signed(d['missed'])} missed, {signed(d['cancelled'])} cancelled")
        end = '(to-the-end)' if rejoined is None else f'(rejoined "{rejoined}")'
        until = clock(END) if rejoined is None else clock(stop)
        history.append(f'(branch "{name}" (from "{parent}") (change ({change[0]} {shown})) '
                       f'(own "{clock(frm)}" "{until}") {end} '
                       f"(against-base (fuel {d['fuel']}) (late {d['late']}) (missed {d['missed']}) (cancelled {d['cancelled']})))")
    print('the history:')
    for h in history:
        print(h)
    print(f'"{END - START} minutes for the base, and {simulated} for its branches, of which {own} their own"')


if __name__ == '__main__':
    main()
