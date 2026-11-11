# Actor runtimes in the Lisp family

Which Scheme and Lisp actor systems are worth adding to `bench/`, so
slight is compared with its own family as an actor system, not only as
a Lisp (where Chez already stands). Research and a proposal: nothing in
`bench/` or the harness has changed. 11 Nov 2026.

The candidates' facts come from their own repositories (cloned and read:
tags, changelogs, manuals, source), Homebrew's formulae (a clone of
homebrew-core), and `apt-cache policy` on this container's Ubuntu 24.04.
The network here blocked codeberg.org, call-cc.org, gnu.org,
quicklisp.org and janet-lang.org, so Guile Fibers' 2.0 and CHICKEN's
eggs are from mailing-list and package-PR pages instead, and said so.
No project publishes a cost per process except Erlang (327 words, 2.6
KB, in its efficiency guide).

## In short

- **Nothing in the family is what slight is.** Isolated heaps with
  copied messages exist only on the BEAM (LFE, Clojerl) and in Racket's
  places (a VM each, too heavy for a million). Every Scheme's
  lightweight processes share one heap and pass messages by reference,
  so `payload` measures nothing there but a pointer.
- **Gambit** is the nearest in shape and the fastest of those run: a
  mailbox per thread, preemptive green threads, compiled to native code
  through C. Ping-pong 1.2 times slight's time; a million waiting in 1,850
  bytes each, where slight takes 4,550.
- **Racket** is the one people know, and the only Scheme with both
  mailboxes and, since 9.0, threads on every core. Ping-pong 5.3 times
  slight's; a million waiting in 3,700 bytes each, but 28 s to start and
  ask them.
- **Swish**, Erlang on Chez, with selective receive, links, monitors and
  `gen-server`, is the one that's an actor *system* rather than threads
  with mailboxes. Released 5 Nov 2026. Not run: it needs a non-threaded
  Chez 10.3 built from source, and this session wasn't allowed to build
  code fetched from GitHub.
- **Recommended:** Gambit and Racket now, Swish once it's built on the
  Mac. LFE is Erlang's compiler underneath, so would time as Erlang does.

## The candidates

Model: how processes are scheduled, whether memory is shared, how a
message travels, where it waits, and how many cores it uses. "Alive" is
the latest release and the last commit. "The nine" is which of
`ping-pong`, `payload`, `thread-ring`, `fan-in`, `spawn`,
`idle-memory`, `parallel-fib`, `latency` and `bank` can be written the
way the language is usually written.

