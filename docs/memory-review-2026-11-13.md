# How slight uses memory: a review (13 Nov 2026)

Asked for by Stevan after the benchmarks of 11 to 13 Nov
(`docs/BENCHMARKS.md`, "Memory, next"): before fixing `spawn`'s records,
`payload`'s malloc or the collector's pauses one at a time, a view of
everything that holds memory, what each costs, and whether the model
still fits now that the process model has changed (D276, D277, D286,
D291, D292). Nothing is changed by this document; it ends with
directions to choose among, each to be proposed properly before it's
built.

**Since (13 Nov):** Stevan chose A, then B. A is built (D294):
`:system`'s `(:memory info)`. B is built (D295): a process that waits
is settled, and findings 1 to 3 are, for the most part, answered; the
numbers after are in `docs/BENCHMARKS.md`, "What the memory review
changed", and what B left is in D295. The measurements below are as
they were before both.

The numbers are from the cloud machine: x86-64 Linux, one island,
static glibc, 4 KB pages, resident memory read from `/proc` while the
program sleeps (the programs are in the appendix). The M2 Max will
differ where pages (16 KB) and the allocator do, and those places are
marked.

## The short of it

The process model was rebuilt around the waiting process, and the
stack side of memory was rebuilt with it (slabs, colours, copying a
waiting process off its stack). The heap side is still the one from
D89, D106 and D200, sized and tuned for a few busy processes and
benchmarks that end. Measured against "many processes that live long",
which is what slight is for (D250), five things stand out, biggest
first:

1.  **A process's heap follows what it has allocated, not what it
    holds.** The nursery only grows, to 1 MB, and an old generation
    under 256 KB is never collected. A worker holding 48 bytes sits at
    640 KB after 10,000 messages. A thousand of them hold 640 MB.
2.  **A process that has allocated one cell holds a 4 KB nursery for
    good.** A waiting process that never allocated is 580 bytes; one
    with any state at all is 4.5 KB. The benchmark's figure
    (`idle-memory`, 731 bytes on the M2 Max) is the first kind; real
    actors are the second.
3.  **A process that goes quiet never collects.** One that built 64 MB,
    dropped it, and then waits, or takes small messages, holds the 64
    MB till it ends.
4.  **Freed memory stays with the program.** Every chunk, message and
    struct is a `malloc`; after a burst of 20,000 processes with 16 KB
    each has ended, 554 MB of its 613 are still resident.
5.  **A big heap costs two to four times what's live**, and a pause to
    copy it (the known one: 45 ms beside 160 MB on the M2 Max).

Exit records (`spawn`, `bank`), which started this, are real but
seventh on the list. None of the five is visible from inside a
program: `:system` reports a heap's bytes in use, not what it holds.

## Where memory lives

Everything a running program holds, with who owns it and when it goes.
Sizes are x86-64's; AArch64's where it differs.

**Per process**

- **The struct** (`rt_proc_t`): 392 bytes (504 on AArch64, whose saved
  registers are 168 bytes to x86-64's 56), one `malloc`, freed at its
  end. 72 bytes of it are the function and eight arguments it starts
  with, used once. DESIGN.md still says 352.
- **The side** (`rt_side_t`): 216 bytes, made only for a process that
  owns a device, has an address, holds a shared buffer, watches across
  islands, joins, or has children whose results are kept.
- **Its table entry** (`entry_t`): 40 bytes, in pages of 1,024 (40 KB),
  a page freed once every entry in it is dead.
- **Its stack**: a slot of 8 MB of addresses in a slab of 64, no guard
  pages; resident, the pages it has touched, at least one. Given back
  when it ends: kept warm (up to 256 an island), or a spare whose pages
  go back when the island is idle or spares hold 64 MB.
- **Its frames off the stack** (`parked_t`, D292): 56 bytes and the
  frames, a hundred bytes or two, kept (the room, not the frames) once
  made.
- **Its nursery**: one chunk, 4 KB at the first allocation, doubled
  each time it overflows, to 1 MB; never smaller again.
- **Its old generation**: a list of chunks. What minor collections
  promoted (chunks doubling to 1 MB), and one chunk for each message
  taken, each fork's values and each joined result that had something
  to copy.
- **Its mailbox**: a list of messages, each one `malloc`: a 16-byte
  chunk header, a 32-byte record, then the copy. Unbounded, and not
  counted toward the heap's cap.
- **Its monitors** (`rt_watch_t`, 64 bytes each), and its handles to
  shared buffers (a pointer each, in two growing arrays).

**Per island**

- The state (`island_state_t`, 512 bytes) and the inbox (128), static.
- The process table's page directory; the open addresses (16 bytes
  each, a table at most half full); the timers (40 bytes each, and
  for one `after` set an entry in an index at most half full, and its
  message).
