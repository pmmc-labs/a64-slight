# `spawn` on the M2 Max: where the time goes, 24 Oct against now

30 Oct 2026, Apple M2 Max, macOS 15.7.9 (24G830), Xcode's command line
tools only (no Xcode.app). Old is `118291f` (24 Oct), new is `82dda3f`
(HEAD's runtime and compiler; HEAD, `0b216fd`, only adds to
BENCHMARKS.md). Both built for aarch64 with `slightc`'s own flags (`-O2
-g`). Measured only: nothing in the runtime or the compiler was changed.

## In short

- **The gap is 13 to 15 ns a process, not 20.** `/usr/bin/time` rounds
  to 10 ms, so 0.120 against 0.140 s overstates it. hyperfine has 127.0
  against 140.4 ms (1.11 times), and 126.0 against 141.1 ms with
  `SLIGHT_ISLANDS=1` (1.12 times).
- **It isn't instructions.** New runs 10 more instructions a process
  than old (2284 against 2274, +0.5 per cent), but 46 more cycles (484
  against 436, +11 per cent). IPC drops from 5.22 to 4.72. Something
  stalls.
- **It isn't thread-locals.** No thread-local is loaded per process on
  spawn's path. Under lldb, `_tlv_get_addr` is called 400 times at
  start-up and 3 more for each further 1,000 processes. The time
  profile has no sample in it.
- **It isn't malloc.** libmalloc is 62 ms per million processes in old
  and 61 in new.
- **It's a stall in `retire`.** `retire`'s self time grows from 3.3 to
  13.8 ms per million processes. Nearly all of the growth is one
  instruction, the epilogue's `ldp x20, x19, [sp, #0x10]` at `retire+196`,
  with 266 to 268 samples in each run against about 10 in old. That's
  about 38 cycles a process. `rt_switch`'s `ret` adds about 2 ms more.
  Together they're the whole gap. The checks that callgrind found on
  Linux (`finish`, `rt_send`, `rt_recv`, the scheduler's inbox check)
  cost 0.4 ms or less each here.
- **The stall came with islands, step 1 (`20d78b3`).** Until `ebb2312`
  it was hidden behind the `_tlv_get_addr` calls. `ebb2312` removed 166
  instructions a process of those calls without changing the cycles (491
  against 496). `_tlv_get_addr`'s 7.6 ms per million went, and `retire`
  gained 9.0 ms.
- **What changed under `retire`: the island's state moved from `__DATA`
  into the malloc heap.** On macOS a thread-local block is malloc'd for
  its thread. `my`, and with it the scheduler's saved registers and the
  run queue, now sits in malloc's tiny region beside the process
  structs. In old they were globals in `__DATA`. The `ldp` that stalls
  reloads the scheduler's x19 and x20, which `rt_switch` has just loaded
  from `my.scheduler`. That 64-byte line also holds `nrecords`,
  `ready_head` and `ready_tail`, which every process writes. In old,
  `scheduler`'s first words had a line of their own. This is the likely
  cause, but without hardware counters it isn't proven (see
  "Conclusion").

## 1. Building

    git pull
    git worktree add ../slight-oct24 118291f
    (cd ../slight-oct24 && node bin/slightc.ts -o /tmp/spawn-old bench/spawn/spawn.slight)
    node bin/slightc.ts -o /tmp/spawn-new bench/spawn/spawn.slight
    /tmp/spawn-old 1000000      # 499999500000
    /tmp/spawn-new 1000000      # 499999500000

Both print 499999500000, which is `sum(range(1000000))`.

## 2. Times, instructions and cycles

Ten runs of each, alternating old and new, n = 1000000, with
`/usr/bin/time -l`. The environment had no other `SLIGHT_` variable.
Medians:

| | islands | real | user | instructions | cycles | IPC | peak footprint |
|---|---|---:|---:|---:|---:|---:|---:|
| old | default (12) | 0.120 s | 0.110 s | 2274.1 M | 435.7 M | 5.22 | 48.4 MiB |
| new | default (12) | 0.140 s | 0.120 s | 2284.4 M | 483.9 M | 4.72 | 48.1 MiB |
| old | 1 | 0.120 s | 0.110 s | 2273.8 M | 437.9 M | 5.19 | 48.5 MiB |
| new | 1 | 0.140 s | 0.120 s | 2284.2 M | 481.5 M | 4.74 | 48.5 MiB |

Per process, and old to new:

| islands | instructions old → new | Δ | cycles old → new | Δ |
|---|---:|---:|---:|---:|
| default | 2274 → 2284 | +10 (+0.5%) | 436 → 484 | +48 (+11.1%) |
| 1 | 2274 → 2284 | +10 (+0.5%) | 438 → 481 | +44 (+9.9%) |

hyperfine, 3 warm-ups and 20 runs, `-N`:

    env -u SLIGHT_ISLANDS hyperfine -N -w 3 -r 20 '/tmp/spawn-old 1000000' '/tmp/spawn-new 1000000'
    SLIGHT_ISLANDS=1 hyperfine -N -w 3 -r 20 '/tmp/spawn-old 1000000' '/tmp/spawn-new 1000000'

| islands | old | new | new/old | Δ a process |
|---|---:|---:|---:|---:|
| default | 127.0 ± 1.3 ms | 140.4 ± 1.5 ms | 1.11 | +13.4 ns |
| 1 | 126.0 ± 1.1 ms | 141.1 ± 1.7 ms | 1.12 | +15.1 ns |

At the 3.7 GHz the cycle counts imply (484 M cycles in 0.130 s of user
time), 46 cycles is 12 to 13 ns. That agrees with hyperfine. How many
islands there are makes no difference. Islands other than 0 are never
started in this program, and `sample` shows one thread with either
setting.

## 3. Thread-locals in the code

The old binary has no thread-local at all. The new one has four, from
`nm -m`: `_rt_current` and `_my` (`process.c`), and `_record` and
`_count` (`replay.c`). I found each access by following `adrp`/`add` to
a `__thread_vars` address in `otool -tvV`'s disassembly (the pattern is
`adrp x0, ...; add x0, x0, #off; ldr x8, [x0]; blr x8`). A script
(`tlv.py`, below) marked an access as in a loop when a backward branch
in the same function spans it.