| | Model | Compiles to | Alive | Ubuntu 24.04 | Homebrew | The nine |
|---|---|---|---|---|---|---|
| **Gambit** threads, Termite | green, preemptive (0.02 s quantum); one heap, by reference; a mailbox per thread, selective receive by a cursor (Termite's `recv`); one core (SMP builds "experimental, beta") | native, via C | 4.9.8, 9 Aug 2026; commits to 8 Nov 2026 | `gambc` 4.9.3 (2018) | `gambit-scheme` 4.9.8 | all but `bank`; `parallel-fib` one core only |
| **Gerbil** `std/actor` | Gambit's threads underneath; `->` and `<-`, selective; marshalled between servers of an ensemble | native, via Gambit | 0.18.2, 2 Apr 2026; 5 commits in a year | none | `gerbil-scheme` 0.18.2 | as Gambit |
| **Racket** threads, places | threads green and preemptive, one heap, by reference, a mailbox each (`thread-send`, `thread-receive`); 9.0's `#:pool 'own` threads run on other OS threads, still sharing; places a VM each, copied messages | native, through Chez (Racket CS) | 9.3, 10 Aug 2026; commits today | `racket` 8.10 (no parallel threads) | `minimal-racket` 9.3 | all but `bank`; every core needs 9.x |
| **Swish** (Chez) | Erlang's processes on Chez continuations, preempted every 1,000 calls; one heap, by reference; an inbox each, selective `receive` with `after`; links, monitors, `gen-server`, supervisors; one core | native, through Chez | 3.0.0, 5 Nov 2026 | none; needs Chez ≥ 10.3, built `--nothreads` | none (Homebrew's Chez is threaded) | all but `bank`; one core only |
| **Chez** threads | OS threads, one heap; no actor library but Swish | native | 10.4.1, 11 May 2026 | `chezscheme` 9.5.8 | `chezscheme` 10.4.1 | not `spawn`, `idle-memory` (a million OS threads) |
| **Guile Fibers** | fibers, preemptive at 100 Hz by default; one heap, by reference; channels (Concurrent ML), no mailboxes; a scheduler per core | bytecode, with a JIT | Fibers 2.0.0, about end of Oct 2026 (from Homebrew's and nixpkgs' PRs; Codeberg blocked); Guile 3.0.11 | Guile 3.0.9; no Fibers package | `guile-fibers` 2.0.0 | all but `bank`, a channel standing for a mailbox |
| **CHICKEN** SRFI-18, `mailbox` egg | green, preemptive, one native thread, one heap | native, via C | 6.1.0, 7 Nov 2026 (mailing list) | `chicken-bin` 5.3.0 | `chicken` 6.1.0 | all but `bank`, if the eggs build on 6 (not confirmed) |
| **Loko** | Concurrent ML fibers, cooperative, one core | native, no C | 0.13.0, 27 Jun 2026 | none | none; amd64 only, no macOS | not on the Mac, so not comparable |
| **Cyclone** | OS threads (SRFI-18); no actor library | native, via C | 0.36.0, Feb 2024; 3 commits in a year | none | a tap, not core | as Chez |
| **LFE** | the BEAM's processes, unchanged | BEAM, through Erlang's own compiler (`compile:forms`) | 2.2.2, 5 Aug 2026 | none; source on Erlang ≥ 27 for HEAD | `lfe` 2.2.2 | all but `bank`, as Erlang |
| **Clojerl** | the BEAM's processes | BEAM, through Core Erlang | 0.9.1, 7 Nov 2026, "experimental" | none | none | as LFE |
| **Clojure** core.async | `go` blocks as state machines on a thread pool, cooperative at `<!`/`>!`; one heap, by reference; channels; every core | JVM bytecode | Clojure 1.12.6, 2 Sep 2026; core.async 1.9.865, 19 Mar 2026 | `clojure` 1.11.1, `libcore-async-clojure` 1.5.648 | `clojure` 1.12.6 | all but `bank` and `latency` (a go block that never parks holds its thread) |
| **SBCL** + sb-concurrency, Sento | OS threads, one heap, a lock-free mailbox each; Sento's actors on a shared pool of threads (default 4) or a thread each | native | SBCL 2.6.10, 29 Oct 2026; Sento 3.5.0, 11 Sep 2026 | `sbcl` 2.2.9 (old); Quicklisp needed for Sento | `sbcl` 2.6.10 | threads: not `spawn`, `idle-memory`; Sento's shared dispatcher: all but `bank`, untried |
| **Janet** | fibers cooperative on an event loop per OS thread; `ev/thread` a VM each, messages marshalled (copied) | its own bytecode | 1.43.0, 7 Nov 2026 | none; `make` | `janet` 1.43.0 | all but `bank`, fibers on one core |

`bank` is left out everywhere: in `bench/` only slight (on its virtual
clock), C and SimPy have it, and Go, Erlang and Rust don't either. A
Scheme port would need a virtual clock written for it.

Sources: Gambit's `doc/gambit.txi` (Threads) and `lib/termite/`;
Gerbil's `doc/reference/std/actor.md`; Racket's reference
(`threads.scrbl`, `eval-model.scrbl`) and its blog's "Parallel Threads
in Racket" (Nov 2025); Swish's `doc/swish/erlang.tex`, README and
`ChangeLog.md` (github.com/becls/swish; indigobio/swish stopped in
2021); Chez's `BUILDING`; Fibers' `fibers.texi` (the GitHub mirror,
frozen at 1.3.1); Loko's `Documentation/manual/int-concurrency.texi`;
Cyclone's `docs/Multithreaded-Development.md`; LFE's
`src/lfe_comp.erl`; Clojerl's `clj_emitter.erl`; core.async's
`impl/dispatch.clj` and clojure.org's "core.async and virtual threads"
(11 Mar 2026); SBCL's `contrib/sb-concurrency/mailbox.lisp`; Sento's
README; Janet's `src/core/ev.c` and `CHANGELOG.md`; Erlang's efficiency
guide (processes, system limits).

Two worth knowing. core.async ran `go` blocks on virtual threads in
1.9.829-alpha2 and took it back in alpha3; only `io-thread` uses them
now. And Racket's places are the only Scheme with slight's isolation
(a heap each, messages copied), but a place is a whole VM, so they
answer `parallel-fib`, not `spawn` or `idle-memory`.

## What was run

Installed from apt, in a few minutes: `racket` 8.10, `gambc` 4.9.3,
`erlang-base` 25.3 (for reference), `clojure` 1.11.1 with
`libcore-async-clojure` 1.5.648 on OpenJDK 21, and `sbcl` 2.2.9. Guile
Fibers, LFE and Swish were cloned, but building them was refused in
this session (code fetched from GitHub), so they weren't run. apt's
Gambit and Racket are old (2018 and 2023): the Mac's Homebrew has 4.9.8
and 9.3.

Built as each is usually built: `raco make`; `gsc -exe`; Clojure
compiled ahead of time (`(compile 'ping-pong)`), run with `java -cp`;
SBCL saved as an executable (`save-lisp-and-die`); slight with
`node bin/slightc.ts --target x86_64`. Each run through `bench/rusage.c`
for wall, CPU and peak memory, 2 or 3 runs. One core is `taskset -c 0`
for all, with Erlang's `+S 1:1`, and the JVM's
`-XX:ActiveProcessorCount=1 -Dclojure.core.async.pool-size=1`; every
core is each runtime's default. The cloud machine (4 cores) is noisy,
so these place them roughly, no more.

### ping-pong 2000000

| | wall | CPU | peak | over slight |
|---|---:|---:|---:|---:|
| slight | 0.49 s | 0.48 s | 2 MB | 1 |
| Gambit 4.9.3 | 0.59 s | 0.58 s | 156 MB | 1.2 |
| Erlang 25, one core | 1.98 s | 1.98 s | 36 MB | 4.0 |
| Erlang 25, every core | 1.94 s | 1.99 s | 38 MB | 4.0 |
| Racket 8.10, one core | 2.60 s | 2.58 s | 77 MB | 5.3 |
| Racket 8.10, every core | 2.49 s | 2.45 s | 77 MB | 5.1 |
| Clojure core.async, one core | 4.71 s | 4.66 s | 159 MB | 9.6 |
| Clojure core.async, every core | 9.94 s | 12.9 s | 295 MB | 20 |
| SBCL threads, one core | 10.1 s | 10.0 s | 74 MB | 21 |
| SBCL threads, every core | 53.2 s | 45.6 s | 74 MB | 109 |

Startup, included above: Racket 0.07 s and 60 MB, the JVM with Clojure
0.52 s and 86 MB, Erlang 0.13 s and 38 MB, Gambit 0.008 s and 11 MB,
SBCL 0.005 s and 19 MB.

Erlang is 4 times slight here, where `docs/BENCHMARKS.md` has them
level on this machine. This is apt's OTP 25 (its JIT is on, `emu_flavor`
says `jit`), older than the suite's, and the runs here weren't through
the harness; which of those makes the gap isn't known. So Erlang's row
is only there to place the others against the same run.

### idle-memory 1000000

| | wall | CPU | peak | a process |
|---|---:|---:|---:|---:|
| Gambit 4.9.3 | 3.0 s | 3.0 s | 1.86 GB | 1,850 bytes |
| Erlang 25, `+P 1100000` | 4.3 s | 4.3 s | 2.81 GB | 2,770 bytes |
| slight | 6.9 s | 6.8 s | 4.55 GB | 4,550 bytes |
| Racket 8.10 | 28.7 s | 28.4 s | 3.77 GB | 3,710 bytes |
| Clojure core.async, one core | 10.6 s | 10.4 s | 1.61 GB | 1,520 bytes |
| Clojure core.async, every core | 4.8 s | 14.5 s | 1.73 GB | 1,640 bytes |

A process is the peak less startup's, over a million, with its handle
in a list. SBCL wasn't tried: a million OS threads, each with a stack
of its own, isn't how anyone writes it.

### Surprises

- **Gambit's threads are slight's size class or better.** A waiting
  Gambit thread is a continuation on the heap, 1,850 bytes, where a
  slight process keeps its stack since D276. Its ping-pong is within
  20% of slight's, in safe code; its peak there, 156 MB, is a heap that
  grows before it collects, not a leak.
- **Racket starts a million threads slowly.** 28 s where its ping-pong
  is merely 5 times slight's: the cost is starting threads and filling
  the main thread's mailbox, not the waiting (3,700 bytes each). Worth a
  profile on 9.3 before quoting it.
- **Every core costs the JVM and SBCL dearly in ping-pong.** Two
  processes handing a message back and forth across cores pay for the
  hand-off each time: core.async twice its one-core time, SBCL five
  times. Racket's ordinary threads stay on one OS thread, so its two
  modes are the same.
- **core.async's idle go blocks are the smallest**, 1,520 bytes: a
  parked go block is a closure and a state array, no stack at all, as
  tokio's tasks are.

### The programs

In `docs/research/actor-runtimes-lisp/`, `ping-pong/` and
`idle-memory/`, named as `bench/` would name them. Each prints only the
number slight's does. The two in Racket:

```racket
#lang racket/base
(require racket/match)

(define (pong)
    (match (thread-receive)
        [(list 'ping from i) (thread-send from (list 'pong i)) (pong)]
        ['stop (void)]))

(define (ping p i n)
    (cond
        [(= i n) (thread-send p 'stop)]
        [else
            (thread-send p (list 'ping (current-thread) i))
            (match (thread-receive) [(list 'pong _) (ping p (+ i 1) n)])]))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(ping (thread pong) 0 n)
(printf "~a\n" n)
```

```racket
#lang racket/base
(require racket/match)

(define (idle) (match (thread-receive) [(list 'ping from) (thread-send from 1)]))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(define me (current-thread))
(for ([p (for/list ([i n]) (thread idle))]) (thread-send p (list 'ping me)))
(printf "~a\n" (for/sum ([i n]) (thread-receive)))
```

In Gambit, with the declarations Gambit's own benchmarks use (still
safe):

