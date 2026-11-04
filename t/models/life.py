#!/usr/bin/env python3
"""The Game of Life as ts-cpi's examples/life/ run it: a glider in the top
left and a blinker in the bottom right of an 8x8 torus, for 8 generations,
B3/S23. Every port in t/367 to t/374 must print these nine frames; each
port's own commentary is modelled here too, and the whole expected output
of each is written by this script: t/models/life.py writes t/367-*.expected
and the rest."""
import sys, os

W = H = 8
GENS = 8
SEED = [(0, 1), (1, 2), (2, 0), (2, 1), (2, 2), (6, 4), (6, 5), (6, 6)]
OFFSETS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]

def seed():
    return [[1 if (r, c) in SEED else 0 for c in range(W)] for r in range(H)]

def neighbors(r, c):
    return [((r + dr) % H, (c + dc) % W) for dr, dc in OFFSETS]

def count(board, r, c):
    return sum(board[nr][nc] for nr, nc in neighbors(r, c))

def rule(alive, n):
    return 1 if n == 3 or (alive == 1 and n == 2) else 0

def step(board):
    return [[rule(board[r][c], count(board, r, c)) for c in range(W)] for r in range(H)]

def frame(gen, board):
    return ['generation %d' % gen] + [''.join('#' if x else '.' for x in row) for row in board]

def boards():
    b = seed()
    out = [b]
    for _ in range(GENS):
        b = step(b)
        out.append(b)
    return out

def frames():
    return [frame(g, b) for g, b in enumerate(boards())]

if __name__ == '__main__' and len(sys.argv) == 1:
    for f in frames():
        print('\n'.join(f))

# ---------------------------------------------------------------------------
# Each port's commentary, and its whole expected output.
# ---------------------------------------------------------------------------

def positions():
    return [(r, c) for r in range(H) for c in range(W)]

def index(r, c):
    return r * W + c

def lst(*xs):
    """pprint's form of a list: keywords without their colon, nested lists."""
    def one(x):
        if isinstance(x, (list, tuple)):
            return '(' + ' '.join(one(y) for y in x) + ')'
        return str(x)
    return one(xs)

def actors():                                   # 367: frames, then the collector's count
    out = [l for f in frames() for l in f]
    out.append(lst('reports', (GENS + 1) * W * H, 'retired', W * H))
    return out

def rows():                                     # 368: every row costs the same, so first in, first out
    out = []
    for g, f in enumerate(frames()):
        out += f
        if g < GENS:
            out.append(lst('superstep', g + 1, 'arrived', list(range(H))))
    out.append(lst(*[['ok', ['rows-done', r]] for r in range(H)]))
    return out

def metronome():                                # 369: a beat a generation, row 5 30 ms late on beat 3
    out = [lst('conductor', H, 'players', 'a-beat-every', 100, 'ms')]
    for g, f in enumerate(frames()):
        t = 100 * g if g != 3 else 330
        if g == 3:
            # a timer's message lands as it rings, so the watch set at 320
            # grumbles at 330 before the woken player has played its row
            for late in (10, 20, 30):
                out.append(lst('conductor', 't', 300 + late, 'ms', late, 'late', 'waiting-for', [5]))
            out.append(lst('conductor', 'beat', 3, 'lands', 30, 'ms-late', 'beat', 4, 'still-due-at', 400))
        out.append(lst('conductor', 't', t, 'beat', g))
        out += f
    out.append(lst('conductor', 't', 800, 'the-players', [['ok', 'bow']] * H))
    return out

def death():                                    # 370: fate's census, deaths row by row, who watched whom
    out = []
    alive = {}                                  # pos -> dict(born, watched: set of ids, id)
    next_id = [0]
    def spawn(pos, gen):
        next_id[0] += 1
        alive[pos] = {'born': gen, 'id': next_id[0], 'watched': set()}
    def wire(pos, born_now):
        me = alive[pos]
        for n in neighbors(*pos):
            if n in alive:
                other = alive[n]
                me['watched'].add(other['id'])
                if n not in born_now:
                    other['watched'].add(me['id'])
    for pos in SEED:
        spawn(pos, 0)
    for pos in positions():
        if pos in alive:
            wire(pos, set(SEED))
    dead_ids = set()
    for g in range(GENS + 1):
        board = [[1 if (r, c) in alive else 0 for c in range(W)] for r in range(H)]
        out += frame(g, board)
        if g == GENS:
            out.append(lst('census', 'the-end', 'living', len(alive), 'the-rest-ghosts'))
            break
        fates = {}
        for pos in positions():
            n = count(board, *pos)
            a = pos in alive
            nxt = rule(1 if a else 0, n)
            fates[pos] = 'stays' if a and nxt else ('loneliness' if n < 2 else 'overcrowding') if a else 'born' if nxt else 'empty'
        obits = []
        for pos in positions():
            if fates[pos] in ('loneliness', 'overcrowding'):
                me = alive.pop(pos)
                mourned = len(me['watched'] & dead_ids)
                dead_ids.add(me['id'])
                obits.append(lst('obituary', list(pos), fates[pos], 'lived', me['born'], 'to', g, 'outlived', mourned))
        born_now = {pos for pos in positions() if fates[pos] == 'born'}
        for pos in positions():
            if pos in born_now:
                spawn(pos, g + 1)
        for pos in positions():
            if pos in born_now:
                wire(pos, born_now)
        out.append(lst('census', 'to', g + 1, 'births', len(born_now), 'deaths', len(obits), 'living', len(alive)))
        out += obits
    return out

