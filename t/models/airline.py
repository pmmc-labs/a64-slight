# The expected output of examples/airline.slight, worked out from the
# example's rules (its header says them), independently of it:
#
#     python3 t/models/airline.py > examples/airline.expected
#
# A day of a small airline, from examples/airline-day.sexp: the base, then
# each branch, run with its change and those of its ancestors, compared
# minute by minute with each ancestor, oldest first, from the end of the
# changes between them, and stopped at the first match. Everything is an
# integer: minutes, kilograms, passengers. The branches, the what-ifs, are
# in the file too.
#
# The program starts a branch from a checkpoint; this model runs each from
# the start of the day, which is what a checkpoint must come to, and works
# out only where the program would have begun, for the minutes it says it
# ran: the last checkpoint at or before the change that holds for the
# parent (the parent's own, kept every half hour while it ran; its
# parent's from before it began; or, once it had rejoined, the run it
# rejoined's from after).
#
# With --record it prints the (:flew flight off on) lines the base gives,
# which is where the data file's record came from: a made-up day has no
# record of its own, so its record is the model's. (A real one would be
# calibrated against the record until it agreed.)
import sys

TURN, CONNECT, START, END = 40, 30, 360, 1440
EVERY = 30
FUEL_OFF, FUEL_MINUTE = 300, 40

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
        self.branches = [(r[1], r[2], r[3]) for r in records if r[0] == 'branch']


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
    if kind == 'cancel':
        dep = day.legs[change[1]]['dep']
        return dep, dep + 1
    if kind == 'swap':
        return change[3], change[3] + 1
    raise ValueError(change)


def run(day, changes, targets):
    """The day, with changes, minute by minute from START: (digests,
    totals, flown, stop, rejoined, cancelled): the digests and the totals
    so far, by minute up to stop; when each flight left and landed, and
    which aircraft flew it, (off, on, tail); the name of the target matched
    at stop, or None if it ran to the end; and when each cancelled flight
    was cancelled. targets is a list of (name, digests, from), oldest
    first."""
    closed = list(day.closed) + [(c[1], c[2], c[3]) for c in changes if c[0] == 'close']
    delay = {c[1]: c[2] for c in changes if c[0] == 'delay'}
    oos = {c[1]: c[2] for c in changes if c[0] == 'out-of-service'}
    quick = [(c[1], c[2], c[3], c[4]) for c in changes if c[0] == 'quick-turn']
    cancel = {c[1] for c in changes if c[0] == 'cancel'}
    swaps = [(c[1], c[2], c[3]) for c in changes if c[0] == 'swap']

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
    cancelled_at = {}
    digests, by_minute = [], []
    for t in range(START, END):
        # a swap: at its minute, before the aircraft hear of it, the two
        # exchange the legs they have ahead
        for a, b, m in swaps:
            if m == t:
                planes[a][1], planes[b][1] = planes[b][1], planes[a][1]
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
            # cancelled at its departure: a flight cancelled by a change,
            # wherever it is among the legs ahead; and the next, while it
            # leaves from somewhere the aircraft isn't, or the aircraft is
            # withdrawn
            cancels.extend(l['flight'] for l in legs if l['flight'] in cancel and l['dep'] <= t)
            legs = [l for l in legs if not (l['flight'] in cancel and l['dep'] <= t)]
            where = status[1]['to'] if status[0] == 'air' else status[1]
            while legs and legs[0]['dep'] <= t and (status[0] == 'withdrawn' or legs[0]['frm'] != where):
                cancels.append(legs[0]['flight'])
                legs = legs[1:]
            if status[0] == 'gate' and legs:
                leg = legs[0]
                if (t >= leg['dep'] and t >= status[2] and t >= leg['dep'] + delay.get(leg['flight'], 0)
                        and is_open(leg['frm'], t)):
                    departures.append((leg['flight'], t, tail))
                    status, legs = ('air', leg, t + leg['block'] + day.wind.get(leg['flight'], 0)), legs[1:]
                    totals['fuel'] += FUEL_OFF + FUEL_MINUTE
            planes[tail] = [status, legs]

        for flight, at in sorted(arrivals):
            leg = day.legs[flight]
            flown[flight] = (flown[flight][0], at, flown[flight][2])
            totals['late'] += max(0, at - (leg['dep'] + leg['block'])) * leg['pax']
            for i, o, n in day.connects:
                if i == flight and o not in gone:
                    pending[(i, o)] = (n, at + CONNECT)
        for flight, at, tail in sorted(departures):
            flown[flight] = (at, None, tail)
            gone.add(flight)
            for i, o, n in day.connects:
                if o == flight:
                    p = pending.pop((i, o), None)
                    if p is None or p[1] > at:
                        totals['missed'] += n
        for flight in sorted(cancels):
            gone.add(flight)
            cancelled_at[flight] = t
            totals['cancelled'] += day.legs[flight]['pax']
            for i, o, n in day.connects:
                if o == flight:
                    pending.pop((i, o), None)

        state = []
        for tail in sorted(planes):
            status, legs = planes[tail]
            # the next leg says what the aircraft has ahead: the rest of
            # the legs it was planned with, which a swap moves together
            nxt = legs[0]['flight'] if legs else None
            if status[0] == 'gate':
                state.append(('gate', tail, status[1], nxt, max(0, status[2] - t)))
            elif status[0] == 'air':
                state.append(('air', tail, status[1]['flight'], nxt, max(0, status[2] - t)))
            else:
                state.append(('withdrawn', tail, status[1], nxt))
        digest = (tuple(state), tuple(sorted((i, o, n, max(0, r - t)) for (i, o), (n, r) in pending.items())))
        digests.append(digest)
        by_minute.append(dict(totals))
        for name, theirs, frm in targets:
            if t >= frm and theirs[t - START] == digest:
                return digests, by_minute, flown, t, name, cancelled_at
    return digests, by_minute, flown, END - 1, None, cancelled_at