```scheme
(declare (standard-bindings) (extended-bindings) (block))

(define (pong)
    (let ((m (thread-receive)))
        (if (eq? (car m) 'ping)
            (begin (thread-send (cadr m) (list 'pong (caddr m))) (pong)))))

(define (ping p i n)
    (if (= i n)
        (thread-send p (list 'stop))
        (begin
            (thread-send p (list 'ping (current-thread) i))
            (thread-receive)
            (ping p (+ i 1) n))))

(define n (string->number (cadr (command-line))))
(ping (thread-start! (make-thread pong)) 0 n)
(display n)
(newline)
```

```scheme
(declare (standard-bindings) (extended-bindings) (block))

(define (idle) (let ((m (thread-receive))) (thread-send (cadr m) 1)))

(define (start n acc)
    (if (= n 0) acc (start (- n 1) (cons (thread-start! (make-thread idle)) acc))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define n (string->number (cadr (command-line))))
(for-each (lambda (p) (thread-send p (list 'ping (current-thread)))) (start n '()))
(display (gather n 0))
(newline)
```

In Clojure, a go block reading a channel of its own stands for a
process and its mailbox:

```clojure
(defn pong [in]
    (go-loop []
        (let [[tag from i] (<! in)]
            (when (= tag :ping)
                (>! from [:pong i])
                (recur)))))

(defn ping [p n]
    (let [me (chan 1)]
        (go-loop [i 0]
            (if (= i n)
                (do (>! p [:stop]) n)
                (do (>! p [:ping me i]) (<! me) (recur (inc i)))))))
```