def joins():                                    # 371: the count, then the frames
    return [lst('processes', (GENS + 1) * W * H, 'joins-each', 9)] + [l for f in frames() for l in f]

def timers():                                   # 372: a frame's times are its live cells' indices
    out = [lst('t', 0, 'writing-the-seed')]
    for g, b in enumerate(boards()):
        base = (g + 1) * 1000
        out.append(lst('t', base + 999, 'woke', [base + index(r, c) for r, c in positions() if b[r][c]]))
        out += frame(g, b)
    out.append(lst('t', (GENS + 1) * 1000 + 999, 'nothing-pending'))
    return out

def crash():                                    # 373: crashes in the order the cells were told, then restarts
    out = []
    lives = {}                                  # pos -> incarnations so far
    live = {}                                   # pos -> age
    def start(pos):
        lives[pos] = lives.get(pos, 0) + 1
        live[pos] = 0
        out.append(lst('supervisor', list(pos), 'first-boot-as' if lives[pos] == 1 else 'restarted-as', [pos[0], pos[1], 0, lives[pos]]))
    for pos in SEED:
        start(pos)
    for g in range(GENS + 1):
        board = [[1 if (r, c) in live else 0 for c in range(W)] for r in range(H)]
        out += frame(g, board)
        if g == GENS:
            break
        counts = {pos: count(board, *pos) for pos in positions()}
        crashes = 0
        for pos in positions():
            if pos in live:
                n = counts[pos]
                if n < 2 or n > 3:
                    out.append(lst('supervisor', list(pos), 'crashed-of', 'loneliness' if n < 2 else 'overcrowding',
                                   'its-frame-says', 'count', n, 'age', live[pos], 'life', lives[pos]))
                    del live[pos]
                    crashes += 1
                else:
                    live[pos] += 1
        births = 0
        for pos in positions():
            if board[pos[0]][pos[1]] == 0 and counts[pos] == 3:
                start(pos)
                births += 1
        out.append(lst('supervisor', 'tally', 'crashes', crashes, 'births', births))
    return out

def lcg(x):
    return (x * 1103515245 + 12345) % 2147483648

def mix(x):
    return lcg(((x * x) // 65536 + x) % 2147483648)

def dice(gen, salt, n):
    return (mix(salt + mix(mix(gen + 977))) // 65536) % n

def hostile():                                  # 374: the adversary's dice, and what's kept and dropped
    out = [lst('workers', 4, 'one-queue', 'scheduler', 'hostile')]
    for g, f in enumerate(frames()):
        if g > 0:
            stride = 1 + 2 * dice(g, 11, 32)
            offset = dice(g, 12, 64)
            forged = dice(g, 13, 64)
            out.append(lst('adversary', 'posts', 66, 'stride', stride, 'offset', offset, 'forged-from', g - 1, 'cell', forged, 'duplicate', offset))
            out.append(lst('kept', 64, 'for', g))
        out += f
    out.append(lst('answers', 66 * GENS, 'kept', 64 * GENS, 'dropped', 2 * GENS))
    return out

PORTS = {
    't/367-life-actors.expected': actors,
    't/368-life-rows.expected': rows,
    't/369-life-metronome.expected': metronome,
    't/370-life-death.expected': death,
    't/371-life-joins.expected': joins,
    't/372-life-timers.expected': timers,
    't/373-life-crash.expected': crash,
    't/374-life-hostile.expected': hostile,
}

def write_expected():
    here = os.path.dirname(os.path.abspath(__file__))
    for name, f in PORTS.items():
        with open(os.path.join(here, '..', '..', name), 'w') as out:
            out.write('\n'.join(f() + ['done']) + '\n')

if __name__ == '__main__' and len(sys.argv) > 1 and sys.argv[1] == 'write':
    write_expected()
