# Fixing the review of 2026-11-02: the plan

**Status (2026-11-04):** steps 1 to 3 are done. In step 4, A, E and F
are settled:
- **A** is built as recommended (D265, golden tests 339 and 340).
  - What a process ends with is checked on its first copy, not by a copy
    of its own, which made a fork and join of lists 30 per cent slower.
  - A fault whose reason holds the value ends `:heap` too.
- **E** stays as D257 has it (D266).
- **F** is built, with `:stack` (D267, `t/live.py`'s stacks).
- **B** is documented only (D268): only a program's own code makes a
  value with sharing.
- **C**, timeouts, isn't taken (D269): a server facing the internet goes
  behind a proxy, as LANGUAGE.md now says.
- **D** is built (D270), golden tests 342 and 343, and 278 extended:
  - output waiting for a socket or a command fails the device
    `:too-big` when more comes while 64 MB waits, so a single big reply
    still goes;
  - a write whose text would pass the heap fails too;
  - `barf` echoes only what prints in 1 KB;
  - `(:most n)` in the new `http/serve-with`, `http/listen-with`,
    `rpc/serve-with` and `rpc/listen-with`.
- **H5** is built (D271, golden test 341): requests refused as Results,
  a handler's bad response a 500 with a warning.
- **G** is built (D273): an address is an id its island gives once, and
  a table of the island's open ones says whose it is, so neither a pid
  past 2^32 nor a wrapped number can misdeliver. It prints
  `#<address 7>`. Golden test 324 is now `324-address-ids`.
- **J** is built (D272), golden tests 344 to 346: a file that isn't a
  regular one is opened `O_NONBLOCK` and polled, read and written as a
  socket is; a FIFO opened to write with no reader fails `:enxio` at
  once. macOS's FIFO with no writer yet, and its `/dev/stdin`, are to
  be checked on the M2.

The rest of step 4 waits for Stevan. Step 5 is begun: CLAUDE.md's
status says what's done.

- **Step 1**, `b0b69d1` to `8e4a0be`, a commit an item. Golden tests 336
  to 338, t/320 extended, and `t/live.py`'s wakes, inherit, accept and
  faults. `t/replay.sh` checks a command's environment, and `make tsan`
  runs `t/data/tsan-sockets.slight` on the real clock. Where it went
  past the plan:
  - `bytes->base64`, `bytes->hex` and their inverses make their string
    first and fill it, so they use no malloc at all.
  - `new_array` aborts on a non-integer in an `#int`, a backstop the
    validator no longer reaches, since `sexp/parse` ignores the build's
    failure.
  - Timers' device commands (`ring()`) wait for the end too, which ends
    the leak of the timer's message.
  - A program with more than about 1,000 descriptors open before its
    first `spawn/on` now exits 2 with a message, where it overran the
    stack before. 4E would lift that.
- **Step 2**, `ba17904` to `7413117`. Golden tests 350 to 353,
  `t/types/` 013 and 014, and `compiler/tests/driver.test.ts`.
  - The refactor's assembly is byte-identical for all 388 programs.
  - The `case` and `match` topic names, and `split.ts`'s temporaries
    that are wholly in parentheses, keep their own shapes, which are
    hidden as well.
  - `|>` refuses a `lambda` form too, as in `x |> (fun(y) => ...)`.
  - The driver also refuses a directory as input, says `couldn't write`
    for a missing output directory, and can now be unit-tested.
  - A local may now be named `car`, `cdr` or `cons` with more than 8
    locals across a `recv`, which was an error.
- **Step 3**, `333718e` to `ceddbe0`. Golden tests 365 and 366, with
  207, 210, 186, 251 and 309 extended.
  - The head and chunk readers keep an offset into the last chunk.
  - A reply to HEAD, or a 1xx, 204 or 304, has no body whatever its head
    says (RFC 9112, 6.3, rule 1). Before, one marked chunked waited for
    a body.
  - Content-Length must be digits alone.
  - A reply with both Content-Length and Transfer-Encoding isn't
    refused: TE wins, as the RFC says. Only a request gets the 400.

Found on the way, for later:
- A body streamed as `:lines` (`http/take`) still joins its chunks, up to
  its 64 KB line.
- The reader recurses, so about 100,000 nested parentheses run Node out
  of stack. That's now `slightc: internal error`, not a crash.
- `rt_io_fopen` (the hosts file and `resolv.conf`) opens without
  close-on-exec. M7's fix covers commands either way.

The review is [`a64-slight-review-2026-11-02.md`](a64-slight-review-2026-11-02.md).
Every finding in it was checked again on 2026-11-03, against the code at
`d62f792` (the review's `5699be7` plus the review). The checks were in
three parts: the compiler and driver, the runtime, and the network,
libraries and agent tools. Each part re-read the code, ran a probe where
one was cheap (x86-64, natively), and looked in DECISIONS.md for what had
been decided already. Nothing in the repo was changed.

The plan has five steps:

- **Steps 1 to 3** fix bugs. None of them changes what the language
  means, so they need no decision. They are in order of harm: memory
  corruption and crashes first.
- **Step 4** is the points that do need Stevan: a new fault, a new API,
  or a change to something decided. Each has options and a
  recommendation, and becomes a D-number (from D265) once it's settled.
- **Step 5** is documents and the project.

As always, each step ends with `make test` passing on both targets, and
adds golden tests. Expected output is worked out by hand or by a model,
never copied from what the compiler printed.

## What the checks changed

Most findings stand as written. These don't.

**Worse than the review says:**

- **L10.** A raw `.sly` name can reach the compiler's hidden helpers, and
  some of those don't check their argument. `` `car (on conses)`(5) `` and
  `` `tuple/get (pattern)`(5, 3) `` segfault (exit 139). That's memory
  safety from user code, so L10 moves into step 2's first fixes.
- **M1, M4 and H4(d) were reproduced.** The review had them only traced
  or plausible.
  - M1: the program hangs.
  - M4: 33,000 forked tasks waiting to be joined end the program with
    `rt: can't allocate a stack`, exit 2, at `vm.max_map_count`'s 65,530
    mappings, two to a stack.
  - H4(d): at `ulimit -n 32`, a server used 2 CPU-seconds in 2 s of wall
    time.
- **M2 corrupts memory today.** With 1,100 files open, the wake pipes got
  descriptors 1103 to 1106. `pselect6` was then passed `nfds` 1104, read
  the stack past the `fd_set` as descriptors, and woke island 1 over and
  over.
- **M5 comes much sooner than it sounds.** One island forks and joins
  about 4.2 million tasks a second, so it runs through 2^32 pids in about
  17 minutes. And past the wrap, a reply isn't just lost:
  `address_owner` can deliver it to another live task, the root
  included.
- **L1's real bug is between islands.** Timer numbers are counted per
  island. Cancelling island 1's timer 1 from island 0 cancelled island 0's
  own timer 1, and island 1's timer still fired.
- **L8 leaks about 61 MB a fault** (measured). That's more than Low.
- **H3(c) and the fault message.** Rendering a fault or a dead letter is
  unbounded too. `(+ 1 v)`, with `v` a 30-level shared value, ends the
  whole program with `rt: out of memory`.
- **More crash sites for M3.** Beyond the review's list:
  - `check.ts:401`, `:465`, `:505`, `:507` and `:781`;
  - `types.ts:458` (type names);
  - `codegen.ts:903` (`name in ARITH`, `COMPARE` and `BITS`).
- **L14.** `(defun)` crashes too, not only `(defun f)`.
- **L13.** `split.ts`'s `nth` and `makeFunction` rely on the same
  convention for `car` and `cdr`. They're safe today, only because the
  unpacking comes before a body's `let`s.
- **The FIFO hang (L4) affects every file device.** Any `open` or
  `connect :fs/read` or `:fs/write` of a FIFO blocks its island, not just
  the agent's tools. `fs/walk` has `fs/delete-all`'s symlink race too, as
  a read.

**Less than the review says, or decided:**

- **H6's ASLR claim is wrong.** The binaries are static and not PIE, so
  code and data aren't randomized anyway. What H6 leaks is the brk heap's
  base. It's still a bug: silently wrong data, which RPC echoes. Medium.
- **H3(b).** `eq?` is fast when both sides are the same value. The
  blow-up comes only between two shared values built separately.
- **H4(b), the ~1,000 descriptors, was decided.** D113 chose `select()`,
  D257 left epoll for when a program needs it, and LANGUAGE.md says
  "about 1,000 sockets". Changing it is in step 4.
- **H4(a).** A default server timeout reopens D236. A pending timer
  overtakes loopback replies on the virtual clock, which is why the
  client's 30 s default was withdrawn. So a server timeout has to be
  opt-in. RPC connections are long-lived on purpose (D228), so RPC wants
  deadlines on a first message and on each datum, not an idle timeout.
- **L1's "anyone can cancel" is by design** (D232: "as any can kill any
  process").
- **L6.** Transfer-Encoding winning over Content-Length is RFC 9112
  §6.3's rule. And every connection closes after one response (D173), so
  a desync can't reach another request.
- **L7.** "The first A record, after any CNAMEs" is D165's. IPv6 is
  parked (D139). Checking the question section is still worth doing.
- **L15.** The default target is D146's, which rejected defaulting to the
  host. The leftover `out.S` is documented. The cache inside the install
  is D150's, but its key is stale, against D150's own promise.
- **M4.** `MAP_NORESERVE` wouldn't help (the limit is mappings, and
  overcommit 2 ignores it), so it's dropped from the fix.
- **M6.** The no-redirect rule is D205's (and D251's for https), not
  D189's. The fix restores what they say.
