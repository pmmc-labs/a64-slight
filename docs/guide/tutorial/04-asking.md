# 4. Asking and answering

The store from chapter 3 prints its replies, which is no use to the code
that sent the commands. This chapter has it answer, shows the
difference between asking from a task and from an actor, and finds a
bug that only shows up when two callers share the store.

## Answering at an address

A task can't take messages with `recv`, but it can make an **address**,
a place that messages can be sent to, and wait for the next message
there. A command carries the address its answer should go to, and the
store sends its reply there:

<!-- code: ../code/tut-store.slight -->
```lisp
(defun store (table)
    (recv
        ((:do cmd to)
            (match (run-command table cmd)
                ((next answer)
                    (reply to answer)
                    (store next))))))

; A task's way to run a command: send it, and wait for the reply.
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

(let s (spawn (store ())))
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
putting back one more. Each runs in a task of its own, started with
`fork`. `join` waits for a task to end and gives its result:

<!-- code: ../code/tut-4-race.slight -->
```lisp
; Adds one to a key's value, in two steps: get it, then put it back.
(defun bump (s key)
    (match (call s (list :get key))
        ((:ok n) (call s (list :put key (+ n 1))))))

(let s (spawn (store ())))
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

## When an actor asks

`ask` is a task's, since it makes an address. An actor has a mailbox
already: it sends its own pid, `$$`, to be answered at, and takes the
reply with `recv`. Here an actor counts words in the store:

<!-- code: ../code/tut-4-actor.slight -->
```lisp
; An actor counts words in the store. It can't ask, which is a task's,
; so it sends its own pid to be answered at, and waits for the reply.
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

(let s (spawn (store ())))
(read (spawn (tally s (list "a" "b" "a"))))
```

<!-- output: ../code/tut-4-actor.expected -->
```
("a" (ok 1))
("b" (ok 1))
("a" (ok 2))
(ok (ok ("a" "b")))
```

`(let r (recv ...))` waits in the middle of the function. That's
allowed because of where it is, and that's the `recv` rule.

## The recv rule

**A `recv` can go only where a tail call could, and a function that can
reach one can only be called in tail position.** The `let` above is in
tail position: everything after it in the body is the rest of the loop.

It's natural to pull the asking out into a helper:

```
(defun ask-store (s cmd)
    (send s (list :do cmd $$))
    (recv ((:reply r) r)))
...
            (pprint (list word (ask-store s (list :incr word))))
```

and the compiler turns it down:

```
tally.slight:11:32: ask-store waits for messages (it reaches a recv), so it can only be called in tail position
```

A function that waits can't give a value back to its caller. The rule
is what lets a waiting process hold no stack at all: the compiler
splits a function at each `recv`, so when the process waits, there's
nothing to come back to. It's a function, its arguments and a mailbox,
and a million of them is cheap.

In practice:

- **Waiting goes inline**, as `(let r (recv ...))` in the function's
  own body.
- **A conversation goes in a task.** A task can call a function that
  asks, as deep as it likes, and get the answer back as a value: `ask`
  and `join` keep the task's stack while they wait. When an actor needs
  a conversation with several steps, it can `fork` a task to have it.
- **An actor waiting for a reply takes the next message, whatever it
  is.** There's no selective receive: if something else arrives first,
  `tally`'s `recv` doesn't match it, and it's a dead letter. `tally`
  gets away with it because nothing else sends to it. An actor that
  serves requests shouldn't stop to wait for a reply in the middle of
  one. It's another reason conversations belong in tasks, and state in
  actors.

---

Previous: [3. State in a process](03-actors.md) ·
Next: [5. When things go wrong](05-errors.md)