The functions asked about, in the new binary. "Inlined" means there's
no symbol in either binary:

| function | in new? | thread-locals loaded | where |
|---|---|---|---|
| `rt_run` | yes | `_my`, `_rt_current` | once each, at entry (+0x14, +0x2c). The loop then uses x19 (`&my`) and x20 (`&rt_current`). |
| `rt_spawn` | yes | none | |
| `new_child` | yes | none | |
| `new_process_on` | inlined | none | |
| `rt_new_process` | yes | `_my` | once. Only the root's start and `place` call it. |
| `finish` (`process.c`) | yes | `_my` ×4 | only in branches spawn's actors never take: a joiner to tell (+0xec), watchers (+0x204), a side record (+0x498), and keeping the value for a parent that's a task, or for the root (+0x7bc, in the joiners' loop). |
| `retire` | yes | none | through `p->isle` |
| `rt_exit` | yes | none | |
| `end` | yes (new) | none | |
| `rt_end` | yes | `_rt_current` | once. Only `raise` and faults, not the normal end, which is `rt_exit`. |
| `release_stack` | inlined | none | |
| `get_stack` | inlined | none | |
| `start` | inlined | none | |
| `rt_send` | yes | none | |
| `rt_recv` | yes | none | |
| `to_scheduler` | yes | none | |
| `enqueue` | inlined | none | |
| `copy_values_after` | yes | none | |
| `settle_on` | inlined | none | |
| `rt_heap_grow` | yes | `_rt_current` | once. Not called in this program's processes, whose heaps never grow. |
| `rt_alloc` | yes | `_rt_current` | once. Only C builtins allocating, none on this path. |
| `take_inbox` | yes (new) | `_my`, `_rt_current` | once each. Only when the inbox isn't empty, never with one island running. |

Every thread-local, and the functions that load it (107 functions in
all):

- `_my` (54): `rt_island_of`, `rt_new_process`, `to_address`,
  `command`, `read_on`, `collect`, `rt_join`, `copy_here`,
  `rt_join_race`, `read_from`, `rt_monitor`, `notify`, `rt_kill`,
  `end_other`, `rt_spawn_on`, `place`, `rt_preempt` (in a loop),
  `rt_yield`, `rt_after`, `set_alarm`, `rt_timer_cancel`,
  `remove_alarm`, `rt_sleep`, `rt_clock_us`, `rt_clock_now`,
  `rt_connect`, `rt_connect_fs`, `fail_start`, `rt_connect_tcp`,
  `rt_connect_device`, `rt_disconnect`, `close_device`,
  `rt_socket_peer`, `rt_connect_exec`, `exec_start` (in a loop),
  `rt_connect_system`, `new_device`, `rt_run`, `main` (in a loop),
  `finish`, `shut`, `close_now`, `release`, `island_main`, `awake`,
  `take_inbox`, `ring`, `wait_for`, `lookup_step`, `fail`, `bury`,
  `tell_sym`, `exited`, `send_key`.
- `_rt_current` (34): `rt_fault`, `rt_alloc`, `rt_heap_alloc`,
  `rt_new_string`, `rt_new_float`, `rt_heap_grow`, `rt_new_shared`,
  `rt_address`, `rt_end`, `rt_join`, `copy_here`, `rt_join_race`,
  `read_from`, `rt_monitor`, `rt_kill`, `rt_spawn_on`, `place`,
  `rt_yield`, `rt_sleep`, `rt_connect`, `rt_connect_fs`,
  `rt_connect_tcp`, `rt_connect_device`, `rt_connect_exec`,
  `rt_connect_system`, `rt_open`, `rt_run`, `main`, `take_inbox`,
  `fail`, `rt_rand_int`, `rt_rand_float`, `rt_rand_seed`,
  `rt_log_line` (in a loop).
- `_record` (19) and `_count` (36): the `rt_io_` functions in
  `replay.c`, with `put_strings`, `get_strings`, `write_entry`, `head`
  and `spoilt`. None of them is called while spawn's processes run.

Counted as the program runs, with an lldb breakpoint on `_tlv_get_addr`
that counts and continues:

    breakpoint set -n _tlv_get_addr -G true
    breakpoint set -n retire -G true
    run 1000                # then 2000
    breakpoint list

| n | `_tlv_get_addr` calls | `retire` calls |
|---:|---:|---:|
| 1000 | 400 | 1002 |
| 2000 | 403 | 2002 |

That's 400 calls at start-up and 0.003 a process after, about one a
slice (`rt_preempt`). Clang on macOS hoists them as it does on Linux.
None is left on spawn's path.

## 4. Time profile

`xctrace` needs Xcode.app, which isn't installed (`xcode-select: error:
tool 'xctrace' requires Xcode`). DTrace needs SIP off, and SIP is on.
So I used `sample`, every millisecond, on runs with n = 30000000 (about
4 s and 1.5 GB each), two runs of each binary:

    /tmp/spawn-old 30000000 > /dev/null & pid=$!
    sample $pid 6 1 -mayDie -file sample-old-1.txt
    wait $pid

I added up the "Sort by top of stack" section of each file, which is
self time by function: 6484 samples for old and 7357 for new. The ms
columns scale each share to hyperfine's mean at n = 1000000 (127.0 and
140.4 ms). They estimate ms per million processes, if the mix is the
same at both sizes.

| function | library | old % | old ms/1M | new % | new ms/1M | Δ ms/1M |
|---|---|---:|---:|---:|---:|---:|
| `retire` | spawn | 2.6 | 3.3 | 9.8 | 13.8 | **+10.5** |
| `tiny_malloc_from_free_list` | libsystem_malloc | 7.4 | 9.4 | 6.2 | 8.7 | −0.7 |
| `_platform_memset` | libsystem_platform | 6.4 | 8.1 | 5.7 | 8.0 | −0.2 |
| `rt_run` | spawn | 5.6 | 7.1 | 6.4 | 9.0 | +1.8 |
| `rt_trampoline` | spawn | 5.9 | 7.5 | 5.7 | 8.0 | +0.5 |
| `_nanov2_free` | libsystem_malloc | 5.6 | 7.1 | 4.8 | 6.7 | −0.4 |
| `tiny_free_no_lock` | libsystem_malloc | 5.2 | 6.5 | 4.2 | 5.9 | −0.6 |
| `finish` | spawn | 4.8 | 6.1 | 4.6 | 6.5 | +0.4 |
| `copy_values_after` | spawn | 4.3 | 5.5 | 4.1 | 5.8 | +0.2 |
| `free_tiny` | libsystem_malloc | 4.0 | 5.1 | 4.2 | 6.0 | +0.8 |
| `rt_switch` | spawn | 2.6 | 3.3 | 4.0 | 5.6 | **+2.3** |
| `nanov2_find_block_and_allocate` | libsystem_malloc | 3.8 | 4.9 | 3.5 | 4.8 | −0.0 |
| `_free` | libsystem_malloc | 2.1 | 2.6 | 2.5 | 3.5 | +0.9 |
| `rt_recv` | spawn | 2.5 | 3.1 | 2.1 | 2.9 | −0.3 |
| `nanov2_allocate_outlined` | libsystem_malloc | 2.4 | 3.0 | 2.0 | 2.8 | −0.2 |
| `tiny_malloc_should_clear` | libsystem_malloc | 2.3 | 2.9 | 1.7 | 2.4 | −0.5 |
| `nanov2_malloc_type` | libsystem_malloc | 2.3 | 2.9 | 2.0 | 2.8 | −0.1 |
| `tiny_free_list_add_ptr` | libsystem_malloc | 2.2 | 2.8 | 2.2 | 3.1 | +0.2 |
| `tiny_free_list_remove_ptr` | libsystem_malloc | 2.1 | 2.7 | 2.0 | 2.7 | +0.1 |
| `new_child` | spawn | 2.0 | 2.6 | 1.7 | 2.4 | −0.2 |
| `rt_rand_split` | spawn | 2.0 | 2.5 | 2.0 | 2.7 | +0.2 |
| `madvise` | libsystem_kernel | 1.9 | 2.4 | 1.8 | 2.6 | +0.1 |
| `_szone_free` | libsystem_malloc | 1.9 | 2.4 | 1.5 | 2.2 | −0.2 |
| `rt_send` | spawn | 1.7 | 2.1 | 1.6 | 2.3 | +0.2 |
| `new_entry` / `new_entry_on` | spawn | 1.6 | 2.1 | 1.5 | 2.1 | +0.0 |
| `events` (inlined into `rt_run` in new) | spawn | 1.5 | 1.9 | — | — | −1.9 |

Grouped:

| | old ms/1M | new ms/1M | Δ |
|---|---:|---:|---:|
| `_tlv_get_addr` | 0 | 0 | 0 |
| libmalloc, all of it | 62.1 | 60.8 | −1.3 |
| `_platform_memset` (calloc's) | 8.6 | 8.4 | −0.2 |
| the kernel (`madvise`) | 2.4 | 2.6 | +0.1 |
| `rt_run` and `events` together | 9.0 | 9.0 | 0.0 |
| `finish`, `retire`, `rt_run`, `new_child` together | 20.0 | 31.7 | +11.7 |
| the program's own code, all of it | 54.1 | 68.6 | +14.6 |

### Within `retire` and `rt_switch`

`sample` gives each frame's offsets. `retire` compiles to almost the
same code in both, with the same loads, tests and stores. In new it
reaches the island's state through `p->isle` (`ldr x19, [x0, #0x1e0]`),
where old reached globals through `adrp`.

| | old | new, run 1 | new, run 2 |
|---|---:|---:|---:|
| `retire+196`, `ldp x20, x19, [sp, #0x10]` | ~10 | 268 | 266 |
| `retire+192`, `ldp x29, x30, [sp, #0x20]` | ~2 | 16 | 5 |
| `retire+168..`, `settle_on`'s tests of the entry | 27–35 | 44 | 34 |
| all of `retire` | 74–94 | 367 | 355 |
| `rt_switch+100`, `ret` | 204–228 | 276 | 265 |

On these cores a sample's PC is the oldest instruction not yet retired.
So the program waits about 267 samples' worth (about 9 ns, 35 to 40
cycles a process) on the second load of the epilogue. That load reloads
the x19 and x20 that the prologue saved on the scheduler's stack. Those
are the scheduler's own callee-saved registers, which `rt_switch` had
just loaded from `my.scheduler`, `ldp x19, x20, [x1]`, before its `ret`
into `rt_run`. The epilogue's first load, x29 and x30, comes from
`my.scheduler + 0x50`, on another line, and hardly waits. `rt_switch`'s
`ret` is where it always stalls (its return goes to a context the
predictor didn't expect), and it stalls about 50 samples more in new.

## 5. Which commit: building the ones in between

Hardware counters (step 5) need root for `kperf`, and the `CPU
Counters` template needs Xcode.app, so I didn't get them. Instead I
built `bench/spawn/spawn.slight` at each commit since `118291f` that
touches the runtime, in the same worktree (`git checkout -q $c && node
bin/slightc.ts -o .../spawn-$c bench/spawn/spawn.slight`). Then I timed
them together with hyperfine (`-N -w 3 -r 20`, default islands), and
counted with `/usr/bin/time -l` (medians of 7, alternating). Every one
prints 499999500000.

| commit | what | hyperfine | instructions/process | cycles/process | IPC | peak |
|---|---|---:|---:|---:|---:|---:|
| `118291f` | 24 Oct | 127.6 ms | 2274 | 435 | 5.23 | 48.4 MiB |
| `347bbc7` | struct back to 480 bytes | 125.4 | 2269 | 422 | 5.37 | 48.4 |
| `5d9c4fe` | D245 | 125.6 | 2275 | 431 | 5.28 | 48.6 |
| `34d6a68` … `cb3b99e` | D254, D255 | 126.2–127.1 | | | | |
| `8988e2a` | `-ffp-contract=off` | 124.7 | 2275 | 430 | 5.29 | 48.4 |
| `9b871bf` | shared buffers (D253) | **132.0** | 2329 | 455 | 5.12 | 63.8 |
| `9d52b93` | arrays (D253) | 133.1 | 2328 | 451 | 5.16 | 63.8 |
| `20d78b3` | islands 1: state thread-local | **153.1** | 2546 | 522 | 4.88 | 64.4 |
| `0d3e608` | islands 2: threads | 170.9 | 2661 | 590 | 4.51 | 63.7 |
| `e46e023` | state as one thread-local | 145.3 | 2470 | 491 | 5.03 | 64.4 |
| `ebb2312` | no thread-locals on busy paths | 145.2 | 2304 | 496 | 4.65 | 64.5 |
| `82dda3f` | chunk header two words again | 140.3 | 2284 | 482 | 4.74 | 48.4 |

`sample` at n = 30000000 on six of them (one run each, self samples):

| commit | total | `retire` | `rt_switch` | `rt_run` | `events` | `_tlv_get_addr` |
|---|---:|---:|---:|---:|---:|---:|
| `5d9c4fe` | 3002 | 81 | 98 | 192 | 37 | 0 |
| `9b871bf` | 3134 | 85 | 86 | 156 | 54 | 0 |
| `9d52b93` | 3102 | 93 | 93 | 142 | 41 | 0 |
| `20d78b3` | 3708 | 100 | 100 | 165 | 47 | 330 |
| `e46e023` | 3375 | 86 | 96 | 164 | 62 | 176 |
| `ebb2312` | 3480 | 304 | 138 | 215 | inlined | 0 |

From `e46e023` to `ebb2312`, scaled as in section 4: `_tlv_get_addr`
−7.6 ms per million, `retire` +9.0, `rt_switch` +1.6, `rt_run` +1.9 (with
`events` inlined into it). The calls, 166 instructions a process, went,
and the time stayed the same. `retire` waited on the same thing the
calls had waited beside. In `e46e023`, `_tlv_get_addr`'s
callers were spread out: `to_scheduler` 35, `rt_send` 35, `retire` 28,
`finish` 23, `new_child` 25, `rt_recv` 10, `events` 10.

So there are two steps, and neither is extra work:

1. **Shared buffers, `9b871bf`: +6 ms, mostly won back.** That's +54
   instructions and +24 cycles a process, and 48 to 64 MB. `82dda3f`
   (a chunk's header two words again) took back 20 instructions, 14
   cycles and the memory. What's left of it is at most 10 cycles a
   process.
2. **Islands, step 1, `20d78b3`: what's left of the gap, about 30 to 35
   cycles a process.** The thread-local calls it brought were cheap at
   this IPC. Removing them all (`e46e023`, `ebb2312`) took back 166
   instructions a process but no cycles. The latency that came with the
   island's state being thread-local stayed, and after `ebb2312` it
   shows as `retire`'s epilogue.

## Where the island's state is

lldb, at a breakpoint on `retire`, in the new binary:

    p sizeof(my)            # 424
    p &rt_current           # 0x11de04290
    p &my                   # 0x11de042a8
    p &my.scheduler         # 0x11de042e0
    p $x0                   # a process: 0x13f604ca0 in another run, malloc_size 496

`my` isn't in `__DATA`. dyld malloc's a thread's thread-local block the
first time the thread uses it, and here it's in malloc's tiny region,
in the same 16 KB page as the first process structs (2.5 KB from one).
The first lines of `my`:

| line | `my`'s fields in it |
|---|---|
| `…4280`–`…42bf` | `rt_current`, `island`, `pages`, `nprocs` |
| `…42c0`–`…42ff` | `pages_cap`, **`nrecords`, `ready_head`, `ready_tail`**, **`scheduler`'s x19–x22** |
| `…4300`–`…433f` | `scheduler`'s x23–x30 |

So the saved registers that the stalled `ldp` waits for share a line
with `nrecords`, which `new_entry_on` and `settle_on` write for every
process, and with the run queue's head and tail, which `enqueue` and
`dequeue` write. In old they were globals in `__DATA`: `scheduler` at
`0x1000400a8`, `ready_head` at `0x100040058` (another line),
`ready_tail` and `nrecords` at `0x100040190` and `0x100040188` (another
again).

## Conclusion

On this Mac the extra 13 to 15 ns a process (46 cycles) is a stall, not
extra work: +10 instructions a process, IPC 5.22 → 4.72. About 10 ns of
it is `retire` waiting on the second load of its epilogue, and about 2
ns is `rt_switch`'s `ret`. The rest moves by a few tenths of a ms each
way. Thread-locals aren't the cause. None is loaded per process on the
path (0.003 calls a process, counted), and removing the last of them in
`ebb2312` didn't change the cycles. malloc isn't the cause either,
unchanged at 61 to 62 ms a million. The work callgrind found on Linux
(`finish`'s far checks, the island checks in `rt_send` and `rt_recv`,
the inbox check) is cheap here, under 0.5 ms a million each, because it
runs at an IPC of 5. The latency, which those instructions overlap, is
what costs.

The stall arrived with the island's state becoming thread-local
(`20d78b3`), and it outlived every thread-local call being taken off
the path. What changed with that commit, besides the calls, is where
the state lives. It used to be in globals in `__DATA`, each in its own
place. It's now one struct in a malloc'd thread-local block, in the
tiny heap among the process structs. The instruction that waits is
reloading the scheduler's registers, saved where `rt_switch` just read
them, in `my.scheduler`. Its first words share a cache line with
`nrecords` and the run queue's head and tail, which every process
writes. That they share a line is measured. That the sharing, or the
block's place in the heap, is what makes the load slow is the most
likely explanation the evidence allows. It's still a hypothesis: I
couldn't count L1 misses or memory-ordering stalls without root or
Xcode.app. Two other things that changed with the same commits would
show in the same place: the extra dependent load through `p->isle` at
the top of `retire`, and `retire`'s prologue no longer shrink-wrapped
after `release_stack`. Neither can by itself cost 35 cycles of L1 hits.

### Likely fixes (not applied)

- **Keep the island's state out of the thread-local block.** Make it a
  static array of island states in `__DATA`, each aligned to a cache
  line (128 bytes, for the M2's prefetch pairs). The thread-local, or
  `p->isle`, only points at its island's. Island 0's state is then where
  old's globals were, and the busy paths already reach it through
  `p->isle`.
- **Give `scheduler` lines of its own** (`_Alignas(64)` on the field,
  or move `nrecords`, `ready_head` and `ready_tail` after it, next to
  `free_stacks`). This is cheaper, and it tests the sharing alone.

To confirm before choosing, either of these would settle it in a
throwaway build:

1. HEAD with `RT_ISLAND` defined empty, run with `SLIGHT_ISLANDS=1`. If
   `spawn` comes back to about 127 ms, the block's place or layout is
   the cause.
2. HEAD with only `_Alignas(64)` on `island_state_t`'s `scheduler`. If
   that alone takes back the 13 ms, it's the line sharing.

With `sudo`, the `kperf` framework's counters (L1D load misses, and
Apple's memory-order-violation and load-stall events) on old and new
would show which.

## The scripts

They're in the session's scratch directory, not in the repo. What they
did:

- `timeit.py N RUNS ISLANDS`: runs `/usr/bin/time -l` on old and new,
  alternating, RUNS times each, checks the sum, and takes medians of
  real, user, sys, instructions retired, cycles elapsed and peak memory
  footprint.
- `tlv.py BINARY DISASSEMBLY`: splits `otool -tvV` output into
  functions, resolves each `adrp` and `add` pair to an address, and
  reports those that are `__thread_vars` entries (from `nm -m`), with
  whether a backward branch in the function spans the access.
- `agg.py OLD NEW MS_OLD MS_NEW N`: adds up "Sort by top of stack"
  across `sample` files and prints the side-by-side table.
- `counts.py COMMITS...`: `/usr/bin/time -l` on each commit's binary,
  seven times in turn, with medians.

## Experiments

These were run later the same day, on throwaway builds of HEAD (`d1d83c6`). Each
build edited `runtime/` (asserting the edit matched), built its own
binary, and then ran `git checkout -- runtime/`. None of these changes
is committed.

    node bin/slightc.ts -o /tmp/spawn-head bench/spawn/spawn.slight

    # A: thread-locals as plain globals (rt.h)
    perl -0pi -e 's/#define RT_ISLAND _Thread_local\n/#define RT_ISLAND\n/ or die' runtime/rt.h
    node bin/slightc.ts -o /tmp/spawn-A bench/spawn/spawn.slight
    git checkout -- runtime/

    # B: the scheduler on lines of its own (process.c's island_state_t)
    #     _Alignas(64) rt_ctx_t scheduler;
    #     _Alignas(64) void *free_stacks;
    node bin/slightc.ts -o /tmp/spawn-B bench/spawn/spawn.slight
    git checkout -- runtime/

    # C: both edits
    node bin/slightc.ts -o /tmp/spawn-C bench/spawn/spawn.slight
    git checkout -- runtime/

All four print 499999500000 for 1000000. `nm -m` shows `_my` in
`__thread_vars` for HEAD and B. For A and C it's in `__bss`, and they
have no thread-locals left. Every run below had `SLIGHT_ISLANDS=1`.

    SLIGHT_ISLANDS=1 hyperfine -N -w 3 -r 20 '/tmp/spawn-head 1000000' '/tmp/spawn-A 1000000' \
        '/tmp/spawn-B 1000000' '/tmp/spawn-C 1000000'
    # /usr/bin/time -l, 7 runs of each, alternating, medians (counts.py, with SLIGHT_ISLANDS=1)
    SLIGHT_ISLANDS=1 /tmp/spawn-C 30000000 > /dev/null & pid=$!
    sample $pid 6 1 -mayDie -file sample-C.txt

| build | hyperfine, n = 1M | instructions/process | cycles/process | IPC | `retire+196` | all of `retire` | `rt_switch+100` (`ret`) |
|---|---:|---:|---:|---:|---:|---:|---:|
| HEAD | 141.1 ± 2.2 ms | 2284 | 478 | 4.78 | 269 | 360 | 237 |
| A | 139.2 ± 2.4 ms | 2288 | 477 | 4.79 | 290 | 395 | 212 |
| B | 139.1 ± 1.7 ms | 2284 | 476 | 4.79 | 253 | 365 | 242 |
| C | 138.5 ± 2.1 ms | 2288 | 477 | 4.80 | **23** | **128** | 231 |

The sample columns are self samples from one run each at n = 30M, of
3,300 to 3,600 in all. `retire`'s epilogue `ldp x20, x19, [sp, #0x10]`
is at +196 in all four builds, so the offsets compare directly.

Larger n, five runs each, with 24 Oct's binary (`118291f`) beside them:

| n | `118291f` | HEAD | A | B | C |
|---:|---:|---:|---:|---:|---:|
| 10M | 1.227 s | 1.362 s | 1.358 s | 1.359 s | 1.363 s |
| 30M | 3.702 s | 4.124 s | 4.132 s | 4.145 s | 4.132 s |

Where each build's state landed, from lldb at a breakpoint on `retire`
(`p &my`, `p &my.scheduler`, and the fields beside it):

| build | `&my` | `&my.scheduler` | `&my.ready_tail` | `&my.free_stacks` | where |
|---|---|---|---|---|---|
| HEAD | `0x11ff04418` | `0x11ff04450` | `0x11ff04448` | `0x11ff044f8` | malloc'd TLV block. `ready_tail` and the scheduler's x19–x20 share line `…4440`. |
| A | `0x1000908a0` | `0x1000908d8` | `0x1000908d0` | `0x100090980` | `__bss`. Still sharing line `0x1000908c0`. |
| B | `0x12d0040c0` | `0x12d004100` | `0x12d0040f0` | `0x12d0041c0` | malloc'd TLV block. The scheduler starts a line, and `free_stacks` the line after its last. |
| C | `0x100090900` | `0x100090940` | `0x100090930` | `0x100090a00` | `__bss`, with lines of its own as in B. |

`sudo` asks for a password here, so there are no kperf counters.

### What they show

None of the variants takes the time back. At every n, A, B and C are
within 1.5 per cent of HEAD, with the same instructions (2284 to 2288)
and cycles (476 to 478) per process. They're still 11 per cent behind
`118291f` at 10M and 30M. Only C, with both changes, takes the stall
off `retire+196`: 23 samples where HEAD has 269. A and B alone don't
touch it. Even then the time doesn't go. In C's profile against HEAD's,
`retire` loses 8.7 ms per million, and the same amount comes back spread
over `finish` (+1.9), libmalloc (+1.9), `memset`, `copy_values_after`,
`rt_send` and others. (C's run took fewer samples, 3316 against 3604,
but hyperfine at 30M says it ran no faster. `sample`'s count isn't the
run's length.)

So it was neither of the report's explanations. The thread-local
block's place in the heap (A) isn't the cause, and neither is the line
the scheduler's registers share with `nrecords` and the run queue (B).
Both together (C) aren't the cause either. The wait at `retire+196` is
where a latency comes to the surface, not where it comes from. When
`ebb2312` took the thread-local calls away, their samples went to
`retire`; when C clears `retire`, they go everywhere else. The cost is
the same in each case. The section "Conclusion" above stands for
*where* the samples fall (and for what isn't the cause: instructions,
thread-locals, malloc). Its explanation of *why*, and the fixes
proposed from it, are withdrawn. The cause is still open. It came with
`20d78b3` and outlasted every change measured here, so the next step is
counters (L1D misses, and load or memory-ordering stalls, for
`118291f` against HEAD), which need `sudo`, or bisecting `20d78b3`'s
own changes one at a time.