- **P7.** D264's `NOTICE` and README already tell distributors to ship
  the notices. What's missing: the Unicode License's text in the repo,
  and a Unicode line in `runtime/unicode.c`'s header.

## Step 1: memory safety and hangs in the runtime

No decisions needed. Each fix is small, except where noted.

1. **H1.** In `rt_new_shared` (`process.c:490`), test
   `len > heap_max || held(p) > heap_max - len`. Neither sum can wrap
   then, since `heap_max` is bounded, and so neither can the size given
   to `malloc`. Tuples and arrays already cap their counts.
   - Test: `str/repeat` on strings of 1 to 4 bytes, with counts near
     2^62, and `str/pad-start` with a 4-byte fill. Each faults `:heap`.
2. **H3(c) and L8.**
   - `rt_buf_add` checks its growth against
     `heap_max - held(rt_current)`, then frees the buffer and faults
     `:heap`. It needs the site, so add a field to `rt_buf_t`.
   - For `bytes/pack`, `bytes->base64` and the hex builtins: a short
     list, per island, of C buffers to free when a builtin faults.
   - Test: `str/replace-all` past the cap faults `:heap`. `t/live.py`
     checks that 8 such faults leave the peak RSS flat. Size M.
3. **H6 (the parse).**
   - `count_digit` stops multiplying past 2^62 / 10 and saturates.
   - `new_array` refuses anything that isn't an integer in an `#int`
     array.
   - Test: extend t/320 with `#int(18446744073709551616)` and
     `#int(20000000000000000000)`, each `(error (bad-sexp 25))`, and
     `#int(100000000000000000000)`, `(error (bad-sexp 26))`. The offset
     is 5 plus the number of digits, as for 2^62's 24.