KEYS = ['fuel', 'late', 'missed', 'cancelled']


def kept(begun, stop):
    """The checkpoints a run keeps: before minute m, once it has settled
    m - 1 and gone on."""
    return [m for m in range(begun + 1, stop + 1) if (m - START) % EVERY == 0 and m < END]


class Explorer:
    """The runs kept, each with its changes, its parent and its own change,
    and its whole day: digests, totals by minute, the legs flown and those
    cancelled, a rejoined run's from where it rejoined being the run it
    rejoined's (its totals moved by what it had changed); and where it
    began, stopped and rejoined, and its checkpoints."""

    def __init__(self, day):
        self.day = day
        digests, by_minute, flown, _, _, cancelled = run(day, [], [])
        self.runs = {'base': dict(changes=[], parent=None, change=None, digests=digests, totals=by_minute,
                                  flown=flown, cancelled=cancelled, begun=START, stop=END - 1, rejoined=None,
                                  checkpoints=kept(START, END - 1))}

    def state_at(self, name, c):
        r = self.runs[name]
        found = [m for m in r['checkpoints'] if m <= c]
        if r['parent'] is not None:
            found.append(self.state_at(r['parent'], min(c, r['begun'])))
        if r['rejoined'] is not None and c > r['stop']:
            m = self.state_at(r['rejoined'], c)
            if m is not None and m > r['stop']:
                found.append(m)
        found = [m for m in found if m is not None]
        return max(found) if found else None

    def branch(self, name, parent, change):
        """Runs a branch and keeps it; gives (same, frm, stop, rejoined,
        begun) for its report."""
        day, runs = self.day, self.runs
        path = [parent]
        while runs[path[-1]]['parent'] is not None:
            path.append(runs[path[-1]]['parent'])
        ancestors = list(reversed(path))            # oldest first
        frm, to = window(change, day)
        targets = []
        for a in ancestors:
            # the changes between a and this branch: those of the runs after
            # a on the path, and this one's
            between = [runs[b]['change'] for b in ancestors[ancestors.index(a) + 1:]] + [change]
            targets.append((a, runs[a]['digests'], max(window(c, day)[1] for c in between)))
        changes = runs[parent]['changes'] + [change]
        mine, totals, flown, stop, rejoined, cancelled = run(day, changes, targets)
        same = all(mine[t - START] == runs[parent]['digests'][t - START] for t in range(START, frm))
        begun = self.state_at(parent, frm) or START
        if rejoined is None:
            whole = dict(digests=mine, totals=totals, flown=flown, cancelled=cancelled)
        else:
            theirs = runs[rejoined]
            shift = {k: totals[-1][k] - theirs['totals'][stop - START][k] for k in KEYS}
            whole = dict(digests=mine + theirs['digests'][stop - START + 1:],
                         totals=totals + [{k: m[k] + shift[k] for k in KEYS} for m in theirs['totals'][stop - START + 1:]],
                         flown={}, cancelled={})
            # its legs till it stopped, and theirs after: one in the air
            # then lands when theirs does, the two being the same then
            for f in day.order:
                if f in cancelled:
                    whole['cancelled'][f] = cancelled[f]
                elif f in flown and flown[f][1] is not None:
                    whole['flown'][f] = flown[f]
                elif f in flown:
                    whole['flown'][f] = (flown[f][0], theirs['flown'][f][1], flown[f][2])
                elif f in theirs['cancelled']:
                    whole['cancelled'][f] = theirs['cancelled'][f]
                elif f in theirs['flown']:
                    whole['flown'][f] = theirs['flown'][f]
        runs[name] = dict(changes=changes, parent=parent, change=change, begun=begun, stop=stop, rejoined=rejoined,
                          checkpoints=kept(begun, stop), **whole)
        return same, frm, stop, rejoined, begun

    def against_base(self, name):
        final, base = self.runs[name]['totals'][-1], self.runs['base']['totals'][-1]
        return {k: final[k] - base[k] for k in KEYS}


