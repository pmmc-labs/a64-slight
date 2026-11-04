# 4. Asking and answering

The store from chapter 3 prints its replies, which is no use to the code
that sent the commands. This chapter has it answer, shows two ways to
wait for the answer, and finds a bug that only shows up when two
callers share the store.

## Answering at an address

Any process can make an **address**, a place of its own that messages
can be sent to, and wait for the next message there. What's sent to an
address is kept apart from the rest of the mail: `recv` leaves it, and
only `read` at that address takes it. A command carries the address its
answer should go to, and the store sends its reply there:

<!-- code: ../code/tut-store.slight -->
```lisp
(defun store (table)
    (recv
        ((:do cmd to)
            (match (run-command table cmd)
                ((next answer)
                    (reply to answer)
                    (store next))))))

; A caller's way to run a command: send it, and wait for the reply.
(defun call (s cmd)
    (ask s (list :do cmd)))
```

The prelude does the work. `(ask pid msg)` makes an address, adds it to
the end of `msg`, sends the message, and waits at the address for the
answer. `(reply to v)` is the other half: the actor sends `v` to the
address `to`. If the actor ends before it answers, `ask` doesn't wait
for ever: it raises an error (chapter 5).

The local is called `answer`, not `reply`, because a local hides a
function of the same name. With `((next reply) (reply to reply) ...)`,
the call would be to the reply itself, and fault with
`not a function`.

This version of the store is in `tut-store.slight`, with `run-command`
and the table, and the rest of the tutorial includes it. Now the top
level can ask:

<!-- code: ../code/tut-4-ask.slight -->
```lisp
(@include "tut-store.slight")

(let s (fork (store ())))
(call s (list :put "apple" 3))
(pprint (call s (list :get "apple")))
(pprint (call s (list :get "plum")))
(call s (list :keys))
```

<!-- output: ../code/tut-4-ask.expected -->
```
(ok 3)
(error not-found)
(ok ("apple"))
```

The program ends once nothing more can happen. Here that's with the
store still waiting for its next command, which will never come. That's
fine: a waiting process holds nothing up.

## Two callers, one value

Say two parts of the program each count a hit, by getting a value and
putting back one more. Each runs in a process of its own, started with
`fork`. `join` waits for a process to end and gives its result:

<!-- code: ../code/tut-4-race.slight -->
```lisp
; Adds one to a key's value, in two steps: get it, then put it back.
(defun bump (s key)
    (match (call s (list :get key))
        ((:ok n) (call s (list :put key (+ n 1))))))

(let s (fork (store ())))
(call s (list :put "hits" 0))

(let a (fork (bump s "hits")))
(let b (fork (bump s "hits")))
(pprint (list (join a) (join b)))
(pprint (call s (list :get "hits")))
```

Two bumps, and the count is 1:

```
((ok (ok 1)) (ok (ok 1)))
(ok 1)
```

Both tasks asked for the value before either put it back, so both saw 0
and both put 1. It's the lost update from any shared-memory program.
Processes don't prevent it. They only make it clear where it can
happen: between one message and the next. The store does each command
whole, but nothing makes two commands from one caller a unit.

The fix is to make the whole update one command, so it happens in one
step of the store. `tut-store.slight` has one, `:incr`:

<!-- code: ../code/tut-store.slight -->
```lisp
; A key that isn't there counts from 0.
(defun incr (key table)
    (let n (table/get key table))
    (let next (if (eq? n :not-found) 1 (+ n 1)))
    (list (table/put key next table) (list :ok next)))
```

<!-- code: ../code/tut-4-race.slight -->
```lisp
(let c (fork (call s (list :incr "views"))))
(let d (fork (call s (list :incr "views"))))
(pprint (list (join c) (join d)))
(call s (list :get "views"))
```

<!-- output: ../code/tut-4-race.expected -->
```
((ok (ok 1)) (ok (ok 1)))
(ok 1)
((ok (ok 1)) (ok (ok 2)))
(ok 2)
```

Whenever the right answer depends on a value not changing between two
messages, the work belongs in the actor that owns the value.

The order the tasks ran in here is the scheduler's: first in, first
out. Programs shouldn't depend on it, and `SLIGHT_SCHED=n` runs one
under other orders to find out whether it does (LANGUAGE.md, "Replaying
a run, and other orders").

## Answering at a pid

An address is one place to answer at. A process's own pid is another:
it sends `$$`, and takes the reply with `recv`. Here an actor counts
words in the store that way:

<!-- code: ../code/tut-4-actor.slight -->
```lisp
; An actor counts words in the store. It sends its own pid to be
; answered at, and takes the reply with recv.
(defun tally (s words)
    (match words
        (()
            (send s (list :do (list :keys) $$))
            (recv ((:reply keys) keys)))
        ((word & rest)
            (send s (list :do (list :incr word) $$))
            (let r (recv ((:reply r) r)))
            (pprint (list word r))
            (tally s rest))))

(let s (fork (store ())))
(join (fork (tally s (list "a" "b" "a"))))
```

<!-- output: ../code/tut-4-actor.expected -->
```
("a" (ok 1))
("b" (ok 1))
("a" (ok 2))
(ok (ok ("a" "b")))
```

`(let r (recv ...))` waits in the middle of the function, and the
function goes on from there when the reply comes.

## Waiting, and where

A `recv` can go anywhere in a process's code (D276), so it's natural to
pull the asking out into a helper:

```
(defun ask-store (s cmd)
    (send s (list :do cmd $$))
    (recv ((:reply r) r)))
...
            (pprint (list word (ask-store s (list :incr word))))
```

and that works: `ask-store` waits, keeping the process's stack, and
gives the reply back as its value.

Two things to know:

- **A `recv` takes the next message, whatever it is.** There's no
  selective receive: if something else arrives first, `ask-store`'s
  `recv` doesn't match it, and it's a dead letter. `tally` gets away
  with it because nothing else sends to it.
- **An address takes only what's sent to it.** That's why `ask` is the
  usual way: it reads the reply at an address of its own, which `recv`
  leaves alone, so nothing else can be taken for it, and nothing sent
  to the process meanwhile is lost. `tally` could have used `call`, and
  the store would have been none the wiser. Answer at a pid when the
  process has nothing else to hear; ask when it might.

---

Previous: [3. State in a process](03-actors.md) ·
Next: [5. When things go wrong](05-errors.md)
