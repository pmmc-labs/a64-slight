# Find a deadlock

A program whose top level is left waiting for something that can never
come ends at once, says what it was waiting for, and exits 1. Here a
store forgot to reply:

<!-- code: ../code/howto-deadlock.slight -->
```lisp
(defun store (n)
    (recv
        ((:get to) (store n))))

(let s (spawn (store 0)))
(ask s (list :get))
```

<!-- output: ../code/howto-deadlock.expected -->
```
deadlock: the root process is waiting in ask/via for the first of #<address 1.0>, #<pid 2>, and nothing else can run
exit: 1
```

Read it as: the top level is in `ask` (whose helper is `ask/via`),
waiting for either an answer at its address or the store, pid 2, to
end. Neither can happen, because nothing can run. The store is waiting
for its next message, and no one will send one.

When the top level waits on a chain, a task joining a task joining an
actor, say, the message follows the chain, naming the function each
process is in, up to the one waiting for a message:

```
deadlock: the root process is waiting in the top level for #<pid 2> to end, which is waiting in ask-twice (recv at prog.slight:7:12) for a message, and nothing else can run
```

## Looking inside a running program

A deadlock among other processes doesn't end the program, and a program
that's slow or stuck while it serves needs looking at while it runs.
The `:system` device describes every process: its kind, state, the
function it's in, its mailbox, its heap and its reductions.

<!-- code: ../code/howto-system.slight -->
```lisp
(defun store (n)
    (recv
        ((:get to) (store n))))

(let s (spawn (store 0)))
(let asker (fork (ask s (list :get))))
(sleep 1)

; Every process's info, from the :system device.
(defun processes (sys infos)
    (match (read sys)
        ((:process _ info) (processes sys (cons info infos)))
        ((:end _)          (reverse infos))))

(match (open :system)
    ((:ok sys)
        (send sys (list :processes))
        (map (lambda (info) (pprint (map (lambda (k) (lookup k info)) (list :pid :kind :state :in :mail))))
            (processes sys ()))))
(kill asker)
:done
```

<!-- output: ../code/howto-system.expected -->
```
(#<pid 1> task running "the top level" 0)
(#<pid 2> actor recv "store" 0)
(#<pid 3> task reading "ask/via" 0)
done
```

The asking task, pid 3, is `reading`, in `ask/via`; the store, pid 2, is
in `recv`, with nothing in its mailbox. Together they say the store
took the request and didn't answer.

- `(open :system)` from a task, or `(connect :system expr)` from an
  actor, opens the device. `(:processes)` asks for every process, which
  come as `(:process s info)` one at a time, then `(:end s)`, all from
  the same moment. `(:process pid)` asks about one, and `(:runtime)`
  about the runtime: processes, timers, devices, heap, the clock.
- An `info` is a list of `(key value)`. Besides those above, there's
  `:parent`, `:joining` (what it waits for), `:heap`, `:reductions`,
  `:stack` and `:devices`.
- `examples/top.slight` shows it all live, refreshed every second.
- `(rpc/system)` is an `rpc` handler that answers with the same, so a
  program can be looked into from outside, with `nc`.

**Signs to look for:** a process in `recv` with mail piling up is being
sent more than it can take. One in `joining` or `reading` for a long
time waits for something stuck: follow its `:joining`. A `running` or
`ready` one with reductions climbing and nothing to show for it is in a
loop.

[How-to](README.md)