def shown(change):
    args = ' '.join(f'"{x}"' if isinstance(x, str) else str(x) for x in change[1:])
    return f'({change[0]} {args})'


def main():
    day = Day(parse(open(sys.argv[sys.argv.index('--day') + 1] if '--day' in sys.argv else 'examples/airline-day.sexp').read()))
    ex = Explorer(day)
    base = ex.runs['base']
    if '--record' in sys.argv:
        for f in day.order:
            print(f'(:flew "{f}" {base["flown"][f][0]} {base["flown"][f][1]})')
        return
    if '--recover' in sys.argv:
        recover(ex, parse(open(sys.argv[sys.argv.index('--recover') + 1]).read()))
        return

    agree = sum(1 for f in day.order if f in base['flown'] and day.flew.get(f) == base['flown'][f][:2])
    print(f'the record: {len(day.order)} legs, {agree} of which the base flies as it says')
    base_final = base['totals'][-1]
    print(f'base: {clock(START)} to {clock(END)}')
    print(f"    fuel {base_final['fuel']} kg, {base_final['late']} passenger-minutes late, "
          f"{base_final['missed']} connections missed, {base_final['cancelled']} seats cancelled")

    history, simulated, own = [], 0, 0
    for name, parent, change in day.branches:
        same, frm, stop, rejoined, begun = ex.branch(name, parent, change)
        simulated += stop - begun + 1
        own += stop - frm + 1
        d = ex.against_base(name)
        print(f'{name}, from {parent}: {shown(change)}')
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
        history.append(f'(branch "{name}" (from "{parent}") (change {shown(change)}) '
                       f'(own "{clock(frm)}" "{until}") {end} '
                       f"(against-base (fuel {d['fuel']}) (late {d['late']}) (missed {d['missed']}) (cancelled {d['cancelled']})))")
    print('the history:')
    for h in history:
        print(h)
    print(f'"{END - START} minutes for the base, and {simulated} for its branches, of which {own} their own"')


# --- recovering from a disruption --------------------------------------------
#
# The disruption is a branch of the base; each candidate remedy a branch of
# it, from when it's known. The shortlist is the candidates, doing nothing
# among them, that no other is better than in all of passenger-minutes late,
# connections missed and seats cancelled (the first of any with the same
# three); the services check those, and rank the ones that pass.

DUTY = 470                  # the crew service: the most minutes a crew is on duty
REPORT, RELEASE = 45, 15    # on duty before its first departure, and after its last landing
CREW_CONNECT = 30           # the least a crew has from landing to its next departure
COMPENSATED = 180           # the rank service: minutes late that a passenger is paid for


def operated(ex, name):
    """The day a run flew, as the services are sent it: for each leg, its
    flight, the aircraft that flew it, when it left and landed, and what
    became of it."""
    r = ex.runs[name]
    out = []
    for f in ex.day.order:
        if f in r['cancelled']:
            out.append((f, None, None, None, 'cancelled'))
        elif f in r['flown']:
            off, on, tail = r['flown'][f]
            out.append((f, tail, off, on, 'landed' if on is not None else 'in the air'))
        else:
            out.append((f, None, None, None, 'not flown'))
    return out


