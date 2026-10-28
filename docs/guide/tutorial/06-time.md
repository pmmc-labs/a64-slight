# 6. Time, and testing

This chapter gives the store keys that expire, a session that lasts a
second or a token that lasts half of one, and then tests the store,
time included, without waiting for any of it.

## Timers

`(after ms pid msg)` sends `msg` to `pid` once `ms` milliseconds have
passed, and gives the timer's number. `(timer/cancel t)` takes timer
`t` back, if it hasn't fired. `(sleep ms)` pauses the calling process,
and only that one.

A key that expires is a timer the store sets for itself. With
`(:put-for key value ms)`, the store sends itself `(:expire key ...)`
after `ms`, and takes the key out when it comes. There's a catch: the
key might have been put again in the meantime, and then the old timer's
message mustn't take out the new value. Cancelling the old timer
handles most of that, but not all of it. If the timer has fired
already, its message is in the mailbox, and there's no taking it back.
So each expiry gets a serial number, the store remembers each key's,
and an `:expire` whose number isn't the key's current one is stale, and
ignored:

<!-- code: ../code/tut-ttl.slight -->
```lisp
; table is the store's, timers holds (key (serial timer)) for each key
; that expires, and serial is the next expiry's.
(defun ttl-store (table timers serial)
    (recv
        ((:do (:put-for key value ms) to)
            (let t (after ms $$ (list :expire key serial)))
            (reply to (list :ok value))
            (ttl-store
                (table/put key value table)
                (table/put key (list serial t) (drop-timer key timers))
                (+ serial 1)))
        ((:expire key n)
            (match (table/get key timers)
                ((m _) (if (== m n)
                           (ttl-store (table/del key table) (table/del key timers) serial)
                           (ttl-store table timers serial)))
                (_     (ttl-store table timers serial))))
        ((:do cmd to)
            (match (run-command table cmd)
                ((next answer)
                    (reply to answer)
                    (ttl-store next (forget cmd timers) serial))))))

; A key put or deleted without a time no longer expires.
(defun forget (cmd timers)
    (match cmd
        ((:put key _) (drop-timer key timers))
        ((:del key)   (drop-timer key timers))
        (_            timers)))

(defun drop-timer (key timers)
    (match (table/get key timers)
        ((_ t) (timer/cancel t) (table/del key timers))
        (_     timers)))
```

A few things to notice:

- The timers are kept in a second table. `table/put` and `table/get`
  don't care what the values are.
- `(:do (:put-for key value ms) to)` is a nested pattern. It comes
  before the general `(:do cmd to)`, which would match it too, and the
  rest still go to chapter 4's `run-command`. The store grew without
  changing what it already did.
- A timer is a message like any other. The store handles `:expire` one
  message at a time, between commands, so nothing can see a key half
  expired.
- A pending timer keeps the program running. A store with a key due to
  expire in an hour holds the program open for an hour, unless the
  timer is cancelled, or the store ends. (A timer for a process that has
  ended is dropped.)

Here it is in use:

<!-- code: ../code/tut-6-ttl.slight -->
```lisp
(@include "tut-ttl.slight")

(let s (spawn (ttl-store () () 1)))
(call s (list :put-for "session" "abc" 1000))
(call s (list :put-for "token" "xyz" 500))
(pprint (call s (list :keys)))

(sleep 600)
(pprint (call s (list :keys)))

(call s (list :put-for "session" "def" 1000))
(sleep 600)
(pprint (call s (list :get "session")))

(sleep 600)
(pprint (call s (list :get "session")))
(clock/us)
```

<!-- output: ../code/tut-6-ttl.expected -->
```
(ok ("token" "session"))
(ok ("session"))
(ok "def")
(error not-found)
1800000
```

At 600 ms the token has gone and the session hasn't. Putting the
session again renews it, so at 1200 ms, past its first expiry, it's
still there, and at 1800 ms it's gone. `(clock/us)` is the time since
the program started, in microseconds.