4. **M2.**
   - Open every island's wake pipe in `start_island`'s first call, while
     descriptors are low, and check each against `FD_SETSIZE`.
   - Check the two wake-only `select()`s (`:1424`, `:2476`) too.
   - Test: `t/live.py`, with `ulimit -n 4096` and 1,100 files open
     before the first `spawn/on`. The bitmap in step 4E would remove the
     limit itself.
5. **M1.**
   - An island flag, `my.ending`, holds a reason. While `take_inbox`
     runs, `fail()` sets it instead of calling `rt_end`.
   - After the loop and `busy -= n`, `take_inbox` ends the process if
     the flag is set, and clears `my.killed` likewise.
   - Test: a golden test on 4 islands. The trigger must work on macOS,
     which has no `/dev/full`, so use `(:read 100000000)` sent to a TCP
     device from another island, through `cut()`'s `:too-big`.
6. **M7.**
   - In `spawn()` (`replay.c:796-813`), close every descriptor above 2
     in the child: `posix_spawn_file_actions_addclosefrom_np` on glibc
     2.34 or later, `POSIX_SPAWN_CLOEXEC_DEFAULT` on macOS. That covers
     every place a descriptor is created, with no change at each.
   - Test: `t/live.py` (Linux): a command lists `/proc/self/fd` while
     another island opens sockets.
7. **H4(d).**
   - On EMFILE or ENFILE from `accept`, pause the listener for 100 ms on
     a runtime timer (`own_alarm`, as lookups use), and have `wants` skip
     a paused listener.
   - Test: `t/live.py`, checking CPU time under a low `ulimit -n`.
8. **L5.**
   - `posix_spawn` gets an environment without `SLIGHT_RECORD` and
     `SLIGHT_REPLAY`.
   - Test: in `t/replay.sh`, a command's environment doesn't have them.