```clojure
(defn idle []
    (let [c (chan)]
        (go (let [[_ from] (<! c)] (>! from 1)))
        c))

(defn -main [arg]
    (let [n (Long/parseLong arg)
          reply (chan n)]
        (doseq [c (doall (repeatedly n idle))] (>!! c [:ping reply]))
        (println (loop [i 0 sum 0] (if (= i n) sum (recur (inc i) (+ sum (<!! reply))))))
        (System/exit 0)))
```

And SBCL's ping-pong, `ping-pong/ping-pong.lisp`, two OS threads with
an `sb-concurrency` mailbox each.

## Recommendation

Add **Gambit** and **Racket** now, and **Swish** when it's been built on
the Mac. Each answers a different question:

- **Gambit**: the fastest native Scheme with a mailbox per process, so
  the closest rival for what a message and a switch cost. One core
  only, as Gambit is; its every-core row would say nothing, so leave it
  out of that table (as `latency` leaves out tokio's every core).
- **Racket**: the Scheme most people would reach for, its threads with
  mailboxes in the standard library. On 9.x, `(thread thunk #:pool
  'own)` gives it an every-core mode, which is the only Scheme with
  both. `racket-mc` is ordinary threads made parallel ones in the
  places that fork (the processes in `parallel-fib`, `fan-in`'s
  senders, the ring), its one-core run the plain `thread`.
- **Swish**: the Erlang model in Scheme (selective receive, links,
  monitors, supervisors, preemption by calls), so the fairest "is
  slight's process model costly" comparison inside the family. One
  core only.

For all three, `payload` should say that the message is passed by
reference, as it does for Go and tokio: nothing is copied.

Leave out:

- **LFE** and **Clojerl**: Erlang's compiler and VM underneath, so they
  time as Erlang does; Erlang is in already. A sentence in
  BENCHMARKS.md saying so is enough.
- **Gerbil**: its actors are Gambit's threads with a protocol on top,
  and the project is quiet (5 commits in a year). Gambit covers it.
- **Clojure core.async**: credible and alive, but go blocks on a JVM
  thread pool aren't processes (they can't be preempted, so `latency`
  can't be written fairly), and the JVM's start and warm-up dominate
  the small runs. Go stands for "channels and a pool" already.
- **SBCL** (threads, or Sento): OS threads, so no `spawn` or
  `idle-memory`, and 21 times slight's ping-pong on one core. Sento's
  shared dispatcher could hold a million actors; worth a look later,
  but it needs Quicklisp, and its actors aren't preempted either.
- **Guile Fibers**: the nearest miss. Every core, preemptive, alive,
  in Homebrew; but channels rather than mailboxes, Guile's bytecode
  rather than native code, and no Ubuntu package for Fibers. Third
  choice if Swish won't build.
- **CHICKEN**, **Loko**, **Cyclone**, **Janet**, **Chez threads**: one
  core with eggs not confirmed on CHICKEN 6; no macOS; no actors and
  quiet; cooperative fibers on a bytecode VM; OS threads.

### What adding each would take

- **`bench/run.py`**: `ACTORS` gets `gambit`, `racket`, `swish`;
  `Impl`s `gambit`, `racket`, `racket-mc`, `swish`; `EXT` `scm` (Gambit),
  `rkt`, `ss` for Swish in a directory or a suffix of its own (`.ss`
  is Chez's); `MC` gets `racket-mc`; tool checks for `gsc`, `raco`
  and `swish`.
- **Builds**: Gambit `gsc -exe -o OUT FILE.scm`; Racket `raco make
  FILE.rkt` (compiled into `compiled/`, run as `racket FILE.rkt N`);
  Swish `swish-build -o OUT FILE.ss` (a standalone executable). Gambit's
  binary needs nothing at run time; Racket's needs `racket`.
- **One core**: Gambit and Swish are one OS thread anyway. Racket's
  ordinary threads are one too; its every-core mode is the program
  using parallel threads, so `racket-mc` needs its own source,
  `NAME-mc.rkt`, as `slight-mc` has.
- **Process limits**: none of the three has one like Erlang's `+P`.
  Gambit's heap grows as needed (`-:m` sets a minimum, not wanted);
  Racket's and Swish's have no flag that matters here.
- **Versions**: Gambit 4.9.8 and Racket 9.3 from Homebrew on the Mac
  (`brew install gambit-scheme minimal-racket`); on Ubuntu, apt's are
  2018's and 2023's, so a fresh one from source or the Racket installer.
  Swish needs Chez 10.3 or later built `--nothreads` (Homebrew's is
  threaded), then Swish's own `./configure && make`, with cmake,
  `uuid-dev` and `libsystemd-dev` on Linux. Worth a Makefile target
  under `bench/`, as Rust's cargo build is.
- **`bank`**: none of them, as for Go, Erlang and Rust.

## Open questions

1. Swish, Gambit and Racket's ordinary threads all share one heap and
   pass messages by reference. Is that fair to put beside slight in
   `payload`, or should that row be slight and Erlang only, with the
   rest marked "not copied" as Go's and tokio's are now?
2. Gambit's Termite has selective receive, and its own `!` and `recv`.
   Write Gambit's programs on plain `thread-send` and `thread-receive`
   (as here, the fastest, and what slight does: the next message), or
   on Termite, the actor library people would use?
3. Should Racket's every-core mode be parallel threads (9.0, sharing
   the heap) or places (a VM each, copying, as slight's islands)? Only
   threads can run `spawn` and `idle-memory`; places could run
   `parallel-fib` and `fan-in`.
4. Building Swish means building a second Chez beside Homebrew's. Is
   that acceptable on the M2 Max, or should Swish wait for a package?
5. Is it worth one cloud run with a current Erlang (28 or 29) to check
   that the 4× gap above is apt's OTP 25 and not something here?
