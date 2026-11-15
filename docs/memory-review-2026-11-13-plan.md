# The memory review: where it stands, and what's left (15 Nov 2026)

For whoever picks this up next. The review is
[`memory-review-2026-11-13.md`](memory-review-2026-11-13.md); its
measurements are from before any of this. What was built is D294 to
D298 in `DECISIONS.md`, and the numbers after are in `BENCHMARKS.md`,
"What the memory review changed". Stevan's plan for the next session:
go through what's left of the review, below, and then finish the
memory work.

## Done

| Step | Decision | What |
|---|---|---|
| A | D294 | `:system`'s `(:memory info)`: `:nursery`, `:old`, `:mail`, `:stack`, `:shared`, and the runtime's `:resident` |
| B | D295 | A heap sized to what's live: a process that waits in `recv` is settled; a nursery's size is one run's need |
| C | D296 | Arenas (`runtime/alloc.c`): chunks, messages, processes' structs and parked frames off `malloc`; memory given back |
| D | D297 | A fork whose pid is thrown away keeps no result |
| E | D298 | A major collection pauses for other processes, every 256 KB (not the review's E: see below) |
| | D299 | A process is settled wherever it waits: `read`, `read/any`, `open`, `join`, `join/race` and `sleep` as `recv` |

`make test` passes on the cloud machine (both targets) and on the M2
Max, as of D298, and on the cloud machine as of D299. `make tsan` is
clean on the cloud machine as of D298.

## The findings, one by one

1. **A heap follows what was allocated.** Answered (D295): 1,000
   workers 640 MB to 43; and for a process that waits only in `read`,
   `join` or `sleep` (D299): 1,000 that only ask, 60 MB to 30.
2. **One allocation costs 4 KB.** Answered in part (D295): 1.6 KB for
   a process holding one cell (a first nursery of 1 KB, kept under the
   first settling's 2 KB), 2.2 KB for one holding 100. Settling every
   process at its first wait would make the first 0.6 KB and cost
   `bank` 8 per cent (measured twice, D295 and D296); left as it is.
3. **A quiet process never collects.** Answered for one that built and
   dropped before it waits (D295). Left: one that drops what it held
   across a wait without allocating as much again (below).
4. **Freed memory stays.** Answered (D296), on Linux. On macOS the
   pages are `MADV_FREE`'s, and count as resident till the system
   takes them (below).
5. **A big heap costs two to four times what's live, and a pause.**
   The pause for other processes is answered (D298). The memory, and
   the big process's own pause, aren't (below).
6. **Stacks.** Not looked at since the review: a burst of processes
   that all wait at once keeps its stacks (`copy_out` refused,
   `NO_ROOM`, when the warm list of 256 is full: 2,951 of 5,000 in the
   review's run); a stack never gets shallower (a process that
   recursed deep once holds those pages while it lives, and a warm
   stack keeps them for the next).
7. **Exit records.** Answered (D297).
8. **Messages.** Not changed, but for their allocation (D296): a
   mailbox has no bound and isn't counted toward anything; a taken
   message's 32-byte record is dead in the taker's old generation till
   a major collection or a settling.
9. **Values.** Not looked at: objects are 16-aligned where 8 would do
   (a two-element tuple 32 bytes for 24); the arena's classes are
   multiples of 16 already.
10. **What can't be seen.** Answered (D294), but an arena's free
    memory isn't shown: `:resident` less the rest includes it.
11. **Limits.** Not looked at: nothing bounds the program as a whole,
    nor a mailbox.

## What's left, with what's known about each

In the order I'd take them; each to be put to Stevan first where it
says so.

- **Measure on the M2 Max.** Nothing since D294 has been timed there.
  `make bench` for the whole table; and in particular `payload` (1.31
  times its time since D276 there, put down to `malloc`, which a send
  no longer calls), `latency` (45 ms at the worst beside 160 MB),
  `binary-trees` (the arena keeps freed chunks, the question in
  BENCHMARKS.md's "Memory on macOS"), `spawn`, `fan-in` and
  `idle-memory` (`slight` and `slight-mc`). And whether Activity
  Monitor shows a burst's memory after it's over: if it does and that
  matters, `MADV_FREE_REUSABLE` (with `MADV_FREE_REUSE` before a run is
  used again) in `alloc.c`'s `GIVE_BACK` is what libmalloc does;
  untried. `t/live.py`'s gives back is skipped on macOS for this.
- **Settling in `read`.** Done (D299), and in `read/any`, `open`,
  `join`, `join/race` and `sleep`. The check this asked for found that
  a slot wasn't always a value or zero at a builtin's call: a function
  zeroed its slots only if it called a compiled function, so the
  compiler now has one that calls any of these waits zero them too.
  Under `SLIGHT_GC_SLICE=64`, test 122 prints its joiners in another
  order, as it should (D299).
- **A process that drops what it held without allocating.** Nothing
  that counts allocation sees it. Only time can: collect a process
  that has waited a while since it last ran, once. That needs a
  waiting process's roots where the scheduler can find them (for one
  on its stack, the waiting frame, its slots in use and its frame
  pointer, three words kept at the wait; for one copied off, D292, its
  frames in the buffer, whose saved frame pointers are for the stack
  it came off, `copy_in`'s arithmetic), and a moment to do it (when
  the island is about to wait, as `rt_arena_idle` and `trim_spares`).
  Machinery; propose first, with how often it happens in a real
  program as the case for or against.
- **An old generation that isn't copied whole** (the review's E, not
  built). It would cut the memory of a major collection (from-space
  and to-space together) and the big process's own pause. Sketch:
  after a minor collection everything live is old and the roots are
  the frames; since nothing changes once made, that's a snapshot that
  stays true, so the old generation can be marked a slice at a time
  with no write barrier, what's promoted meanwhile counting as live;
  then chunks with nothing live are freed, and sparse ones copied out.
  Costs: a mark bit for each 16 bytes of a chunk, a marking twin of
  the walk, and the copying collection kept for sparse chunks. A
  replicating copy (the mutator keeps reading from-space while
  to-space is built; with nothing mutable there's no log to keep) is
  the alternative, and needs forwarding pointers beside the objects,
  not in them, since a cons has no header. Either is the largest
  change since D200: prototype and measure before proposing, and only
  if one big heap's memory or its own pause matters to the simulation
  work (D257).
- **Stacks** (finding 6). `copy_out` could put a stack on the spares
  when the warm list is full, if its pages aren't given back before
  the process has switched off it (they go back at `trim_spares`,
  which runs only when the island is about to wait or spares hold 64
  MB, so marking the entry "in use till the next switch" may be all
  it takes). A deep stack's pages could go back at a wait when its
  process is shallow again (`madvise` below the stack pointer, past
  some depth, as `trim_spares` does for a freed one).
- **Limits and mailboxes** (findings 8 and 11). A language question:
  whether a mailbox counts toward its process's cap, and whether a
  program has a limit of its own. Propose first.
- **Smaller, none urgent:** objects aligned to 8 bytes (finding 9; the
  compiler's allocation, the collector's `box_bytes`, and the arena's
  classes would all change); `entry_t` is 40 bytes and could be 24
  with its process, device and value in a union, 16 MB fewer at a
  million processes; `rt_proc_t` carries the function and eight
  arguments it starts with (72 bytes) for its whole life;
  `(:memory info)` could say what an arena holds free; shared buffers
  and devices' buffers are still `malloc`'s (D296); minor collections
  don't pause (D298: 0.5 to 0.9 ms on the cloud machine when a 1 MB
  nursery all survives); `lib/http.slight`'s and `lib/rpc.slight`'s
  acceptors monitor every connection, which D297 made unnecessary
  where `:most` isn't set.

## How to measure

- `bench/memory.py LABEL [ROOT]` builds the benchmarks and the
  programs in `bench/memory/` (the review's) with ROOT's compiler and
  prints instructions (callgrind), times, peaks and what's resident at
  rest; `bench/memory.py --compare A B` puts two runs side by side.
  For a before and after: `git worktree add DIR COMMIT`, run it with
  DIR as ROOT, then again for the working tree. Linux only. The
  instructions are what to trust on the cloud machine; its times are
  noisy.
- `bench/latency/latency.slight 10` on one island for the pauses: its
  `# heap p50 p99 max n` line, in microseconds.
- `bench/airline.py --target x86_64 --random 10` for a program of many
  small processes: its peak.
- After any change to the collector or the arena: `make test`, `make
  tsan`, and `make golden TARGETS=x86_64` under each of
  `SLIGHT_GC_VERIFY=1`, `SLIGHT_GC_STRESS=7`, `SLIGHT_GC_SLICE=64`,
  `SLIGHT_COPY_OUT=1 SLIGHT_GC_VERIFY=1`. Under stress on the cloud
  machine, 059, 130, 162, 187, 194, 198, 214, 240 and `con-memory`
  run past 60 s and are killed (162 doesn't finish in 240 s before
  any of this either); with `SLIGHT_COPY_OUT`, 200, 266, 384 and
  `con-memory` fail, and should; with `SLIGHT_GC_SLICE=64`, 122 does,
  and should (D299).

## Found on the way, and left

- `bench/spawn/spawn.slight` and `bench/idle-memory/idle-memory.slight`
  said the top level is a task that can't wait (D243), which D277
  ended: their comments now say why the extra process is kept (the
  numbers are of the same program as before).
- `rt.h`'s comment on `rt_proc_t.code` said "or restarts after waiting
  in recv", which D276 ended: corrected.
- A glibc habit worth knowing if anything goes back on `malloc`: a
  request of 1 KB or more gathers up every freed small block first
  (`malloc_consolidate`), and small requests are slow after (D295).