9. **L9.**
   - `input_seen` becomes `my.input_seen`.
   - `t/tsan.sh` gains a program with sockets on two islands, on the
     real clock.

## Step 2: the compiler and the driver

No decisions needed, but see L11.

1. **One way to make hidden names** (N3), in `sexp.ts`. The convention
   is now in eight places:
   - `pattern.ts`: `BUILTIN`, `ON_CONSES`, `NO_MATCH`, `RECV_MESSAGE`,
     `TUPLE_OF` and `TUPLE_REF`;
   - `sexp.ts`'s `TUPLE`;
   - `expand.ts`'s topic names, and `madeFrom`;
   - `modules.ts`'s `IN` and `PRELUDE_SUFFIX`;
   - `split.ts`, at nine sites.

   Give them three helpers:
   - `writable(name)`, from the reader's own delimiters;
   - `hidden(base, note)`;
   - `builtinSym(name, pos)` and `builtinOf(name)`, which move `BUILTIN`
     out of `pattern.ts`.

   This is a refactor alone. The emitted code is unchanged.
2. **L10.**
   - `sly.ts`'s raw names refuse what `writable` refuses. That's what
     D246 and SYNTAX.md already say a raw name is: "any slight name as it
     is".
   - Test: in `sly.test.ts`, `` `car (on conses)` `` and
     `` `map/bit (in lib/map.slight)` `` are errors.