## The virtual clock

Run that program and it takes 1.8 seconds, and its last line is a
little over 1800000, since work takes time too. Run it as

```
SLIGHT_CLOCK=virtual ./ttl
```

and it finishes at once, and prints exactly 1800000. The virtual clock
starts at 0, and moves only when nothing can run, straight to the next
timer. So a sleep takes no time, the timers fire in the same order as
on the real clock, and work takes no time at all. That's how the
output above was worked out, and how the guide's tests run it.

Under the virtual clock a program that waits for an hour runs in an
instant, and runs the same way every time. Tests of anything with
timeouts, retries or expiry should use it.

## Tests

`(@include :test)` is a small library that prints TAP, the Test Anything
Protocol: one `ok` or `not ok` line a test, which any TAP harness reads.
Perl's `prove` is one.

<!-- code: ../code/tut-6-test.slight -->
```lisp
(@include "tut-ttl.slight")
(@include :test)

; The reply alone, from running cmd on table.
(defun reply-of (table cmd)
    (cadr (run-command table cmd)))

(let t1 (car (run-command () (list :put "a" 1))))
(let s (spawn (ttl-store () () 1)))

(run-tests (list
    (is (reply-of () (list :get "a")) (list :error :not-found) "get of a key that isn't there")
    (is (reply-of t1 (list :get "a")) (list :ok 1) "get of a key that is")
    (is (reply-of t1 (list :incr "a")) (list :ok 2) "incr adds one")
    (is (reply-of () (list :incr "a")) (list :ok 1) "incr counts from 0")
    (is (reply-of () (list :bogus)) (list :error (list :unknown (list :bogus))) "an unknown command")
    (is (call s (list :put-for "k" "v" 100)) (list :ok "v") "put-for")
    (is (do (sleep 150) (call s (list :get "k"))) (list :error :not-found) "a key expires")
    (is (do (call s (list :put-for "k" "v" 100))
            (call s (list :put "k" "w"))
            (sleep 150)
            (call s (list :get "k")))
        (list :ok "w")
        "a put without a time stops the expiry")
    (ok (== (clock/us) 300000) "the clock moved by the sleeps alone")))
```

<!-- output: ../code/tut-6-test.expected -->
```
ok 1 - get of a key that isn't there
ok 2 - get of a key that is
ok 3 - incr adds one
ok 4 - incr counts from 0
ok 5 - an unknown command
ok 6 - put-for
ok 7 - a key expires
ok 8 - a put without a time stops the expiry
ok 9 - the clock moved by the sleeps alone
1..9
#true
```

- `(is got expected name)` passes if the two are `eq?`, and shows both
  if they aren't. `(ok test name)` passes if `test` is `#true`.
- `(run-tests tests)` prints the results in order, then the plan,
  `1..9`, and gives `#true` if they all passed.
- The arguments are worked out as the list is built, in order, so a
  test can do things first, as the expiry tests do with `do`, which
  evaluates its forms in turn and gives the last one's value.

Most of the tests are of `run-command`, with no process in sight.
That's the payoff of chapter 3's shape, a pure function inside a thin
actor: the logic is tested by calling it. The actor's own behaviour,
expiry, is tested through `call`, under the virtual clock, so the last
test can say exactly how much time passed.

Two more tools for when a test fails only sometimes:

- `SLIGHT_SCHED=n` runs the processes in an order picked by a
  generator seeded with `n`, the same order for the same `n`. A loop
  over a hundred seeds finds a test that depends on which process runs
  first.
- `SLIGHT_RECORD=file` keeps everything a run takes from the world,
  and `SLIGHT_REPLAY=file` runs it again exactly, as often as it takes
  to see what went wrong.

LANGUAGE.md has both, under "Replaying a run, and other orders".

---

Previous: [5. When things go wrong](05-errors.md) ·
Next: [7. Keeping the store in a file](07-files.md)
