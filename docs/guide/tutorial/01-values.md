# 1. Values, functions, and loops

The tutorial builds one program: a **key-value store**, a table of names
and values that other parts of a program can put things in and ask about.
Over five chapters it goes from a few functions on a list to an actor,
asked by tasks, kept running by a supervisor. Each chapter's programs
are whole, and run as they are: copy one into a file, and

```
node bin/slightc.ts --target x86_64 -o store store.slight
./store
```

(leave out `--target x86_64` on an Apple Silicon Mac).

This chapter is about values and functions, and writing loops in a
language with nothing to mutate.

## Values

<!-- code: ../code/tut-1-values.slight -->
```lisp
(pprint 42)
(pprint (+ 1 2))
(pprint (/ 7 2))
(pprint (div 7 2))
(pprint "a string")
(pprint :apple)
(pprint #true)
(pprint (list 1 "two" :three (list 4.5)))
(pprint ())

(pprint (== 1 1.0))
(pprint (eq? 1 1.0))
(pprint (eq? (list 1 "two") (list 1 "two")))

(str "apples: " 3)
```

<!-- output: ../code/tut-1-values.expected -->
```
42
3
3.5
3
"a string"
apple
#true
(1 "two" three (4.5))
()
#true
#false
#true
"apples: 3"
```

`pprint` prints a value and a newline. A program's last form isn't
printed with `pprint`: its value is what the program prints when it
ends.

- Every call is `(function arguments...)`, operators included.
- Numbers are integers or floats. `/` always gives a float, and `div` is
  integer division.
- A string is in double quotes, and prints in them.
- `:apple` is a **keyword**, a symbol that stands for itself. It prints
  without its colon. Keywords are slight's names for things: tags,
  options, states, kinds of message.
- `#true` and `#false` are the booleans.
- `(list ...)` makes a list, which can hold anything, other lists
  included. `()` is the empty list.

There are two kinds of equality. `==` compares numbers, by value, so an
integer and a float can be `==`. `eq?` compares any two values by
structure: two lists are `eq?` when their elements are, whoever made
them. Nothing has an identity apart from its value, since nothing can
change.

## A table

The store's table is an **association list**: a list of `(key value)`
pairs. The first function looks a key up:

<!-- code: ../code/tut-1-table.slight -->
```lisp
(defun table/get (key table)
    (cond
        ((nil? table)           :not-found)
        ((eq? (caar table) key) (cadar table))
        (#true                  (table/get key (cdr table)))))
```

`defun` defines a function. The `/` in `table/get` is only part of the
name; slight's builtins are grouped the same way (`str/len`,
`math/sqrt`).

`cond` tries its tests in order and gives the body of the first that's
`#true`. A test must be `#true` or `#false`: there's no truthiness, so
an empty list isn't false, and `(cond (table ...))` faults. Ask what you
mean, `(nil? table)`. The last clause's `#true` makes it the default.

`car` is a list's first element, and `cdr` the rest. `caar` is the
`car` of the `car`, the first pair's key, and `cadar` the `car` of the
`cdr` of the `car`, its value. That's hard to read, and chapter 2
replaces it with patterns.

A key that isn't there gives `:not-found`, a keyword that says so.

## Loops are tail calls

`table/get` calls itself as the last thing it does. That's a **tail
call**, and slight compiles it as a jump: the call takes no stack, so
the function is a loop, however long the table. slight has no `for` or
`while`. A loop is a function that calls itself in tail position, with
what would be its loop variables as its arguments.

Removing a key is where that matters. The obvious way:

```
(defun table/del (key table)
    (cond
        ((nil? table)           ())
        ((eq? (caar table) key) (cdr table))
        (#true                  (cons (car table) (table/del key (cdr table))))))
```

works, but its recursive call isn't the last thing it does: `cons` is,
after it returns. So each pair before the key takes a frame on the
stack, and a long enough table faults (`:stack`). The way round it is an
**accumulator**, an argument that carries what's been done so far:

<!-- code: ../code/tut-1-table.slight -->
```lisp
(defun table/del (key table)
    (table/del-loop key table ()))

; seen holds the pairs passed over, in reverse.
(defun table/del-loop (key table seen)
    (cond
        ((nil? table)           (reverse seen))
        ((eq? (caar table) key) (append (reverse seen) (cdr table)))
        (#true                  (table/del-loop key (cdr table) (cons (car table) seen)))))
```

`cons` puts an element on the front of a list, so `seen` builds up
backwards, and is turned round at the end. This shape (a function that
starts a loop, and a loop with an accumulator) is the most common one
in slight. The prelude's `map`, `filter` and `reverse` are written this
way.

## Nothing changes

Putting a key builds a new table. It doesn't change the old one:

<!-- code: ../code/tut-1-table.slight -->
```lisp
(defun table/put (key value table)
    (cons (list key value) (table/del key table)))

(defun table/keys (table)
    (map car table))

(let t1 (table/put "apple" 3 ()))
(let t2 (table/put "pear" 5 t1))
(let t3 (table/put "apple" 4 t2))
(let t4 (table/del "pear" t3))

(pprint t3)
(pprint (table/get "apple" t3))
(pprint (table/get "plum" t3))
(pprint (table/get "apple" t1))
(table/keys t4)
```

<!-- output: ../code/tut-1-table.expected -->
```
(("apple" 4) ("pear" 5))
4
not-found
3
("apple")
```

`let` gives a value a name for the rest of the body it's in. It isn't a
variable: there's nothing to assign to it later. Each table is a value
of its own, and `t1` still says `"apple"` is 3 after `t3` says it's 4.

`map` calls a function on each element of a list and gives the list of
results. `car` is a builtin, and a builtin can be passed as a value like
any function, so `(map car table)` is the keys.

A table that's never changed in place is safe to share, to keep old
versions of, and to send to another process. The next chapters lean on
all three.

The prelude has `lookup`, which is `table/get`, and `assoc`, which adds
a pair to the front without taking out the old one. Writing them here
was the point.

---

Next: [2. Lists, match, and data as tagged lists](02-match.md)
