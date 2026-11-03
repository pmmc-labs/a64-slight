# a64-slight: code review, every issue found

Reviewed at commit `5699be7` (2026-11-02, "License under Apache 2.0 with
LLVM's exceptions"). Line numbers are the code's at that commit.

## How this review was done

- **Mostly static, with the high-severity bugs reproduced.** The findings
  come from reading the code. On 2026-11-03 the project was built for
  x86-64 (its native target here: Node 22.23, clang 18; `npm install`, all
  271 unit tests pass), and the high-severity bugs that benefit from a
  runtime check were reproduced by compiling and running small programs.
  See "Runtime confirmation (2026-11-03)" below. The AArch64 target, and
  the network findings (H4, H5), were not run.
- Three reviews ran in parallel: the runtime core (scheduler, collector,
  islands, builtins), the I/O and security surface (devices, DNS, JSON,
  s-expressions, crypto, HTTP, RPC, agent tools), and the compiler and
  driver. The project-level checks were done separately.
- **Status** says how sure each finding is:
  - **✔ verified**: I re-read the code path myself and it does what the
    finding says.
  - **◐ traced**: a reviewer followed the full path through the code, but I
    didn't re-read all of it.
  - **plausible**: the mechanism is in the code, but whether it bites depends
    on the environment (kernel limits, curl's configuration).
- A **⚑ reproduced** tag in the Status column means the trigger was run and
  behaved as the finding says (details below).

## Runtime confirmation (2026-11-03)

Built for x86-64 and run. Each program is tiny and self-contained.

- **H2 ⚑** `(let k (recv ((:scale length) length))) ... (* k (length xs))`
  with the message `(:scale 10)` should print `30`. It instead faulted
  `not a function: 10 (call at …:3:18)`: the pattern's `length` had hidden
  the function in the rest of the body. The variant `(let v (recv ((:a x)
  x))) (pprint x)`, with no outer `x`, compiled and printed the message's
  value `99` instead of being an unknown-name error.
- **H6 ⚑** `(sexp/parse "#int(20000000000000000000)")` should be
  `(:error …)`. It returned `(ok #int(214425913))`, and a second parse in
  the same run returned `(ok #int(214426273))` — a different value, i.e. a
  leaked internal word, not the number. `(sexp/parse "#int(4611686018427387904)")`
  (2^62) was correctly `(error (bad-sexp 24))`, confirming the digit
  counter wraps only past 63 bits.
- **H3 ⚑** A value built by doubling a shared leaf (`(defun double (x n)
  (if (== n 0) x (double (list x x) (- n 1))))`) is O(n) to build. Joining
  a fork of it timed, by depth: n=10 0.008s, n=18 0.024s, n=20 0.081s,
  n=22 1.24s, n=24 6.79s — exponential, because the copy expands the ~n
  shared cells into ~2^n. Build stays instant throughout.
- **H1 ⚑** `(let s (str "abc" "d")) (str/repeat s 4611686018427387903)`
  crashed with SIGSEGV (signal 11). The contrast case `(str/repeat s
  1000000000000)` — a large count whose product does **not** wrap —
  faulted cleanly with `heap exhausted` and exit 1. So the crash is the
  size-check wrap, not merely an over-large request. (ASan's runtime wasn't
  installed here; the clean-fault-vs-crash contrast is the proof.)
- **H4, H5** were not run. H4 is a hard `FD_SETSIZE` constant plus missing
  `after` timeouts; H5 is the absence of CR/LF checks in
  `http/request-text` and `http/response-text`. A live demo would mean a
  loopback server plus a raw client (H5) or a deliberate DoS (H4); both are
  confirmed by the code, so neither was run.

## Summary

| ID | Sev. | Area | Issue | Status |
|---|---|---|---|---|
| H1 | High | runtime | Heap buffer overflow: `rt_new_shared`'s size check wraps (`str/repeat`, `str/pad-*`) | ✔ ⚑ |
| H2 | High | compiler | One-clause `recv` leaks its pattern's names into the rest of the function: miscompiles | ✔ ⚑ |
| H3 | High | runtime | Heap cap and fault isolation bypassed by off-heap work: message copies, `eq?`/`hash`, string builders | ✔ ⚑ |
| H4 | High | net | Servers: no timeouts, ~1020 descriptors for the whole program, unbounded output, EMFILE spin | ✔ / ◐ |
| H5 | High | net | CR/LF injection when building HTTP requests and responses | ✔ |
| H6 | High | net | Heap address leak: `#int(...)` overflow in the s-expression validator, echoed by RPC | ✔ ⚑ |
| M1 | Med | runtime | A device failure inside an inbox batch abandons the batch; the program hangs at exit | ◐ (key steps ✔) |
| M2 | Med | runtime | Island wake pipe never checked against `FD_SETSIZE`: `fd_set` overflow on the stack | ✔ |
| M3 | Med | compiler | Names like `toString`, `valueOf`, `constructor` crash the compiler | ✔ |
| M4 | Med | runtime | 8 MB stack mappings, never returned: ~32K blocked processes exceed `vm.max_map_count` | plausible |
| M5 | Med | runtime | Task addresses cut the pid to 32 bits; pids are 52-bit and never reused | ✔ |
| M6 | Med | net/agent | curl run without `-q`, `--globoff`, `--max-redirs 0`: per-URL approval can be defeated | ✔ / plausible |
| M7 | Med | runtime | Close-on-exec set after creating a descriptor: other islands' commands inherit sockets | ✔ / ◐ |
| L1 | Low | runtime | `timer/cancel` doesn't check who owns the timer | ✔ |
| L2 | Low | lib | `fs/delete-all` follows a symlink swapped in during the walk | ✔ |
| L3 | Low | net | HTTP parsing is quadratic in the size of a head that arrives slowly | ✔ |
| L4 | Low | agent | Read tools need no approval and aren't confined; a FIFO hangs the runtime | ◐ |
| L5 | Low | runtime | Replay records hold secrets; `SLIGHT_RECORD` passes to child programs | ✔ |
| L6 | Low | net | HTTP framing laxness: Transfer-Encoding by substring, first duplicate wins | ✔ / ◐ |
| L7 | Low | net | DNS takes the first A record without matching the name; no IPv6 anywhere | ✔ |
| L8 | Low | runtime | C buffers leak when a builtin faults after allocating them | ✔ / ◐ |
| L9 | Low | runtime | Data race on the plain global `input_seen` | ✔ / ◐ |
| L10 | Low | compiler | `.sly` raw names can contain spaces: module privacy and hygiene bypassed | ✔ |
| L11 | Low | compiler | `x \|> fun(y) => ...` makes a closure instead of calling it | ✔ |
| L12 | Low | compiler | `case` tests with an `eq?` that a local can hide | ✔ |
| L13 | Low | compiler | More-than-8-locals packing checks only some names for `cons` | ✔ |
| L14 | Low | compiler | `(defun f)` crashes the type checker | ◐ |
| L15 | Low | driver | Driver problems: stack traces, `-o -x`, cache inside the install, stale cache key, default target | ✔ / ◐ |
| N1–N3 | Nit | docs/code | Stale README, leap seconds, size of files | ✔ |
| P1–P7 | — | project | Maturity, releases, CI, stability, gaps, limits, tests, licence | ✔ |

---

## High

### H1. Heap buffer overflow: `rt_new_shared`'s size check wraps

- **Severity:** High. **Area:** runtime. **Status:** ✔ verified.
- **Where:** `runtime/process.c:488-494` (`rt_new_shared`), reached through
  `runtime/rt.c:571-572` (`rt_new_string`, for lengths of 4096 bytes or
  more) from `runtime/strings.c:237-248` (`str/repeat`) and
  `runtime/strings.c:251-260` (`pad`, under `str/pad-start` and
  `str/pad-end`).
- **Problem:** `str/repeat` checks `len * k` for multiplication overflow
  (`size_times`). But `len = 4`, `k = 2^62 - 1` (the largest integer) gives
  `2^64 - 4`, which doesn't overflow. Then:
  1. `held(p) + len > heap_max` wraps to `held(p) - 4`, so the check passes
     once the process holds 4 bytes or more.
  2. `malloc(sizeof *b + len + 1)` wraps to a small size (13 bytes, if the
     header is 16).
  3. `b->bytes[len] = '\0'` writes 4 bytes before the buffer.
  4. `str/repeat`'s loop then copies 4 bytes `2^62 - 1` times from the start
     of the buffer. It overwrites the malloc heap, which every island shares,
     until it hits an unmapped page.

  `pad` reaches the same sizes through
  `size_times(whole, len_of(fill)) + part` when the fill character is 4
  bytes.
- **Trigger:**

  ```lisp
  (str/repeat (str "abc" "d") 4611686018427387903)
  (str/pad-start "" 4611686018427387903 "😀")   ; once the process has allocated
  ```

- **Impact:** memory corruption from ordinary code, in any program that
  takes a count or width from its input. Golden test 262 uses `"ab"`, whose
  product (`2^63 - 2`) doesn't wrap, so it misses this.
- **Fix:** write the cap test so it can't wrap,
  `len > heap_max - held(p)`, and check `sizeof *b + len + 1` with
  `__builtin_add_overflow`. Better still, put every size check in
  `rt_alloc` and `rt_new_shared`, not in each builtin. Add golden tests at
  the wrap boundaries: lengths 1, 2, 3 and 4 with counts near `2^62`.

### H2. One-clause `recv` leaks its pattern's names into the rest of the function

- **Severity:** High (wrong code, silently). **Area:** compiler.
  **Status:** ✔ verified.
- **Where:** `compiler/src/split.ts:334-341`, the shortcut in `waitFor`.
- **Problem:** take a `recv` with one clause and no nested `recv`, with forms
  after it. The shortcut makes the receive function's single clause
  `pattern → pre... (let t (do body...)) back... rest...`. So the rest of the
  function runs inside the clause, where every name the pattern binds is
  still in scope.
  - `back` rebinds only names that were outer locals (`live`).
  - Any other name the pattern binds hides, for the rest of the function,
    a prelude function, a program's function or a builtin. A name that's
    unbound in the rest gets the message's value instead of an error.
  - `docs/LANGUAGE.md` says a pattern's names are bound "in its body only".
  - In `codegen.ts`, a local is looked up before functions (`:528`,
    `lookup(cx.env, name)`), so the call becomes a call of the local's
    value.
  - The type checker sees the program before splitting, so it doesn't warn.
- **Trigger:**

  ```lisp
  (defun worker (xs)
      (let k (recv ((:scale length) length)))
      (pprint (* k (length xs))))
  (let w (spawn (worker (list 1 2 3))))
  (send w (list :scale 10))
  ```

  It should print `30`. Instead, `length` in the last line is the 10 from
  the message, and the call faults `:not-a-function`. Names that commonly
  appear in patterns (`str`, `sum`, `first`, `length`) hit this.

  It also accepts a program that is wrong:
  `(let v (recv ((:a x) x))) (pprint x)`, with no outer `x`, compiles and
  prints the message's `x` instead of failing with an unknown name.
- **Not affected:** `recv`s with more than one clause, and nested `recv`s,
  where the rest becomes a function of its own.
- **Fix:** take the shortcut only when every name the pattern binds that
  the rest uses is in `live` (it is then rebound). Otherwise take the
  continuation path. Add tests to `split.test.ts`, which has only 11, none
  of them on a pattern's scope across a `recv`.

### H3. Heap cap and fault isolation bypassed by off-heap work

- **Severity:** High. **Area:** runtime.
- The design promises that a process that uses too much memory faults
  `:heap` alone (D202, D233), and that a fault ends only its own process.
  Several paths do large amounts of work outside the cap, so a single
  process can end the whole program (OOM, or `exit(2)` from `rt_must`), or
  stall an island.

**(a) Message, fork, join and exit-record copies turn shared structure into a tree. Status: ✔.**

- **Where:** `runtime/process.c:544-545` (the comment: "Sharing inside a
  value is not preserved ... as in the BEAM"), `:634-645`
  (`scratch_take` grows the buffer with `rt_must(realloc)`, never checked
  against `heap_max`), and `:732-773` (`copy_walk`).
- **Problem:** a value with 40 levels of sharing becomes 2^40 cells.
  Erlang has the same behaviour, but here nothing caps the copy.
- **Trigger:**

  ```lisp
  (defun double (x n) (if (== n 0) x (double (list x x) (- n 1))))
  (join (fork (double :leaf 40)))
  ```

  The same happens through `send`, or a `spawn` whose expression captures
  the value.
- **Note:** golden test 132 shows the collector keeps sharing (its forwarding
  pointers, `process.c:824`), but no copy path does.

**(b) `eq?` and `hash` walk shared structure as a tree, in C. Status: ◐.**

- **Where:** `runtime/rt.c:348-376` and `:420-445`.
- **Problem:** the same value takes exponential time. The walk can't be
  preempted or killed, so the island's thread stalls, with every process on
  it.

**(c) Text built in C before the cap is checked. Status: ◐.**

- **Where:** `runtime/rt.c:48-56` (`rt_buf_add`), used by `str`,
  `str/join`, `str/replace-all` (`strings.c:502-525`), `tty/write`, and the
  rendering of faults and dead letters.
- **Problem:** the whole result is built in malloc before `rt_new_string`
  checks the cap.
- **Trigger:**

  ```lisp
  (let s (str/repeat "a" 1000000)) (str/replace-all s "" s)   ; about 1 TB
  ```

**Fix:**
- Count copy scratch and `rt_buf` growth against the cap (the sender's, or
  the receiver's) and fault `:heap` past it.
- Or keep sharing when copying, with forwarding, as the collector does.
- Give `eq?` and `hash` a visited set, or a budget that yields or faults.

### H4. The HTTP and RPC servers are easy to take down

- **Severity:** High for anything that faces a network. **Area:** net.

**(a) No read or idle timeouts. Status: ✔.**

- **Where:** `lib/http.slight:237-263` (`http/conn` and `http/head` wait in
  `recv` with no timer); `lib/rpc.slight:324` (◐).
- **Problem:** a client that connects and sends nothing, or a byte at a
  time, holds its socket and its process forever. A close also waits forever
  while a reply is still unwritten.

**(b) About 1020 descriptors for the whole program. Status: ✔.**

- **Where:** `runtime/replay.c:499-509` and `:779-783`.
- **Problem:** the event loop uses `select()`, so `rt_io_setup` and
  `runtime_end` refuse any descriptor at `FD_SETSIZE` (1024) or above. The
  limit covers every island and every kind of descriptor: accepted
  connections, outgoing connections, DNS sockets and `:exec` pipes.
- **Impact:** once about 1000 idle connections exist, nothing new can open
  anywhere in the program, so an agent's model calls and tools fail too.

**(c) Output waiting to be written grows without limit. Status: ◐.**

- **Where:** `runtime/process.c:3654` (`queue`), `:4031` (`wants`).
- **Problem:** the queue is in malloc, outside the heap cap. A connection
  keeps reading input whatever its backlog.
- **Trigger:** an RPC peer that never reads but keeps sending
  `(call N help)`, or a stream of bad data, which `barf` echoes back.

**(d) EMFILE spin. Status: ◐.**

- **Where:** `accept_all`.
- **Problem:** at `ulimit -n 1024`, `accept` fails with EMFILE and
  `accept_all` returns. The listener stays readable, so `select()` returns
  at once, again and again, at 100% CPU.

**(e) No limit across connections. Status: ◐.**

- **Where:** `process.c:3509`, `:3586`.
- **Problem:** each connection may buffer a line or datum of up to 64 MB
  before it's `:too-big`, plus an 8 MB body. Nothing limits the total.

**Trigger:** about 1000 idle `nc host 8080` connections.

**Fix:**
- Head, body and idle timeouts in `http/serve` and `rpc/serve` (`after`
  plus a clause in each wait).
- A limit on connections, and backpressure: stop reading while the output
  queue is over a bound.
- On EMFILE, stop watching the listener for a while.
- Move from `select()` to `poll()` or `epoll`/`kqueue`.
- Until then, run these servers only behind a reverse proxy that enforces
  timeouts and connection limits.

### H5. CR/LF injection when building HTTP requests and responses

- **Severity:** High where a URL, header or status comes from input.
  **Area:** net. **Status:** ✔ verified.
- **Where:** `lib/http.slight:556-572` (`http/request-text`,
  `http/response-text`, `http/header-lines`), and `:600-609`.
- **Problem:** the method, request target, header names, header values and
  a handler's status are put into the text as they are. Nothing rejects
  CR, LF or spaces.
- **Trigger:** `(http/get (str "http://api/x?q=" input))`, with `input`:

  ```
  "a HTTP/1.1\r\nHost: api\r\n\r\nGET /admin HTTP/1.1\r\nX: "
  ```

  This sends a complete second request, without `Connection: close`. On
  the server side, a handler that puts a decoded query value in `Location`
  lets a client add response headers of its own.
- **Fix:**
  - Reject CR and LF in every part of a request or response.
  - Reject spaces in the method and target, and anything but token
    characters in header names.
  - Require a status of three digits.
  - Percent-encode the target or refuse it.

### H6. Heap address leak through `#int(...)` and the RPC echo

- **Severity:** High (it defeats ASLR from the network). **Area:** net.
  **Status:** ✔ for the parse; the RPC path is ✔ for the echo, ◐ for the
  read mode.
- **Where:**
  - `runtime/sexp.c:115-117` (`count_digit`) and `:106-109` (`token_ok`);
  - `:405-436` (`token`, which makes a float of an integer that doesn't
    fit);
  - `:463-473` (`new_array`, which stores `rt_int_value(x)` without
    checking that `x` is an integer);
  - `lib/rpc.slight:382-383`, which echoes any unknown message:
    `(barf (bad-message msg))`. Bad arguments are echoed too (`:496`).
- **Problem:**
  1. `count_digit` multiplies `mag` by 10 while `mag ≤ 2^62`. For
     `20000000000000000000`, the 20th digit takes `2e18 * 10` past
     `2^64`, and the result wraps to about `1.55e18`, which `token_ok`
     accepts as fitting in 63 bits.
  2. The builder's own check sees that the number doesn't fit and makes a
     boxed float.
  3. `new_array` (type `#int`) stores that tagged pointer shifted right by
     one bit: the heap address, recoverable exactly.
- **Trigger:** `printf '#int(20000000000000000000)\n' | nc host 7317`
  against an `rpc/listen` server prints the address back. Locally,
  `(sexp/parse "#int(20000000000000000000)")` gives `(:ok #int(<address>))`.
- **Fix:** stop accumulating (saturate) once `mag` passes `2^62`. In
  `new_array`, check that each value is an integer and refuse otherwise.
  Golden test 320 checks only `2^62` exactly; add `2^64` and `10^20`.

---

## Medium

### M1. A device failure inside an inbox batch abandons the batch

- **Severity:** Medium. **Area:** runtime (islands).
- **Status:** ◐ traced by a reviewer. I verified the key steps:
  `take_inbox` has no guard, and `fail()` calls `rt_end` when the owner is
  running.
- **Where:** `runtime/process.c:2425-2452` (`take_inbox`), `:3318-3334`
  (`fail`), `:2548-2560` (`rt_preempt` calls `events()`).
- **Problem:**
  - `take_inbox` detaches the island's whole inbox list, then handles each
    item. It decrements `busy` only after the loop.
  - A message to a device goes to `command()`. On a write or read error that
    calls `fail()`. If the device's owner is an actor and is `rt_current`,
    `fail()` calls `rt_end`, which doesn't return.
  - The owner is `rt_current` exactly when the batch is taken in its own
    `rt_preempt`.
- **What's lost:**
  - The rest of the batch: messages, kills, and `spawn/on` starts and their
    replies, so a spawner waits forever. End notices too.
  - `busy` never gets back to 0, so the program never ends, and no deadlock
    is reported.
- **Related:**
  - A kill earlier in the batch leaves `isle->killed` set
    (`process.c:2389`), so the next process to end a slice on that island
    is ended `:killed`.
  - `ring()` leaks the timer's message on this path.
- **Trigger:**
  1. An actor on island 1 opens `(connect :fs/write "/dev/full" ...)`.
  2. It sends the device's pid to an actor on island 2, then computes for a
     long time.
  3. Island 2 sends `(:write "x")` to the device. ENOSPC follows, then
     `fail`, then `rt_end` inside the batch, and the program hangs.

  `(:read 100000000)` sent to a TCP device does the same through `cut()`'s
  `:too-big`.
- **Fix:** do what `socket_events` (`process.c:4170-4197`) already does for
  this hazard. Note that the running process is to end, finish the batch,
  fix `busy`, then end it.

### M2. Island wake pipe never checked against `FD_SETSIZE`

- **Severity:** Medium. **Area:** runtime. **Status:** ✔ verified.
- **Where:** `runtime/process.c:1286-1295` (`open_island`: plain `pipe()`
  and no limit check), and `:2900-2901` (`FD_SET(my.wake_fd, &in)`,
  unchecked).
- **Problem:** an island is started lazily, on the first message to it. If
  descriptors 0 to 1023 are in use by then (H4):
  - with a descriptor limit above 1024, the pipe gets a descriptor of 1024
    or more, and `FD_SET` and `select()` write past the 128-byte `fd_set`
    on the stack;
  - with a limit of 1024, `pipe()` fails and the program exits with
    status 2.
- **Fix:** open every island's wake pipe at startup, check them against
  `FD_SETSIZE`, or move to `poll()`.

### M3. Names like `toString`, `valueOf` or `constructor` crash the compiler

- **Severity:** Medium. **Area:** compiler. **Status:** ✔ verified for
  `C_BUILTINS`; ◐ for the other tables.
- **Where:**
  - `compiler/src/codegen.ts:547` (`C_BUILTINS[name]`) and `:1016` (its
    definition, a plain object literal);
  - likewise `TREE_OPS`, `TREE_UNARY`, `CONSTANTS`, `ARITH`, `BITS` and
    `SOURCES` (`codegen.ts:431`, `:740`, `:1275`, `:1490`);
  - in `check.ts:332-335`, the tables `own`, `calls`, `fns` and
    `CONSTANTS`.
- **Problem:** the tables are read without `Object.hasOwn`, so a name that
  `Object.prototype` has (`toString`, `valueOf`, `constructor`,
  `hasOwnProperty`, `isPrototypeOf`, `propertyIsEnumerable`,
  `toLocaleString`, `__proto__`) is found as a builtin.
  - `compileCBuiltin` then gets `Object.prototype.toString`, `b.max` is
    undefined, and `Array(NaN)` throws a RangeError.
  - The driver rethrows anything that isn't a `CompileError`, so the user
    sees a Node stack trace.
  - Used as a value, such a name crashes `notYet` instead.
  - In an actor's state function, `mailboxesOf` crashes the checker before
    code generation.
  - `.sly` keeps camelCase names as they are, so these names are likely
    there.
- **Trigger:** `(defun toString (n) (str "#" n)) (pprint (toString 5))`
- **Fix:** use `Map`, or `Object.create(null)`, or `Object.hasOwn`, for
  every table read with a program's names.

### M4. Stack mappings can exceed the kernel's limit

- **Severity:** Medium. **Area:** runtime. **Status:** plausible (the code
  is ✔; the kernel limit wasn't tested).
- **Where:** `runtime/process.c:228-240` (`get_stack`).
- **Problem:**
  - Each stack is its own `mmap` of a page plus `STACK_BYTES` (8 MB), with
    a guard page made by `mprotect`, so two kernel mappings each.
  - It isn't `MAP_NORESERVE`, and it goes back to the island's free list,
    never to the OS.
  - Processes that are preempted, joining, sleeping or reading keep their
    stacks. Only a process waiting in `recv` gives its stack back.
  - About 32K such processes at once pass Linux's default
    `vm.max_map_count` of 65530. Some distributions raise it to about 1M.
  - Under `vm.overcommit_memory=2`, far fewer are enough.
  - A failure `exit(2)`s the whole program.
- **Trigger:** forking 40,000 tasks that are joined later, for example
  `(map join (map (lambda (_) (fork (fib 20))) (range 0 40000)))`.
- **Fix:**
  - `MAP_NORESERVE`, and smaller stacks, or stacks that grow.
  - Fault the process that spawns, rather than exiting.
  - Unmap pooled stacks above a threshold.
  - Document the ceiling.

### M5. Task addresses cut the pid to 32 bits

- **Severity:** Medium (long-running programs). **Area:** runtime.
  **Status:** ✔ verified.
- **Where:**
  - `runtime/rt.h:556-560`: `RT_ADDRESS_SHIFT` is 35, and
    `rt_address_task` masks with `0xffffffff`;
  - `runtime/process.c:1680-1693`: an address is
    `RT_ADDRESS_BIT | num << RT_ADDRESS_SHIFT | p->pid`, and nothing masks
    the pid;
  - `rt.h:547-549`: pid numbers are 52-bit and never reused.
- **Problem:** after 2^32 pids on one island:
  - a newer task's own address no longer names it, so `read` faults
    `:not-a-pid`, and `ask`, `http/request` and `rpc/call` break;
  - messages to such addresses are dropped, or delivered to the live task
    whose pid is the same modulo 2^32;
  - the pid's high bits also collide with the address number.
- **Trigger:** a server or simulation that makes about 4.3 billion
  processes on one island over its life.
- **Fix:** widen the field or reuse pids. At the least, fault when pids run
  past what an address can hold.

### M6. curl run with unsafe defaults

- **Severity:** Medium for the agent tools. **Area:** net/agent.
  **Status:** ✔ for the arguments; the effects are plausible (they're
  curl's documented behaviour).
- **Where:** `lib/tools.slight:265-266` (`tools/get`, the agent's `fetch`),
  and `lib/http.slight:163-168` (`http/curl-command`, for `https://`).
- **Problem:** neither passes `-q` (as the first argument), `--globoff` or
  `--max-redirs 0`. So:
  - a `~/.curlrc` with `location` turns redirect-following back on. That
    defeats D189's rule that a page can't send a fetch where the person
    didn't agree to;
  - curl expands URL patterns, so one approved
    `https://x/[1-999999]` makes 999,999 requests. Each has a 20 s limit,
    but `exec/run` has no overall limit;
  - ordinary URLs containing `[` or `]` (`?filter[a]=b`) fail with exit
    code 3 over https, though they work over plain http.
- **Already right:** `--` comes before the URL, and `--proto` keeps it to
  http and https.
- **Fix:** add `-q` first, then `--globoff` and `--max-redirs 0`.
  Consider `--max-time` per run, and a check on `%{num_redirects}`.

### M7. Close-on-exec set after creating a descriptor

- **Severity:** Medium. **Area:** runtime. **Status:** ✔ for sockets
  (`replay.c:494-509`); ◐ for accepts and pipes (`:588`, `:765`).
- **Where:** `runtime/replay.c:494-509` (`socket()`, then a separate
  `fcntl(FD_CLOEXEC)` in `rt_io_setup`), and the same for `accept` and
  `pipe`. The spawn is at `:813`; `process.c:4295` holds a lock around the
  spawn only.
- **Problem:** islands are threads, so `posix_spawn` on one island can run
  between another island's `socket()` and its `fcntl`. The child inherits
  the descriptor. Then:
  - a command's stdin never closes, so `exec/run` hangs;
  - a peer doesn't see a connection close;
  - a shell command the model runs holds a live socket.
- **Fix:** create descriptors close-on-exec from the start:
  `SOCK_CLOEXEC | SOCK_NONBLOCK`, `accept4`, `pipe2(O_CLOEXEC)`,
  `O_CLOEXEC`. On macOS, which lacks some of these, take the spawn lock
  around creation too.

---

## Low

### L1. `timer/cancel` doesn't check who owns the timer

- **Status:** ✔ verified.
- **Where:** `runtime/process.c:2756-2764`.
- **Problem:** timer numbers are sequential on each island (`++my.afters`),
  and cancelling checks only that the number is live. Any process can
  cancel another's `after` timers, and a number got on another island
  cancels an unrelated timer.
- **Trigger:** `(timer/cancel 1)`.
- **Fix:** keep each timer's owner, and cancel only the caller's.

### L2. `fs/delete-all` follows a symlink swapped in during the walk

- **Status:** ✔ verified.
- **Where:** `lib/fs.slight`, `fs/delete-all`, `fs/delete-in` and
  `fs/delete-each`.
- **Problem:** `fs/stat` (lstat), then `fs/list` and `fs/delete` by joined
  path strings, with no `openat` and no no-follow. This is the classic
  `rm -r` race.
- **Trigger:** a privileged program deletes `/tmp/build`. An attacker
  replaces `build/sub` with a symlink to `/etc` between the stat and the
  listing, and `/etc/*` is deleted.
- **Fix:** a C builtin that uses `openat`, `fstatat(AT_SYMLINK_NOFOLLOW)`
  and `unlinkat`. Or document that it isn't safe on directories others can
  write.

### L3. HTTP parsing is quadratic

- **Status:** ✔ for the head; ◐ for chunk sizes, chunk ends and trailers.
- **Where:** `lib/http.slight:246-263`.
- **Problem:** each chunk is joined to the buffer (`(~ buf s)`), and the
  whole buffer is searched again from the start. Each line is then cut off
  with a copy (`bytes/slice`).
- **Impact:**
  - a 64 KB head sent a byte at a time costs about 2 GB of copying and
    scanning, on the thread that serves every other connection;
  - a 64 KB head of about 20,000 tiny lines costs about 700 MB.
- **Fix:** search only the new bytes, and keep an offset rather than
  slicing.

### L4. Agent read tools need no approval and aren't confined

- **Status:** ◐; partly by design (D188).
- **Where:** `lib/tools.slight:119-127` and around.
- **Problem:**
  - `read_file`, `list_files` and `search` need no approval and aren't kept
    to a directory, so a model can read `~/.ssh` or `/proc/self/environ`.
    D188 leaves confining them for later.
  - `read_file` on a FIFO or `/dev/tty` blocks in `open()` or `read()`, and
    stalls the runtime.
  - The approval display (`tools/printable`) escapes control and bidi
    characters, but not U+200E, U+200F or U+061C.
- **Fix:** an optional root directory for the tools; open with
  `O_NONBLOCK` and refuse anything but regular files; escape every
  `Bidi_Control` character.

### L5. Replay records hold secrets, and `SLIGHT_RECORD` is inherited

- **Status:** ✔ verified.
- **Where:** `runtime/replay.c:813` (`posix_spawn(..., environ)`).
- **Problem:**
  - A record holds the whole environment, every byte read, and
    `crypto/random-bytes` output.
  - It is created 0600, with `O_NOFOLLOW` and `fchmod`, which is good.
  - But `SLIGHT_RECORD` passes to child processes. A slight program run
    through `:exec` opens the same record file and truncates the parent's
    recording.
- **Fix:** remove `SLIGHT_RECORD` and `SLIGHT_REPLAY` from a child's
  environment. Document that records are secrets.

### L6. HTTP framing laxness

- **Status:** ✔ for Transfer-Encoding; ◐ for the rest.
- **Where:** `lib/http.slight:334-336` and `:428`.
- **Problem:**
  - `http/chunked?` checks for `chunked` anywhere in Transfer-Encoding, by
    substring.
  - The first of duplicate headers wins.
  - Content-Length isn't checked against Transfer-Encoding.
  - The line after a chunk isn't checked (`:428`).
- **Why low:** every connection serves exactly one request and then closes.
  This matters only behind a proxy that frames requests differently.
- **Fix:** RFC 9112's rules. Refuse a message with both headers, refuse
  duplicate Content-Length values, and require `chunked` to be the last
  coding.

### L7. DNS takes any A record; no IPv6 anywhere

- **Status:** ✔ verified.
- **Where:** `runtime/dns.c:263-276`.
- **Problem:** the question section is skipped, not compared with the
  question asked. The first A record is taken, whatever name it is for.
- **Already right:** IDs come from `getentropy`, the UDP socket is
  connected, replies are bounds-checked, and compression pointers can't
  loop.
- **IPv6:** there is none in the runtime. No `AF_INET6`, no AAAA queries,
  and lookups return a `uint32_t` address. IPv6-only hosts and networks
  can't be reached.
- **Fix:** compare the question with the one asked, and follow the CNAME
  chain to the name's own A record. Plan for IPv6.

### L8. C buffers leak when a builtin faults after allocating them

- **Status:** ✔ for `buf_to_string`; ◐ for the others.
- **Where:** `runtime/strings.c:51-55` (`buf_to_string`: if
  `rt_new_string` faults `:heap`, `rt_buf_free` never runs); `bytes/pack`
  (`strings.c:841-895`, on a fault in a spec or value); `bytes->base64`
  and the hex builtins (`strings.c:973` on).
- **Impact:** each fault leaks memory outside every cap, so a server that
  faults often grows without bound.
- **Fix:** register the buffer for release on fault, or check sizes before
  allocating.

### L9. Data race on the plain global `input_seen`

- **Status:** ✔ that the variable is a plain global written there; ◐ that
  every island reaches the write.
- **Where:** `runtime/process.c:2831` and `:2956-2957`.
- **Problem:** `input_seen` is a plain `static`, not per island. `events()`
  writes it on every island that has sockets, on the real clock. That is
  undefined behaviour, and one island's polling interferes with another's.
- **Why it isn't caught:** `make tsan` runs only on the virtual clock.
- **Fix:** move it into the island's state, and run TSan on the real
  clock too.

### L10. `.sly` raw names can contain spaces

- **Status:** ✔ verified.
- **Where:** `compiler/src/sly.ts:136-140`. A raw name is anything between
  backticks but a newline.
- **Problem:** hygiene in five files rests on "a name with a space can't be
  written": `split.ts`'s `alias`, `pattern.ts`'s `BUILTIN` and
  `ON_CONSES`, `expand.ts`'s topic names, `modules.ts`'s private names, and
  `TUPLE`. So:
  - in a `.sly` file that includes `:map`,
    `` `map/bit (in lib/map.slight)`(...) `` calls the map's private
    helper;
  - an outer raw local with a space in it crosses a `recv` without an
    alias, so a pattern of the same name hides it.
- **Fix:** reject spaces and parentheses in raw names. Better, generate
  names from one facility that no reader can produce.

### L11. `x |> fun(y) => ...` makes a closure instead of calling it

- **Status:** ✔ verified.
- **Where:** `compiler/src/sly.ts:564-570` (`binary`).
- **Problem:** any pair on the right gets the left side appended. A lambda
  becomes `(lambda (y) (+ y 1) x)`, a function whose body ends with `x`,
  and nothing calls it. `x |> [f]` and `x |> #(a)` likewise quietly become
  a list and a tuple. The error message says "the right of `|>` is a
  function or a call", so users will expect a lambda to work.
- **Fix:** turn a lambda on the right into a call, `((lambda ...) x)`, and
  refuse lists and tuples.

### L12. `case` tests with an `eq?` that a local can hide

- **Status:** ✔ verified.
- **Where:** `compiler/src/expand.ts:324`.
- **Problem:** `case` builds its tests with `sym('eq?')`, where
  `pattern.ts` uses an `eq?` no local can hide.
- **Trigger:** `(defun f (eq? x) (case x (1 :one) (#true :no)))` calls the
  parameter.
- **Fix:** use the same hidden builtin name as `pattern.ts`.

### L13. More-than-8-locals packing checks only some names for `cons`

- **Status:** ✔ verified.
- **Where:** `compiler/src/split.ts:396-399` (`bundle`).
- **Problem:** when more than 8 locals cross a `recv`, they're packed with
  `cons`. The check that no local is named `cons` looks at the names in the
  environment only, not at a `(let cons ...)` earlier in the clause's body.
- **Fix:** pack with a builtin name no local can hide, as `pattern.ts`
  does.

### L14. `(defun f)` crashes the type checker

- **Status:** ◐.
- **Where:** `compiler/src/check.ts:138`.
- **Problem:** `paramNames(lowered[2]!)` with no parameter list throws a
  TypeError, which comes out as a stack trace, not a `CompileError`.
- **Fix:** check a `defun`'s shape before reading it, or in `expand`.

### L15. Driver problems (`bin/slightc.ts`)

- **Status:** ✔ unless noted.
- **Exit codes:** anything thrown that isn't a `CompileError` comes out as
  a Node stack trace, with exit code 1 (`:214-216`). That covers the
  compiler's own bugs (M3, L14), and `-o` into a missing directory (◐). The
  usage message says 2 for toolchain failures.
- **Option-like names:** `-o -x` is accepted, and `-x.S` reaches clang as an
  option. There's no `--`, so an input file whose name starts with `-` can't
  be given (`:117-122`, `:225-229`).
- **Cache inside the install:** the runtime's object files go in
  `build/runtime/` inside the compiler's own directory (`:55`, `:166`). A
  read-only install (a system prefix, Nix, a container layer) fails at the
  first compile.
- **Stale cache key:** the key hashes the compiler *command* and flags
  (`:160`), not the compiler's version. Upgrading clang in place reuses
  objects it built.
- **Default target:** the target is AArch64 whatever the host (`:190`), so
  on x86-64 the result needs qemu unless given `--target x86_64`. It's
  documented, but surprising.
- **Leftover file:** `${out}.S` is left beside every binary (`:225-226`).
- **Already right:** tools are run with `spawnSync` and argument arrays,
  never a shell. The runtime is built in a `mkdtemp` directory and renamed
  into place.

---

## Nits

- **N1. The README is out of date.** `README.md:43` says "The runtime
  schedules processes on one core", but islands (a thread per core, D256)
  are built. `README.md:90` says "Next is `:exec`", which is done (D185),
  with much more after it.
- **N2. Leap seconds.** `lib/time.slight`'s `time/parse-iso` refuses second
  60, which RFC 3339 allows. This is consistent with the file's stated
  "POSIX time, so no leap seconds", but worth documenting on the parser.
- **N3. Size and hygiene.**
  - `runtime/process.c` is one 5,300-line file covering the scheduler,
    collector, islands, devices and `:system`. Invariants that cut across
    it stay implicit (M1 is the clearest example).
  - `compiler/src/codegen.ts` is 1,784 lines, and `check.ts` 1,168.
  - About 50 lines are over 140 columns, up to 192.
  - Generated names are kept apart by a convention repeated in five files,
    rather than by one facility. That convention is the root of H2, L10,
    L12 and L13.
  - Size arithmetic is checked in some places and not others (H1).

---

## Project-level issues, for adoption

- **P1. Maturity.**
  - The first commit was on 2026-10-03: 348 commits in 30 days.
  - 285 commits are by Claude (an AI coding agent), 62 by Stevan Little,
    and 1 by an outside contributor (Yves Orton).
  - There is one human maintainer.
- **P2. No releases and no CI.**
  - No tags, releases or version number.
  - No CI configuration in the repository (no `.github/`).
  - Tests are run by hand: CLAUDE.md says on the maintainer's M2 after
    every step, and in cloud sessions.
- **P3. The language is still moving.** For example, D260 changed the value
  tags and reverted them the same day. `README.md` says self-hosting
  "waits until the language settles".
- **P4. Gaps, by the project's own list** (`docs/PLAN.md:1170-1250`,
  "Toward general purpose", D249):
  - a standard library: `format`, CSV, `@ARGV` parsing, sets, sorted maps,
    regular expressions;
  - supervisors and a process registry;
  - a formatter, a language server, a debugger and a profiler;
  - packages and dependencies;
  - "a version number and a core that holds still for it".
- **P5. Built-in limits.**
  - Integers are 63-bit, with no bignums; overflow faults.
  - A function takes at most 8 parameters (`values.ts:8`,
    `docs/LANGUAGE.md:372`).
  - No FFI, by policy (D252). TLS only by running curl (D251).
  - IPv4 only (L7). About 1020 descriptors per program (H4).
  - Linux and macOS only.
  - The compiler needs Node 22.18 or later, plus clang. Cross targets need
    lld, a cross libc and qemu.
- **P6. Tests check correct input, not hostile input.** There are 305
  golden programs on both targets, 271 unit tests, 12 checker golden tests,
  and the replay and live checks. But:
  - there's no fuzzing, and no ASan or UBSan;
  - TSan is opt-in, and runs on the virtual clock only;
  - nothing tests size arithmetic at the wrap boundaries, shared values in
    messages, a device failing from another island, tens of thousands of
    live stacks, slow or abusive clients, descriptor exhaustion, or CR/LF
    in headers;
  - https is tested only against a stand-in for curl;
  - `split.test.ts`, on the riskiest pass, has 11 tests.
- **P7. Licence.** Apache-2.0 WITH LLVM-exception, which is fine; the
  exception covers the runtime compiled into programs. But programs also
  embed CORE-MATH (MIT) and tables made from the Unicode Character Database
  (Unicode License v3), whose notices must go with a binary you
  distribute. Versions before D264 are MIT-0.

---

## Checked and found sound

These were looked at specifically and hold up:

- **Parsers:** the JSON and s-expression validators and builders are loops
  that keep their depth on the heap, against the cap. Deep nesting can't
  overflow the C stack. Escapes are handled correctly, and the builder sees
  only text the validator passed (apart from H6's digit count). JSON is
  tested against JSONTestSuite.
- **Crypto:** SHA-256 and SHA-512 padding and length fields are right, and
  so is HMAC. `crypto/equal?` is constant-time, with `volatile`. Random
  bytes come from `getentropy`.
- **Commands:** `:exec` uses `posix_spawn` with an argument list, never a
  shell. `ls`, `grep` and curl get `--` before paths and URLs.
- **Signals and I/O loops:**
  - SIGPIPE is ignored, and children get it back.
  - EINTR is retried, and partial writes are finished.
  - Signals are handled on island 0, on an alternate stack.
  - A stale Unix socket is replaced only if `lstat` says it's a socket and
    connecting is refused.
- **Descriptors:** every socket, DNS socket and command pipe is checked
  against `FD_SETSIZE`, apart from the wake pipe (M2).
- **Collector:** "no write barrier" holds:
  - no collection runs inside C;
  - builders touch only young objects;
  - adopted chunks point only into themselves or static data;
  - exit records, mailboxes and timers are in chunks of their own.
  Forwarding and the sweep of handles are right in both generations.
- **Shared buffers:** reference counts are `_Atomic`, and buffers are
  published through the inbox mutex.
- **Process table:** freeing pages never invalidates an entry in use, and
  pids can't be forged.
- **Context switches:** the full callee-saved set is saved on both ABIs.
  16-byte alignment is kept, and nothing relies on the red zone across a
  switch.
- **Code generation:**
  - `+`, `-` and `*` detect overflow.
  - `div` of −2^62 by −1 faults.
  - Division by zero faults, floats included.
  - `bit/` shift counts are checked.
  - Comparisons of ints with floats agree with `rt_compare`, NaN included.
  - Float trees allocate before any value is in a float register.
  - `placeArgs` handles register cycles.
  - Tail calls load arguments before taking the frame down.
  - Every entry point checks the stack and reductions.
  - No value stays in a register across a call.
  - The two targets agree.
- **The `recv` rule:** enforced as documented. State functions can only be
  tail-called, can't be values, and can't be called from lambdas or tasks.
- **Builtin bounds:** `arrays.c`, `tuples.c` and `numbers.c` check indices,
  counts and overflow (`mul_overflow` in `array/matmul`).
- **Libraries looked at by hand:** `lib/map.slight` keeps its trie's shape
  right through puts and deletes. `lib/time.slight` uses Hinnant's
  algorithms correctly (apart from N2).
