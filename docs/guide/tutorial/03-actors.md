# 3. State in a process

In chapter 2, whoever ran the store's commands had to keep the table and
pass it along. That's fine for one caller. When several parts of a
program share a store, the table needs to be somewhere they can all
reach, changing as commands come in. In slight, that place is a
process.

## An actor

An **actor** is a process with a mailbox. It runs a function that waits
for a message, handles it, and calls itself with what it has become:

<!-- code: ../code/tut-3-actor.slight -->
```lisp
; The store: its table is its argument, and it runs a command for each
; message.
(defun store (table)
    (recv
        ((:do cmd)
            (match (run-command table cmd)
                ((next answer)
                    (pprint answer)
                    (store next))))
        (:stop table)))
```

`recv` takes the next message from the mailbox, waiting for one if it's
empty, and matches it against its clauses, with the same patterns as
`match`. For `(:do cmd)` it runs the command, prints the reply, and
loops with the next table. `:stop` ends it, and its value is the table.

This is the chapter 1 loop again, with one difference: each turn waits
for a message. The table is still never changed. Each turn is a call
with a new one. "The store's state" is just the argument of the call
it's waiting in.

`run-command` is chapter 2's, unchanged. The actor is a thin loop around
a pure function. That's the usual shape: the logic stays testable
without any processes, and the actor only decides when it runs.

## Starting it and sending to it

<!-- code: ../code/tut-3-actor.slight -->
```lisp
(let s (spawn (store ())))
(send s (list :do (list :put "apple" 3)))
(send s (list :do (list :put "pear" 5)))
(send s (list :put "plum" 7))
(send s (list :do (list :keys)))
(send s :stop)
(read s)
```

<!-- output: ../code/tut-3-actor.expected -->
```
(ok 3)
(ok 5)
dead letter: (put "plum" 7) (recv at docs/guide/code/tut-3-actor.slight:19:5)
(ok ("pear" "apple"))
(ok (("pear" 5) ("apple" 3)))
```

- `(spawn expr)` starts a new process that evaluates `expr`, here a
  call to `store` with an empty table, and gives its **pid**.
- `(send pid msg)` puts a copy of `msg` at the end of the process's
  mailbox, and returns at once. It doesn't wait for the message to be
  taken, or answered.
- `(read s)` waits for the store to end and gives how it ended:
  `(:ok value)`, here the last table.

The third message forgot its `:do`. No clause matches it, so the store
drops it, logs it to stderr as a **dead letter**, with the place of the
`recv` that turned it down, and takes the next. A message the receiver
doesn't understand is a bug in the sender, and the log is how you find
it.

## What the store guarantees

Messages from one process to another arrive in the order they were
sent, and an actor takes them one at a time. So each command runs on the
table the previous one left, and nothing else can touch the table in
between, because nothing else can reach it. There are no locks: the
store's whole state belongs to one process, which does one thing at a
time.

`send` copies the message, and every process has a heap of its own.
Processes share nothing, and since nothing can be changed in place,
there's nothing a copy could get out of step with.

## Why the top level can't do this

The program's top level is a process too, but a **task**, not an actor:
a process that computes a value, and can't wait for messages. If the
top level tried to take the store's replies with `recv`:

```
(send s (list :do (list :get "apple") $$))
(recv ((:reply r) r))
```

the compiler would say:

```
top.slight:5:1: a task (a fork's body, or the top level) can't wait for messages, and this recv would: spawn an actor for it
```

(`$$` is the current process's pid, which a message carries so the
receiver knows where to answer.) That's why the store prints its replies
here, and the top level just reads its end. Getting replies back to a
task is the next chapter.

---

Previous: [2. Lists, match, and data as tagged lists](02-match.md) ·
Next: [4. Asking and answering](04-asking.md)
