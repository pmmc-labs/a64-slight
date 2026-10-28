# Nothing changes

In slight, no value can be changed once it's made. There's no
assignment, no `set!`, no mutable array or hash table, and no way to
change a list in place. A `let` names a value; it doesn't make a
variable. This page says where state goes instead, and why the language
is built this way.

## Where state goes

A program still has state: a counter goes up, a table gets new keys, a
game moves on. In slight that state is always one of two things.

- **The arguments of a loop.** A loop is a function that calls itself in
  tail position, so what changes from one turn to the next is what it
  passes itself. Chapter 1 of the tutorial is about this.
- **A process.** State that lasts, and that several parts of a program
  share, lives in an actor, as the arguments of the function it loops
  through, one call per message. "Changing" it is sending a message,
  after which the actor calls itself with a new value. Chapter 3 of the
  tutorial is about this.

So the question to ask of any state is *whose* it is. If it belongs to
one computation, it's an argument. If it belongs to the program, it's an
actor's.

## Old versions stay

Since nothing is changed in place, a new version of something is a new
value, and the old one is still there for as long as anything refers to
it:

<!-- code: ../code/con-versions.slight -->
```lisp
(@include :map)

; Every version of a map is a value of its own: a change makes a new
; map, which shares all but what changed with the old one.
(let v1 (map/put (map/new) "apple" 3))
(let v2 (map/put v1 "pear" 5))
(let v3 (map/delete v2 "apple"))
(let history (list v1 v2 v3))

(pprint (map (lambda (m) (map/get m "apple")) history))
(pprint (map map/size history))

; So undo is going back to an older value.
(map/get (car history) "apple")
```

<!-- output: ../code/con-versions.expected -->
```
(3 3 not-found)
(1 2 1)
3
```

`(@include :map)` is a persistent hash map, a trie like Clojure's. A
change copies only the path to what changed, a few small nodes, and
shares the rest with the old map. Lists work the same way: `(cons x xs)`
makes one cell and shares all of `xs`. Keeping a hundred versions
of a big structure costs a hundred small paths, not a hundred copies.

`examples/etch.slight` keeps every step of a drawing this way, for undo
and redo.

## Why

**Sharing is safe.** A value can be handed to any function, kept in any
structure, and nothing can change it behind the holder's back. There's
no defensive copying, and no aliasing bug.

**Equality is by value.** Since a value can't change, there's nothing
to tell two equal values apart. `eq?` compares structure, and `hash`
gives the same number for equal values on every run and every machine.

**Messages are just values.** A message is copied into the receiver's
heap, and the copy can't drift from the original, because neither can
change. A process's state is its own, so actors need no locks.

**There are no cycles.** A value can only refer to values made before
it, so no structure ever refers to itself. That simplifies printing,
equality and copying. It also makes the collector cheaper: an old value
can't point at a younger one, so the collector needs no write barrier
(D200).

**Programs are easier to test.** A function of values gives the same
answer every time. Most of a slight program is such functions, with a
thin layer of processes around them (tutorial, chapter 6).

## What it costs

- **Some algorithms change shape.** One that updates an array in place
  becomes one that builds a new structure, or threads it through a loop.
  A tuple gets an element in one step, and a persistent map gets a key
  in a few, so it's rarely slower in the large, but it's written
  differently.
- **Building a list backwards.** `cons` adds to the front, so a loop
  that gathers results builds them in reverse, and turns them round at
  the end. The prelude's `map` and `filter` do this.
- **Shared mutable state has to be an actor.** That's a process and
  messages where another language would use a variable and a lock. In
  return, every change to that state goes through one place, one
  message at a time.

---

[Concepts](README.md) · Next: [The recv rule](recv-rule.md)