def crew_check(day, ops):
    """Each aircraft's first two legs are a crew's, and the rest another's,
    whichever aircraft flies them. A crew must be where its next leg leaves
    from, have 30 minutes from landing to leaving, and be on duty no more
    than DUTY minutes, from 45 before its first departure to 15 after its
    last landing."""
    by = {f: (tail, off, on, st) for f, tail, off, on, st in ops}
    reasons = []
    for tail, _ in day.aircraft:
        legs = [f for f in day.order if day.legs[f]['tail'] == tail]
        for pairing in (legs[:2], legs[2:]):
            flew = [f for f in pairing if by[f][3] in ('landed', 'in the air')]
            if not flew:
                continue
            for a, b in zip(flew, flew[1:]):
                if day.legs[b]['frm'] != day.legs[a]['to']:
                    reasons.append((b, f"its crew is at {day.legs[a]['to']}, not {day.legs[b]['frm']}"))
                elif by[b][1] - (by[a][2] if by[a][2] is not None else END) < CREW_CONNECT:
                    gap = by[b][1] - (by[a][2] if by[a][2] is not None else END)
                    reasons.append((b, f'its crew has {gap} minutes after {a}, under {CREW_CONNECT}'))
            last_on = by[flew[-1]][2] if by[flew[-1]][2] is not None else END
            duty = last_on + RELEASE - (by[flew[0]][1] - REPORT)
            if duty > DUTY:
                reasons.append((flew[-1], f'its crew is on duty {duty} minutes, over {DUTY}'))
    return reasons


def maintenance_check(day, ops):
    """Each aircraft must end the day where its last leg was to take it,
    where its check is tonight."""
    reasons = []
    for tail, home in day.aircraft:
        legs = [f for f in day.order if day.legs[f]['tail'] == tail]
        due = day.legs[legs[-1]]['to'] if legs else home
        flew = [(on if on is not None else END, f) for f, t, off, on, st in ops if t == tail and off is not None]
        ends = day.legs[max(flew)[1]]['to'] if flew else home
        if ends != due:
            reasons.append((tail, f'{tail} ends the day at {ends}; its check tonight is at {due}'))
    return reasons


def rank(day, ops):
    """What the day costs its passengers: refunds for legs that didn't fly
    (100 plus its block minutes a seat), 150 for each who misses a
    connection, 250 for each who lands COMPENSATED minutes late or more, and
    1 a passenger-minute late."""
    by = {f: (tail, off, on, st) for f, tail, off, on, st in ops}
    refunds = refunded = misconnected = compensated = late = 0
    for f in day.order:
        leg = day.legs[f]
        st, on = by[f][3], by[f][2]
        if st in ('cancelled', 'not flown'):
            refunds += leg['pax'] * (100 + leg['block'])
            refunded += leg['pax']
        elif on is not None:
            m = max(0, on - (leg['dep'] + leg['block']))
            late += m * leg['pax']
            if m >= COMPENSATED:
                compensated += leg['pax']
    for i, o, n in day.connects:
        if by[o][1] is not None and (by[i][2] is None or by[i][2] + CONNECT > by[o][1]):
            misconnected += n
    cost = refunds + 250 * compensated + 150 * misconnected + late
    return dict(cost=cost, disrupted=refunded + misconnected + compensated, refunds=refunds,
                compensated=compensated, misconnected=misconnected, late=late)