- The copy's scratch buffer and its slots: kept up to 1 MB each.
- Up to 256 warm stacks, each with whatever pages its last process
  touched.
- Items in flight between islands: a message's is 64 bytes in the
  message's own chunk; the others 224 bytes each.

**Per device** (`rt_device_t`): 536 bytes, a read buffer that grows to
the line or value being read (under 64 MB, D160), and an output buffer
that grows to what's waiting to be written (bounded only where
`http/serve-with` and `rpc/serve-with` say, D270).

**Shared**: buffers for strings and arrays of 4 KB or more, their
references counted, freed when the last holder collects or ends. Static
data (literals, quoted lists, closures that capture nothing, the
function-name table) is in the binary and never copied.

**The program**: 2.7 MB on disk (static; 1.5 MB of text, most of it
Unicode's tables and CORE-MATH), 1.4 MB resident before it does
anything, the same on one island or four, since an island's thread
starts when something is first placed on it.

**Values**: an integer, a keyword, a symbol, a pid, `()` and the
booleans are a word, no memory. A cons is 16 bytes, no header. A float
is 16. A tuple is 8 and 8 an element, rounded up to 16: two elements
32, where 24 would do. A closure is 32 and 8 a captured value. A
string is 8, its bytes and a NUL, rounded to 16. Everything is
16-aligned though the tags need only 8.

## What was measured

| Program | Resident |
|---|---|
| Nothing (`(sleep 1500)`) | 1.4 MB |
| 100,000 processes waiting in `recv`, never allocated | 59 MB: 580 bytes each |
| 100,000 waiting, each holding a list of 1, 10 or 100 integers | 453 MB: 4.5 KB each |
| 1,000 waiting, never allocated | 5.7 MB: 4.3 KB each (a stack page) |
| 1,000 waiting, each holding a list | 9.6 MB: 8.2 KB each |
| 1,000 workers, 48 bytes live each, after 100 messages each | 26 MB: 25 KB each |
| ... after 1,000 messages each | 129 MB: 128 KB each |
| ... after 10,000 | 640 MB: 640 KB each |
| ... after 100,000 | 652 MB: 652 KB each |
| The same workers sent bare integers, after 10,000 each | 246 MB: 246 KB each |
| ... after 100,000 | 1,019 MB: 1 MB each |
| One process, a list of 1 million integers (16 MB live), built once | 33 MB |
| ... rebuilt 5 times | 63 MB |
| One process, 64 MB live, rebuilt 5 times | 266 MB |
| One process, 256 MB live, rebuilt 3 times | 827 MB |
| One process that built 64 MB, dropped it, and waits | 67 MB |
| ... and then takes 100,000 messages | 67 MB |
| A mailbox of 1 million integers | 62 MB: 61 bytes each |
| A mailbox of 1 million three-element lists | 109 MB: 108 bytes each |
| ... after the mailbox is drained | the same |
| 100,000 processes with 160 bytes each, after all have ended | 450 MB of a peak of 455 |
| 20,000 processes with 16 KB each, after all have ended | 554 MB of a peak of 613 |
| ... with glibc told to map every chunk of 4 KB or more | 108 MB of a peak of 615 |
| 1,000 processes that recursed 1,000 deep once, then wait | 36 MB: 30 KB each |
| ... 20,000 deep | 615 MB: 610 KB each |
| 5,000 processes, 20,000 deep once, then wait | 3,070 MB: all 5,000 stacks |
| `spawn` 1,000,000 (from the first look) | 108 MB; 63 with no records kept |
| `bank` 1,000,000 | 49 MB; 12, and flat, with none kept |

## Findings

### 1. A heap follows what was allocated, not what's held

Two rules make this, each sensible alone.

The nursery is collected only when it overflows, and an overflow
doubles it (`restart`). So its size is a count of how much the process
has ever allocated, capped at 1 MB: after about 1 MB allocated in all,
a process has a 1 MB nursery, and every page of it has been written.
Nothing makes it smaller.

The old generation is collected at twice what survived the last major
collection, and never below 256 KB (`GC_MIN`, D106). Each message taken
that had something to copy is a chunk of its own in the old
generation, garbage the moment the process drops it, and counted
toward the 256 KB by its payload alone: a message of 64 bytes costs 96
with its record, header and `malloc`'s own.

The worker measured holds a three-cell list and takes `(:work n)`. Its
old generation fills with 4,000 dead messages (384 KB as malloc sees
them) before the first major collection; that collection also starts
the nursery again, so the nursery stops growing at 256 KB: 640 KB, for
48 bytes. Sent bare integers instead (D199), the old generation stays
empty, nothing starts the nursery again early, and it goes on to its
full 1 MB: measured, 1,019 MB for the thousand.

This is the airline's 84 MB for 300 aircraft (D210), and
`thread-ring`'s 266 MB before D199, seen each time as a benchmark's
oddity. It's the model: **a long-lived process that does anything
settles at somewhere between 0.6 and 1.25 MB, whatever it holds.**
Erlang's process starts at 233 words, grows its heap in steps as what
survives grows, and shrinks it when a collection finds it mostly
empty.

D106's reason for the floor was that "a short-lived or small process
never collects", when a collection copied everything. Since D200 a
minor collection costs only what survives, and for a process holding
48 bytes a major one costs nothing either.

### 2. One allocation costs 4 KB

The first allocation makes the nursery, 4 KB, and a waiting process
keeps it (DESIGN.md: "a waiting process keeps its nursery"). So the
headline figure, a waiting process in 580 bytes (731 on the M2 Max),
is for a process that has never allocated. A process with one cons of
state is 4.5 KB: Go's and Erlang's 2.7 KB look different beside that.

D200 turned down "a pool of nurseries that waiting processes give
back, as they do their stacks (later, if processes that allocate and
then wait turn out to hold too much)". They do.

### 3. A quiet process never collects

A collection is triggered by allocating: the nursery overflowing, or
the old generation reaching `gc_at`. After a major collection `gc_at`
is twice what was live then. A process that built 64 MB and dropped it
has `gc_at` at 128 MB or so, and if it then waits, or works in small
steps, it never gets there: 67 MB held by a process holding nothing,
measured, and unchanged after 100,000 more messages.

Waiting was once where collecting happened (D105), when a wait threw
the stack away and the roots were a function's arguments. Since D276 a
wait keeps its frames, and since D292 the runtime already does work at
a wait once many are waiting. Nothing looks at the heap there.

### 4. Freed memory stays with the program

Every chunk of every heap, every message and every struct is a
`malloc` and a `free`. glibc returns memory to the system only from
the top of its heap, or for blocks big enough to be mapped alone (128
KB at first, a threshold that moves up as such blocks are freed). A
program's chunks are 4 KB to 1 MB, interleaved with long-lived things
(the root's own chunks, table pages), so after a burst nearly all of
the peak stays resident: 554 MB of 613 in the measurement, and 108 MB
when the same run maps each chunk of 4 KB or more (`GLIBC_TUNABLES`).
The stacks do go back (D291); the heaps were never looked at.

This is the Linux side of BENCHMARKS.md's "Memory on macOS" (whether
the collector should keep and reuse its chunks). It's a candidate for
`payload`'s time on the M2 Max too, not yet profiled: a chunk of 1.7
KB allocated and freed for each message, by an allocator slight
doesn't control. And BENCHMARKS.md already has starting and freeing
processes and messages spreading only 3 to 5 times across 12 islands
because they lean on `malloc`.

### 5. A big heap costs two to four times what's live, and a pause

A major collection copies everything live into new chunks before it
frees the old: 2 times what's live at the least, 3 when it runs at its
threshold, and 4 measured, with what malloc keeps. The copy isn't
counted toward the cap (LANGUAGE.md says so: a process at its cap
"holds about half the limit" as it faults). And everything on the
island waits for it: 0.4 ms a MB live on the M2 Max.

This is the known one (D200's next steps, D238, D239). It matters to a
program with one or a few big heaps, which is the simulation side of
D257; findings 1 to 4 matter to a program with many small ones.

### 6. Stacks: what D291 and D292 left

- **A burst of processes that all wait at once keeps its stacks.**
  `copy_out` puts the stack on the warm list, which holds 256; with it
  full, the copy is refused (`NO_ROOM`) and the process keeps its
  stack till it next waits. It happens when processes are preempted
  before their first wait (a start that makes more than 1,000 calls),
  so all of them hold stacks before any waits. Of 5,000 such
  processes, counted with `COPY_TALLY`, 1,024 kept their stacks under
  the gate, 1,025 were copied off, and 2,951 were refused. A page
  each at the least, 16 KB on macOS.
- **A stack never gets shallower.** A process that recursed 20,000
  frames once holds those 610 KB while it lives, and a warm stack keeps
  them for the next process. The prelude's list functions are loops
  (D203), so this needs a program's own deep recursion; but 5,000 such
  processes held 3 GB.
- **Under 1,024 waiting, each holds a page**: 4 KB here, 16 KB on the
  M2 Max. Chosen (D292) for speed, and bounded: 4 MB or 16 MB an
  island.

### 7. Exit records

About 45 bytes a child whose parent never joins it (D277): the entry,
which pins its page, and the parent's list of ids; and a quarter of
`spawn`'s time. `spawn` holds 108 MB at a million where it would hold
63, and `bank` 49 where it would hold 12, flat. Both fork in statement
position and throw the pid away. The options, a language question
apart from the rest:

- a fork whose pid is discarded keeps no result, which the compiler
  can see, and both programs go flat with no change to them; the catch
  is a child that sends its own `$$` to someone who joins it after it
  has ended, who gets `:ended`;
- keep the rule and shrink the record, to about 24 bytes, by moving a
  kept result out of the table into the parent's list, so the pages
  go: still a cost per child;
- turned down as they stand: dropping a record when the parent's heap
  no longer holds the pid (a test in the collector's walk, and a pid in
  a mailbox is a hole), keeping nothing as Erlang does (undoes D244),
  and a cap with the oldest dropped (arbitrary, and silent).

### 8. Messages

- A waiting message is 61 bytes with nothing to copy, and 108 for a
  three-element list (48 of payload): `fan-in`'s 640 MB at ten
  million.
- Taken, a message with a payload leaves its 32-byte record and 16-byte
  header dead in the taker's old generation, and its chunk as one more
  link in the old generation's list, till a major collection frees
  them one by one.
- A mailbox has no bound and isn't counted toward anything. Nor are
  timers' messages, results kept, or a device's buffers.
- Sharing inside a message isn't kept (a DAG is copied as a tree, as
  in the BEAM), bounded by D265.

### 9. Values

Objects are rounded to 16 bytes where 8 would keep the tags working: a
tuple of two, the map's and the trees' common case, is 32 bytes and
could be 24 (BENCHMARKS.md has `binary-trees` 30 per cent fewer
instructions with pairs). Floats are boxed, 16 bytes each (D260 tried
immediates and stopped; D261 keeps trees of them unboxed). A closure's
header is four words. None of these is new; they're here to complete
the map.

### 10. What can't be seen

`:system` gives a process's heap in use and whether it holds a stack.
It doesn't give: the heap's chunks (the nursery's size, the old
generation's garbage), the stack's resident bytes, the mailbox's
bytes, what the kept results and timers hold, or what the program has
from the system in all. Every finding above was found from outside,
with `/proc`. A program can't watch its own memory, and nor could a
test.

### 11. Limits

The only limit is one process's heap (1 GB, or a quarter of a
container's memory, D233). Nothing bounds the program: a thousand
processes at half the cap, mailboxes, or the copy a collection makes.
In a container the kernel's limit is the bound, and it kills the
program.

## What the new process model changes

The heap's design assumed a process is either running or reduced to
its struct. That was true till D276: a wait dropped the stack, so "a
waiting process costs its struct" and the heap was the only other
thing it had. Three things are different now.

- **A wait is a place where the runtime works.** `wait_from` already
  decides, at every wait, whether to copy the process off its stack.
  The same moment is the natural one to settle its heap: collect what
  a quiet process would otherwise keep (finding 3), and give back or
  shrink the nursery (findings 1 and 2).
- **A waiting process is already data in a buffer.** Past 1,024, it is
  a struct, a `parked_t` of frames, a nursery chunk and an old chunk:
  four allocations of unrelated sizes for one small thing. Its frames
  and its live data are both small, both only read while it waits, and
  could be one block.
- **Every process is the same kind** (D277), started with a function
  and its arguments (D286). There's no cheap kind any more (a task
  that can't wait), so the cost of the one kind is the cost of
  everything. The struct still carries what the old model needed (the
  restart's code and eight arguments).

And one thing is the same as it ever was, and is the model's strength:
nothing old points at anything young, nothing changes once made, and
heaps share nothing but counted buffers. That's what lets a collector
do without a write barrier (D200), and it would let an old generation
be marked a slice at a time without one too, since a snapshot of an
immutable heap stays true.

## Directions

Not fixes; ways the model could go, to choose among. They're largely
independent, and in the order I'd take them.

**A. Make memory visible first.** `:system` to say what each process
holds, not just what it uses (its chunks' bytes in each generation,
its stack's, its mailbox's), and what the program has in all; and a
way for a test to assert on it. Small, changes no behaviour, and
everything after it can then be a golden test rather than a script
reading `/proc`. It's a change to what a device says, so it needs
agreeing.

**B. A heap sized to what's live.** One policy in place of the two
rules of finding 1: the nursery sized from what the last collections
found (shrinking as well as growing), the old generation's threshold
relative to what's live with a floor far below 256 KB, and a
collection when a process waits with more garbage than a little.
Erlang's shape, with slight's cheaper collector. This is findings 1,
2 and 3, the biggest three, and it's policy: the collector's walks
don't change. The cost is more collections for busy small processes,
each of which copies nearly nothing; `thread-ring`, `ping-pong`,
`fan-in` and the airline would say what it costs in time.

**C. Own the chunks.** An allocator of slight's own for heap chunks
and messages, per island: size classes, free lists, pages mapped and
given back as the stacks' are. This is finding 4 on Linux, "Memory on
macOS" and probably `payload` on the M2 Max, and it takes `malloc` out
of a send. It's the most code of the first three, and the stacks'
allocator (D291) is the pattern. It fits after B, since B decides
which sizes matter.

**D. Exit records.** The language question of finding 7, whenever you
like; it doesn't interact with the others.

**E. An old generation that isn't copied whole.** Finding 5: marked
in slices, with no write barrier, and its chunks freed or kept whole.
The largest change to the collector since D200, and wanted by the
simulation side more than the server side. To prototype and measure
before deciding.

**F. One block for a small process.** The radical one: a waiting
process's struct, frames and live data in a single block from C's
allocator, a few size classes, so a small actor is one allocation of
1 or 2 KB and a million of them is a known number. I'd not start
here: B and C get most of it, and what's left would show whether this
is worth its machinery.

Smaller things that fall out along the way, none urgent: objects
aligned to 8 bytes (finding 9); `copy_out` when the warm list is full,
and a deep stack's pages given back at a wait (finding 6); a mailbox
counted toward something (finding 11); the struct's start-only fields
(the model section).

## Questions

1.  Is the order A, B, C right, with D whenever and E as a prototype?
    Or does the simulation side (E) come first?
2.  For B, how much time is memory worth? A worker at 640 KB could be
    at 8 KB for a few per cent more collections; the benchmarks are
    all tuned the other way.
3.  Should a program have a memory limit of its own, beyond each
    process's (finding 11)?
4.  Is "a waiting process in under 1 KB" a promise for processes with
    state too? That decides whether F is ever wanted.

## Appendix: the programs

Each was compiled with `slightc --target x86_64` and run with
`SLIGHT_ISLANDS=1` on the real clock; its resident memory (`VmRSS`)
was read from `/proc/PID/status` every 50 ms, and the figure is the
one 0.4 s before it ended, while it slept.

Waiting processes, `n` of them, each holding a list of `k`:

```lisp
(defun idle (st) (recv ((:ping from) (send from (length st)))))
(defun idle-with (k) (idle (range 0 k)))
(defun start (n k acc)
    (if (== n 0) acc (start (- n 1) k (cons (if (== k 0) (fork idle ()) (fork idle-with k)) acc))))
(let n (str->int (car @ARGV)))
(let k (str->int (cadr @ARGV)))
(let ps (start n k ()))
(sleep 2500)
(length ps)
```

Workers, `n` of them, `m` messages each:

```lisp
(defun worker (st)
    (recv
        ((:work x) (worker (list x (+ x 1) (car st))))
        (:stop     st)))
(defun start (n acc) (if (== n 0) acc (start (- n 1) (cons (fork worker (list 0)) acc))))
(defun rounds (ps m)
    (when (> m 0)
        (map (lambda (p) (send p (list :work m))) ps)
        (yield ())
        (rounds ps (- m 1))))
(let n (str->int (car @ARGV)))
(let m (str->int (cadr @ARGV)))
(let ps (start n ()))
(rounds ps m)
(sleep 2500)
(length ps)
```

One heap of `n` cells, rebuilt `r` times:

```lisp
(defun build (n acc) (if (== n 0) acc (build (- n 1) (cons n acc))))
(defun churn (r xs n) (if (== r 0) xs (churn (- r 1) (build n ()) n)))
(let n (str->int (car @ARGV)))
(let r (str->int (cadr @ARGV)))
(let xs (churn r (build n ()) n))
(sleep 1500)
(length xs)
```

A process that builds `n` cells, drops them, and waits:

```lisp
(defun build (n acc) (if (== n 0) acc (build (- n 1) (cons n acc))))
(defun burst (n) (length (build n ())))
(defun idle (k) (recv (:tick (idle (+ k 1))) (:stop k)))
(defun go (n) (let x (burst n)) (idle x))
(let p (fork go (str->int (car @ARGV))))
(sleep 500)
(dotimes 0 (str->int (cadr @ARGV)) (lambda (i) (send p :tick)))
(sleep 2000)
(send p :stop)
(join p)
```

A burst that ends, `n` processes of `k` cells:

```lisp
(defun hold (k) (let xs (range 0 k)) (recv (:go (length xs))))
(defun start (n k acc) (if (== n 0) acc (start (- n 1) k (cons (fork hold k) acc))))
(let n (str->int (car @ARGV)))
(let ps (start n (str->int (cadr @ARGV)) ()))
(sleep 1000)
(map (lambda (p) (send p :go)) ps)
(let total (sum (map (lambda (p) (cadr (join p))) ps)))
(sleep 2500)
total
```

A mailbox of `n`, integers (0) or lists (1):

```lisp
(defun sink () (sleep 2500) (drain 0))
(defun drain (k) (recv (:end k) (x (drain (+ k 1)))))
(defun fill (p n mode) (when (> n 0) (send p (if (== mode 0) n (list n n n))) (fill p (- n 1) mode)))
(let n (str->int (car @ARGV)))
(let p (fork sink))
(fill p n (str->int (cadr @ARGV)))
(sleep 3000)
(send p :end)
(join p)
```

Processes that recurse `d` deep once, `n` of them:

```lisp
(defun deep (d) (if (== d 0) 0 (+ 1 (deep (- d 1)))))
(defun w (d) (let x (deep d)) (recv (:go x)))
(defun start (n d acc) (if (== n 0) acc (start (- n 1) d (cons (fork w d) acc))))
(let ps (start (str->int (car @ARGV)) (str->int (cadr @ARGV)) ()))
(sleep 2500)
(length ps)
```