3. **L12 and L13.**
   - `case` (`expand.ts:324`) tests with `builtinSym('eq?')`, and
     `refine` in `check.ts` sees through it, so `case` still narrows.
   - `bundle`, `nth` and `makeFunction` in `split.ts` use the hidden
     `cons`, `car` and `cdr`. `bundle`'s `tooMany` check goes.
   - Test: `expand.test.ts` #134 changes on purpose. Add golden tests
     for `(defun f (eq? x) (case x ...))` and for 9 locals with
     `(let cons ...)` in a clause.
   - A trial of L12 and L13 (with H2's guard below) passed all 363
     golden tests on x86-64, and `make types`.
4. **H2.**
   - In `split.ts`'s `waitFor`, take the one-clause shortcut only when
     no name the pattern binds is used in the rest, other than the
     `let`'s own and those in `live`. Otherwise take the continuation
     path. A correct program never meets that, so its code is unchanged.
   - Test: `split.test.ts` cases on a pattern's scope across a `recv`.
     Add a golden test where `length` from the pattern prints `30`, and
     another where `(pprint x)` after `(recv ((:a x) x))` is
     `unknown name 'x'`.
5. **M3.**
   - `own(table, key)`, on `Object.hasOwn`, at every lookup keyed by a
     program's name. That's the review's sites and the extra ones above.
   - Test: a golden test that defines `toString`, `valueOf` and
     `constructor`, calls them, passes one as a value, and tail-calls one
     from an actor. Add a `t/types/` test for the checker.
6. **L14.**
   - `checkDefun` and `checkParams` move from `codegen.ts` to
     `expand.ts`'s `defun`, so the checker sees only well-formed
     `defun`s.
   - Test: `expand.test.ts`, for `(defun)` and `(defun f)`.
7. **L11, the bug.**
   - `parseBinary` refuses `fun`, `[` and `#(` after `|>`, saying so.
     `[f]` reads the same as `list(f)`, so the check has to be on the
     token, not the s-expression.
   - Making a lambda there work is 4L.
8. **L15.**
   - Anything thrown that isn't a `CompileError` prints
     `slightc: internal error: ...` and exits 2. That's toolchain-shaped,
     and keeps the documented codes.
   - `-o` refuses a value starting with `-`, and `--` ends the options.
   - The runtime's cache key hashes `cc --version` as well as the
     command, as D150 promises.
   - Test: unit tests on the argument parsing, and a stale-key case.

## Step 3: libraries and the network

No decisions needed. Each restores what a decision already says, or
changes nothing visible.

1. **M6.**
   - `tools/get` and `http/curl-command` put `-q` straight after
     `curl`, then `--globoff --max-redirs 0`.
   - Test: t/309's `/args` expected output changes (worked out by hand).
     Add a golden test with `; env: HOME=t/data/curlrc`, a `.curlrc` that
     says `location`, and a URL with `[` in it, against the real curl as
     235 does.
2. **L3.**
   - `http/head`, `chunk-size`, `chunk-end` and `trailers` search only
     the new bytes (`bytes/index-of` from the old length), and keep an
     offset instead of slicing off each line.
   - Tests 207 and 210 hold the behaviour.
3. **L6.**
   - `chunked` must be the last coding, compared exactly.
   - A 400 for both Content-Length and Transfer-Encoding, and for
     Content-Length values that differ.
   - A chunk's ending line must be empty.
   - Test: cases in 207.
4. **L7 (the check).**
   - QDCOUNT is 1. The question's name matches what was asked, ignoring
     case, and its type and class are 1 and 1.
   - An A record counts only when its owner is the name asked, or the
     end of its CNAME chain.
   - Test: t/186's own name server sends a reply for another name.
5. **L4 (the escapes).** `tools/printable` escapes U+200E, U+200F and
   U+061C too, which completes D205's list.

H5 is the step after these, once 4D's error shapes are agreed.

## Step 4: what needs Stevan

Each item has a recommendation. Settled, each becomes a decision with
DESIGN.md and LANGUAGE.md updated, and its work joins the step it
belongs to.

- **A. A copy that would pass the cap faults `:heap` (H3(a)).**
  - The copy keeps no sharing (D25, D223), so a value with 40 levels of
    sharing is 2^40 cells.
  - *Recommended:* `scratch_take` checks the cap as it grows, at one
    compare a growth, nothing a cell, so D240's walk is untouched. A
    send, fork or spawn faults `:heap` in the sender. A join or an exit
    record whose copy is too big becomes `(:error (:heap ...))`.
  - *Rejected:* keeping sharing in the copy. It costs every message 30
    to 50 per cent of D240's 41 instructions a cell, and gains nothing
    for trees.
  - *With it:* a fault's or dead letter's rendering is cut at a bound,
    ending `...`.
- **B. `eq?` and `hash` on shared values (H3(b)).**
  - *Recommended:* `eq?` keeps a set of visited pairs once its walk
    passes `held/16` cells. A tree that big couldn't be in that heap, so
    there must be sharing.
  - `hash` stays as D209 fixes its values. Document that it walks a
    shared value as a tree.
  - Low priority.
- **C. Server timeouts, opt-in (H4(a)).** D236 rules out a default.
  - *Recommended:* `http/serve-within ms ...` and `http/listen-within`,
    the twins of `http/request-within`, answer 408 (add it to
    `http/reason`) when a head or body is late.
  - In `rpc`: a deadline on the first message, and on each datum once
    it's begun.
  - In the runtime: a close still writing gives up after a bound.
  - Test: a raw client sends half a head and waits. That's within D236,
    since the timer is meant to win there.
- **D. Bounds on what connections hold (H4(c), H4(e), H5's errors).**
  - *Recommended:* unwritten output past 64 MB (as `READ_MAX`) fails its
    device, with a reason. Stopping reading instead would deadlock
    `cat` under `:exec`, and echoing peers.
  - `barf` echoes a bounded summary, not the whole message (revisits
    D230's text).
  - An optional most connections for `http/listen` and `rpc/listen`.
  - H5's refusals:
    - the client gives `(:error (:bad-url url))` and
      `(:error (:bad-header name))`;
    - a handler whose response has CR, LF or NUL in it, a header name
      that isn't a token, or a status outside 100 to 999 gets a 500;
    - the method and target are checked on both the `:tcp` and curl
      paths.
- **E. More than ~1,000 descriptors (H4(b), M2).**
  - *Recommended:* keep `select()`, so D113 holds, over a bitmap that
    grows in place of `fd_set`, with our own set and test, since glibc's
    fortified `FD_SET` aborts past 1,024.
    - macOS needs `_DARWIN_UNLIMITED_SELECT`; Linux takes a larger
      `nfds` as it is.
    - Raise the soft `RLIMIT_NOFILE` to the hard one (macOS starts at
      256).
    - The record's `select` entry becomes as long as it needs.
    - `paths[]` and `groups[]` are sized at startup. `groups` is read
      from a signal handler, so it can't grow.
    - About 120 lines.
  - *Rejected:* `poll()`, about 200 lines, because of macOS's terminals
    (D113).
  - Test: `t/live.py` opens 1,500 sockets. This also ends M2's limit.
- **F. Running out of stacks (M4).**
  - *Recommended:* when a stack can't be had, the process about to run
    faults `:heap` (or a new `:no-stack`, Stevan's choice), in place of
    `exit(2)`.
  - Pooled stacks past a threshold are unmapped.
  - LANGUAGE.md gives the ceiling: about 30,000 processes that hold a
    stack at once, at Linux's default `vm.max_map_count`.
- **G. Pids past an address's 32 bits (M5).**
  - *Recommended now:* `rt_address` faults once a pid needs more than
    the field holds, so nothing is misdelivered.
  - *Then:* rebalance the bits, 40 for the pid and 12 for the number:
    about 73 hours of non-stop fork and join on one island.
  - Pids never reused (D244) is the root. Reuse would be a larger
    change, not recommended now.
- **H. Timers across islands (L1).**
  - *Recommended:* a timer's number carries its island. Island 0's are
    still 1 up, so test 286 holds.
  - A cancel of another island's timer goes to that island, with a
    reply, as `spawn/on` does.
  - D232's "any process may cancel" stays.
- **I. Deleting a tree others can write (L2, and `fs/walk`).**
  - *Recommended now:* correct D254's note, that `lstat` means a walk or
    a delete can't be led out of its tree. It's true only of a tree
    nobody changes during the walk. Say so in LANGUAGE.md.
  - *Optional, later:* a C `fs/delete-all` on `openat`,
    `fstatat(AT_SYMLINK_NOFOLLOW)` and `unlinkat`.
- **J. FIFOs and devices that never end (L4).**
  - *Recommended:* open files `O_NONBLOCK`, `fstat` them, and read
    anything but a regular file as a waited-for descriptor, as a socket
    is. Refusing them would break `slurp "/dev/stdin"`.
  - The tools kept to one directory stays D188's "later".
- **K. IPv6 (L7).** It stays parked (D139, D165). No change proposed.
- **L. A lambda after `|>` (L11).**
  - *Optional:* `x |> fun(y) => ...` becomes `((lambda (y) ...) x)`,
    with SYNTAX.md updated.
  - Step 2 refuses it meanwhile.
- **M. A cache outside the install (L15).**
  - *Recommended:* `SLIGHT_CACHE` names where the runtime is kept,
    defaulting to `build/runtime/`, as D150 has it.
- **N. CI and hostile tests (P2, P6).**
  - *Recommended:* a GitHub Actions job running `make test` on x86-64
    Linux, both targets, AArch64 under qemu.
  - `make asan`, the runtime built with ASan and UBSan over the golden
    tests, as `make tsan` is.
  - A fuzzing harness for the JSON and s-expression validators.
  - Stevan's M2 stays the reference for macOS.

## Step 5: documents and the project

- **N1.** README's "one core", "Next is `:exec`" and "then TLS" (against
  D251) are brought up to date.
- **N2.** LANGUAGE.md and `time/parse-iso` say second 60 is refused (POSIX
  time), as Python's, Go's and JavaScript's parsers do.
- **P7.** The Unicode License v3's text goes beside `NOTICE`, which points
  at it. `runtime/unicode.c`'s header gets Unicode's copyright line, and
  the containers how-to gets a line on notices.
- **The review's own status.** Corrections:
  - H6's severity is Medium; there's no ASLR to defeat.
  - L1, L7 and L15's points that are decisions are marked so.
  - The citation for M6 is D205.

  A status block at the top, as the 2026-10-11 review has, says what's
  done as the steps land.
- **P1, P3, P4 and P5** are observations, already in PLAN.md's "Toward
  general purpose" (D249). No work here.
- **CLAUDE.md's Status** says what's done as each step lands.

## Not changing

- The default target stays AArch64 everywhere (D146).
- `out.S` stays beside the binary (documented).
- Any process may cancel any timer (D232), apart from 4H's fix between
  islands.
- Transfer-Encoding over Content-Length (RFC 9112).
- D165's first A record after CNAMEs, once the question is checked.
- `select()` itself (D113), however 4E is answered.
- `MAP_NORESERVE` is dropped from M4's fix, since it wouldn't help.

## Order and size

1. Step 1: two to three sessions, M1 and the buffer list the largest.
2. Step 2: one or two sessions. The hidden-name refactor goes first, so
   L10, L12 and L13 are each a few lines on it.
3. Step 3: one session.
4. Step 4: Stevan's answers first. Then A, E and F, which are the ones
   that keep a program running when a process misbehaves, then the rest.
5. Step 5: along the way.

Steps 1 to 3 don't depend on each other, and could go in any order or
together.
