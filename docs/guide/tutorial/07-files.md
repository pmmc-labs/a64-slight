# 7. Keeping the store in a file

In chapter 5, the supervisor brought the store back after a fault, but
with an empty table. This chapter keeps the table somewhere a fault
can't reach: a file. Every command that changes the table is added to a
**log** as it's run. A store started on the log runs those commands
again, and is back where it was.

## Devices

A file, a socket, a running command and the keyboard are all
**devices**. A device is a pid the runtime serves, not compiled code. It
talks in messages like any process. There are two ways to have one:

- `(connect source arg expr)` starts an actor, as `spawn` does, to
  evaluate `expr`, and connects it to the device. The actor gets
  `(:open f)` first, `f` being the device's pid, and then whatever the
  device sends: a reader's lines as `(:line f s)`, then `(:eof f)`.
- `(open source arg)`, from a task, opens a device the task owns, and
  gives `(:ok f)` or `(:error reason)`. The task takes the device's
  messages with `read`.

For files, `source` is `:fs/read`, `:fs/write` (which empties the file
first) or `:fs/append`, and `arg` is the path. A file opened to write
takes `(:write x ...)` from any process, and writes its arguments at
once. A device closes when its owner ends, or with `(disconnect f)`.

`(@include :fs)` has two functions on top of these, for whole files:
`(slurp path)`, which gives `(:ok lines)`, and `(spew path lines)`,
which writes them. Each runs in a task of its own, so either can be
called from anywhere.

## The log

<!-- code: ../code/tut-log.slight -->
```lisp
(defun write? (cmd)
    (member? (car cmd) (list :put :del :incr)))

; The table the log at path says, or an empty one if there's no log.
(defun load (path)
    (match (slurp path)
        ((:ok lines)          (replay () lines))
        ((:error (:enoent _)) ())))

(defun replay (table lines)
    (match lines
        (()            table)
        ((line & rest) (match (sexp/parse line)
                           ((:ok cmd) (replay (car (run-command table cmd)) rest))))))
```

The log is one command a line, written as slight writes data: `(put
"apple" 3)`. `sexp/print` turns a value into that text, and
`sexp/parse` reads it back, giving `(:ok value)`. A command read back
is the same list it was, so `replay` runs it with chapter 4's
`run-command`, threading the table through as chapter 2's `run-all`
did.

A missing log is an empty table: that's the first run. Any other error,
a log that can't be read or a line that isn't a command, matches no
clause, so it's a fault. Something's badly wrong, and a store that
carried on with half its data would hide it.

## The store that writes it

<!-- code: ../code/tut-log.slight -->
```lisp
; Starts a store on the log at path, and gives its pid.
(defun start (path)
    (let table (load path))
    (connect :fs/append path (logged-open table)))

(defun logged-open (table)
    (recv
        ((:open f) (logged table f))))

(defun logged (table f)
    (recv
        ((:do cmd to)
            (match (run-command table cmd)
                ((next answer)
                    (when (and (write? cmd) (eq? (car answer) :ok))
                        (send f (list :write (sexp/print cmd) \n)))
                    (reply to answer)
                    (logged next f))))))
```

`start` reads the log, then connects a new store to the same file,
opened to append. The store's first message is the device's `(:open
f)`, so it waits for that, then loops as before, with the device as a
second argument. A command that changed the table, and worked, is
written to the log before the reply goes back. `\n` is the newline, a
one-character string.

`when` runs its body if the test is `#true`, and is `()` if not. `and`
stops at the first `#false`, and every operand but the last must be a
boolean.

## Starting again

<!-- code: ../code/tut-7-log.slight -->
```lisp
; args: build/t/tut-7-log.log
(@include "tut-log.slight")

(let path (if (nil? @ARGV) "store.log" (car @ARGV)))
(spew path ())

(let s (start path))
(call s (list :put "apple" 3))
(call s (list :put "pear" 5))
(call s (list :incr "apple"))
(call s (list :del "pear"))
(call s (list :get "apple"))
(kill s)

(match (slurp path)
    ((:ok lines) (map (lambda (line) (tty/write line \n)) lines)))

(let s2 (start path))
(list (call s2 (list :get "apple")) (call s2 (list :keys)))
```

<!-- output: ../code/tut-7-log.expected -->
```
(put "apple" 3)
(put "pear" 5)
(incr "apple")
(del "pear")
((ok 4) (ok ("apple")))
```

`@ARGV` is the program's arguments, a list of strings. The first
comment line is for the guide's tests, which give the program a path
under `build/`. Run on its own, it writes `store.log`. `(spew path ())`
empties the log first, so the program does the same every time.

`(kill s)` ends the first store, whatever it's doing, and its file
closes with it. The log has the four changes and not the `:get`. A
second store started on it has the table the first one left.

`tty/write` writes its arguments as they are, strings without the
quotes `pprint` would give them, so the log's lines come out as they
are in the file.

## Back to the supervisor

Chapter 5's supervisor, with `start` in place of `spawn`:

<!-- code: ../code/tut-7-supervised.slight -->
```lisp
; Chapter 5's supervisor, starting the store on its log each time.
(defun supervise (path)
    (let s (start path))
    (monitor s)
    (supervisor path s))

(defun supervisor (path s)
    (recv
        ((:exit _ (:error (kind _ _)))
            (pprint (list :restarting kind))
            (supervise path))
        (msg
            (send s msg)
            (supervisor path s))))

(let path (if (nil? @ARGV) "store.log" (car @ARGV)))
(spew path ())

(let sup (spawn (supervise path)))
(call sup (list :put "apple" 3))
(call sup (list :put "color" "red"))
(pprint (join (fork (ask-within 1000 sup (list :do (list :incr "color"))))))
(call sup (list :get "apple"))
```

<!-- output: ../code/tut-7-supervised.expected -->
```
fault in #<pid 7>: not a number: "red" (+ at docs/guide/code/tut-store.slight:23:40)
(restarting not-a-number)
(error (timeout #<pid 4>))
(ok 3)
```

The same fault, the same restart, but this time `"apple"` is still 3.
The command that faulted was never logged, since the store died before
it got that far, so the store that replaced it doesn't fault on it
again. A log of commands only works if replaying them can't fault the
way running them first did, and here that holds: a command is logged
only after it has run.

The `:exit` pattern now takes the result apart, `(:error (kind _ _))`,
and keeps only the fault's kind. It also matches only an error, so an
`:exit` of any other shape would be passed on to the store, where it
would be a dead letter.

The pids are the runtime's, given out in order: `spew`'s task and its
file are 2 and 3, the supervisor 4, `load`'s task and its file 5 and 6,
and the store 7.

A log grows for ever. A real store would now and then write the whole
table out (`(spew path (map sexp/print ...))`, as `put` commands) and
start a new log from there. That's left as an exercise.

---

Previous: [6. Time, and testing](06-time.md) ·
Next: [8. Serving the store](08-serving.md)