def candidates(ex, d, now, tries):
    """The remedies to try, in order: those asked for; holds, each flight
    with passengers connecting to it, leaving after now, held 15, 30 and 45
    minutes; cancellations, each flight leaving after now; and swaps, each
    two aircraft at the first minute after now they're both on the ground
    at one airport, if either has a leg ahead then. A remedy already there
    isn't there twice."""
    day, r = ex.day, ex.runs[d]
    out = list(tries)
    alive = [f for f in day.order if day.legs[f]['dep'] >= now and f not in r['cancelled']]
    for f in alive:
        if any(o == f for _, o, _ in day.connects):
            out += [['delay', f, m] for m in (15, 30, 45)]
    out += [['cancel', f] for f in alive]

    # When each aircraft is on the ground at the start of a minute, and
    # where: before its first departure, at its base; between a landing
    # and the next departure; after its last landing, to the end.
    def ground(tail, home):
        legs = sorted((v[0], v[1], f) for f, v in r['flown'].items() if v[2] == tail)
        spans, at, since = [], home, START
        for off, on, f in legs:
            spans.append((since, off, at))
            if on is None:
                return spans
            at, since = day.legs[f]['to'], on + 1
        spans.append((since, END - 1, at))
        return spans

    aircraft = day.aircraft
    for x in range(len(aircraft)):
        for y in range(x + 1, len(aircraft)):
            (a, ha), (b, hb) = aircraft[x], aircraft[y]
            starts = [max(now, s1, s2) for s1, e1, p1 in ground(a, ha) for s2, e2, p2 in ground(b, hb)
                      if p1 == p2 and max(now, s1, s2) <= min(e1, e2)]
            if not starts:
                continue
            m = min(starts)
            ahead = [f for f, v in r['flown'].items() if v[2] in (a, b) and v[0] >= m]
            if ahead:
                out.append(['swap', a, b, m])
    seen, unique = [], []
    for c in out:
        if c not in seen:
            seen.append(c)
            unique.append(c)
    return unique


def recover(ex, records):
    day = ex.day
    _, dname, dchange = next(r for r in records if r[0] == 'disruption')
    tries = [r[1] for r in records if r[0] == 'try']
    top = next((r[1] for r in records if r[0] == 'top'), 3)
    budget = next((r[1] for r in records if r[0] == 'check'), 12)
    ex.branch(dname, 'base', dchange)
    now = window(dchange, day)[0]
    d = ex.against_base(dname)
    print(f'the disruption, {dname}: {shown(dchange)}, from {clock(now)}')
    print(f"    against base: fuel {signed(d['fuel'])} kg, {signed(d['late'])} passenger-minutes late, "
          f"{signed(d['missed'])} missed, {signed(d['cancelled'])} cancelled")

    cands = candidates(ex, dname, now, tries)
    names = [('c0', None)]
    for i, c in enumerate(cands, 1):
        ex.branch(f'c{i}', dname, c)
        names.append((f'c{i}', c))
    run_of = {n: (dname if c is None else n) for n, c in names}
    label = {n: 'nothing' if c is None else shown(c) for n, c in names}
    measure = {n: tuple(ex.against_base(run_of[n])[k] for k in ('late', 'missed', 'cancelled')) for n, _ in names}
    rejoined = sum(1 for n, c in names if c is not None and ex.runs[n]['rejoined'] is not None)
    print(f'the candidates: {len(cands)} remedies, and nothing; {rejoined} of the remedies rejoin a run before them')

    # Rounds: the candidates no other betters in all three together, checked,
    # then those of the rest, till as many as the budget have been checked.
    rest, passed, checked, round_ = [n for n, _ in names], [], 0, 0
    while rest and checked < budget:
        round_ += 1
        front = [n for n in rest
                 if not any(measure[o] != measure[n] and all(measure[o][k] <= measure[n][k] for k in range(3)) for o in rest)]
        print(f'round {round_}: the {len(front)} candidates no other left betters in late, missed and cancelled together')
        for n in front:
            late, missed, cancelled = measure[n]
            print(f'    {n} {label[n]}: late {signed(late)}, missed {signed(missed)}, cancelled {signed(cancelled)}')
            ops = operated(ex, run_of[n])
            reasons = [('crew', w, y) for w, y in crew_check(day, ops)] + [('maintenance', w, y) for w, y in maintenance_check(day, ops)]
            for service, what, why in reasons:
                print(f'        {service}: {what}: {why}')
            if not reasons:
                print('        passes')
                passed.append(n)
        checked += len(front)
        rest = [n for n in rest if n not in front]

    print('ranked by the ranking service, of those that pass:')
    scored = sorted(((rank(day, operated(ex, run_of[n])), i, n) for i, n in enumerate(passed)), key=lambda x: (x[0]['cost'], x[1]))
    for place, (s, _, n) in enumerate(scored[:top], 1):
        print(f"    {place}. {n} {label[n]}: cost {s['cost']}: {s['disrupted']} passengers disrupted, "
              f"{s['refunds']} refunded, {s['compensated']} compensated, {s['misconnected']} misconnected, "
              f"{s['late']} passenger-minutes late")
    best = scored[0][2] if scored else 'none'
    print(f'"{len(cands)} remedies, {checked} checked, {len(passed)} that pass, and the best {best}"')


if __name__ == '__main__':
    main()
