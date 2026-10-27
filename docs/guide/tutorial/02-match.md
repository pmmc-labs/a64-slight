# 2. Lists, match, and data as tagged lists

Chapter 1's table works, but `caar` and `cadar` say how to dig into a
list, not what's in it. This chapter takes data apart with patterns, and
makes the store's commands into data: lists with a keyword at the front.

## Patterns

Here's the table again, with `match`:

<!-- code: ../code/tut-table.slight -->
```lisp
(defun table/get (key table)
    (match table
        (()             :not-found)
        (((k v) & rest) (if (eq? k key) v (table/get key rest)))))

(defun table/del (key table)
    (table/del-loop key table ()))

(defun table/del-loop (key table seen)
    (match table
        (()             (reverse seen))
        ((pair & rest)  (if (eq? (car pair) key)
                            (append (reverse seen) rest)
                            (table/del-loop key rest (cons pair seen))))))

(defun table/put (key value table)
    (cons (list key value) (table/del key table)))

(defun table/keys (table)
    (map car table))
```

`match` tries its clauses in order, and runs the body of the first whose
pattern fits, with the pattern's names bound to the parts they matched:

- `()` matches the empty list. A literal (a number, a string, `#true`,
  `()`, a keyword) matches a value `eq?` to it.
- A name matches anything, and binds it. `_` matches anything, and
  binds nothing.
- `(p1 p2)` matches a list of exactly two elements, each matching its
  pattern. Patterns nest: `((k v) & rest)` is a list whose first element
  is a pair.
- `& rest` matches the rest of a list, which can be empty.

A value no clause fits is a fault, so a `match` says what shapes it
expects and complains about the rest. `if` is a short `cond`: a test,
then the value when it's `#true`, then the value when it's `#false`.

These functions are in a file of their own, `tut-table.slight`, which
the rest of the tutorial's programs include with
`(@include "tut-table.slight")`. An include splices a file's forms in
where it stands. There are no modules: a program is one namespace.

## Commands as data

The store will take commands from other parts of a program, so a
command should be a value that can be built, passed around, kept and
sent: a list with a keyword at the front saying what kind it is.

```
(:put "apple" 3)
(:get "apple")
(:del "apple")
(:keys)
```

Running a command takes the table and gives back two things: the next
table, and a reply. A function gives back one value, so it gives a list
of the two:

<!-- code: ../code/tut-2-commands.slight -->
```lisp
; Runs one command on a table, and gives the next table and the reply.
(defun run-command (table cmd)
    (match cmd
        ((:put key value) (list (table/put key value table) (list :ok value)))
        ((:get key)       (list table (found (table/get key table))))
        ((:del key)       (list (table/del key table) (list :ok ())))
        ((:keys)          (list table (list :ok (table/keys table))))
        (_                (list table (list :error (list :unknown cmd))))))

(defun found (value)
    (if (eq? value :not-found)
        (list :error :not-found)
        (list :ok value)))
```

`(:put key value)` matches a list of three elements whose first is the
keyword `:put`, and binds the other two. The last clause takes anything
else, so a command the store doesn't know is an answer, not a fault.

Every reply is `(:ok value)` or `(:error reason)`. That's slight's
convention for anything that can fail in a way the caller should deal
with. The caller matches on it, and the pattern says which case is
which. There are no exceptions to forget to catch.

## Running a list of commands

<!-- code: ../code/tut-2-commands.slight -->
```lisp
; Runs commands in order: the last table, and the replies in order.
(defun run-all (table cmds replies)
    (match cmds
        (()           (list table (reverse replies)))
        ((cmd & rest) (match (run-command table cmd)
                          ((next reply) (run-all next rest (cons reply replies)))))))

(let cmds (list
    (list :put "apple" 3)
    (list :put "pear" 5)
    (list :get "apple")
    (list :del "apple")
    (list :get "apple")
    (list :keys)
    (list :frobnicate)))

(match (run-all () cmds ())
    ((table replies)
        (map pprint replies)
        table))
```

<!-- output: ../code/tut-2-commands.expected -->
```
(ok 3)
(ok 5)
(ok 3)
(ok ())
(error not-found)
(ok ("pear"))
(error (unknown (frobnicate)))
(("pear" 5))
```

`run-all` is chapter 1's loop shape again: the table is threaded through
as an argument, each command's next table becoming the next call's, and
the replies gathered in an accumulator. A `match` with one clause is how
to take a result apart: `((next reply) ...)` names the two parts of what
`run-command` gave.

The commands are built with `list`, not quoted. `'(:put "apple" 3)`
doesn't mean what it looks like: the reader turns `:put` into
`(quote put)` everywhere, a quoted list included, so that's a list
whose first element is the list `(quote put)`. Build messages with
`list`.

The whole store is now a function from a table and a command to a table
and a reply. It has no state of its own: whoever calls it keeps the
table. The next chapter gives it somewhere to live.

---

Previous: [1. Values, functions, and loops](01-values.md) ·
Next: [3. State in a process](03-actors.md)
