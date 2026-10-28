# Types for slight

Proposed, **(open)**: nothing here is agreed or built. It's a thought
experiment written down, as `SYNTAX.md` was before D246, so it can be
argued with. Each choice that changes the language (a new form, new
syntax in `.sly`) needs Stevan's word before it's built, and goes into
`DECISIONS.md` when it has it.

## Goals

- **Types describe slight as it is.** Tagged lists, association lists,
  heterogeneous lists, `()` for "nothing", `#false` for "no number",
  mailboxes that take messages from anywhere: each gets a type, and no
  program has to change to be checked. Examples bend to the language
  (D84); the language doesn't bend to the checker.
- **Nothing changes what a program does.** Types are erased: a program
  with annotations compiles to the same code as without them, and runs
  the same. A checker that's wrong can cost a warning, never a run.
- **Warn only about what will fault.** By default the checker says
  something only when a value can't be what's asked of it: a fault, a
  dead letter or an arm no value reaches, certain if that code runs.
  It never rejects a program that might work (Dialyzer's rule). A
  warning on a program that runs correctly is a bug in the checker.
- **Useful with no annotations,** and more useful with some. What
  comes from builtins, the prelude, literals and patterns is known
  without anyone writing a type; annotations add the rest.
- **Small enough to port.** The checker is written as the compiler is
  (pure functions over s-expressions, association lists, no classes),
  so it can be ported to slight with the rest of it.

Not goals: soundness, types in code generation (for now), type classes
or overloading, inferring generic types, and changing `eq?`, `()` or
anything else about values.

## How deep

| Depth | What | Annotations | Rejects programs? |
|---|---|---|---|
| 0 | As now: everything is found when it runs, as a fault | none | no |
| 1 | Contracts: annotations checked when a function is entered | optional | no, faults |
| 2 | A lint by inference: only what's certainly wrong | none | warnings |
| 3 | Gradual types, structural, as TypeScript's | optional | warnings; errors with `--strict` |
| 4 | Sound inference, Hindley-Milner, as Gleam's or OCaml's | none needed | yes |

This proposes depths 2 and 3, in that order: one checker, two
settings. Without `--strict` it warns only about certain faults; with
it, it also warns about what might fault, and the warnings are errors.

Depth 4 is turned down. Heterogeneous lists, association lists such as
`%ENV`, `apply`, `json/parse`'s values and mailboxes mixing keys, timers
and exit notices would all need declared variants or a `Dynamic` type
with decoders, as Gleam has. Every example would have to change, and
the inference engine would be a large part of a compiler that's meant
to be ported. Faults end only their process, too, so soundness is worth
less here than where one wrong value takes the program down. Depth 1 is
left as a question (below): it finds less than depth 2 does, and only
when the code runs.

## The types

| `.sly` | s-expression | What it holds |
|---|---|---|
| `int`, `float` | `int`, `float` | the numbers |
| `num` | `num` | `int \| float` |
| `str` | `str` | strings |
| `sym` | `sym` | every symbol, keywords and booleans included |
| `:ok` | `:ok` | that symbol alone |
| `true`, `false` | `#true`, `#false` | that boolean alone |
| `bool` | `bool` | `true \| false` |
| `[]`, `nil` | `nil` | `()` |
| `list<T>` | `(list T)` | a list, `()` included, of `T`s |
| `[A, B]` | `(shape A B)` | a list of exactly two, an `A` then a `B` |
| `[A, ...T]` | `(shape A & T)` | an `A`, then a list of type `T` |
| `:tag(A, B)` | `(:tag A B)` | a tagged list: `[:tag, A, B]` |
| `#(A, B)` | `(tuple A B)` | a tuple of two |
| `fun(A, B) -> C` | `(fun (A B) C)` | a function of two arguments |
| `task<T>` | `(task T)` | a task's pid; its value is a `T` |
| `actor<M>` | `(actor M)` | an actor's pid; it takes messages of type `M` |
| `address<M>` | `(address M)` | an address; what's sent to it is an `M` |
| `pid` | `pid` | any process or device |
| `A \| B` | `(or A B)` | either |
| `any` | `any` | anything: not checked |
| `never` | `never` | nothing: what `raise` gives, or a loop that never returns |
| `Name`, `Name<T>` | `Name`, `(Name T)` | an alias, declared with `type` |

A tagged list is just a fixed list headed by a keyword, but it gets its
own notation, since it's how slight writes variants and messages. In
s-expressions, a list type headed by a keyword is a tagged list, and
`&` takes the rest, as in patterns: `(:write & (list str))`.

Types are structural: two types with the same shape are the same type,
whatever they're called. An alias is only a name for its type, and may
refer to itself through a list, tagged list or tuple, which JSON needs:

```
type Json = :null | bool | int | float | str | list<Json>
          | :object(...list<[str, Json]>)
type Result<T, E> = :ok(T) | :error(E)
```

```lisp
(deftype Json (or :null bool int float str (list Json)
                  (:object & (list (shape str Json)))))
(deftype (Result T E) (or (:ok T) (:error E)))
```

What fits where:

- `int` and `float` are each a `num`; a keyword is a `sym`, and so are
  `true` and `false`; `[]` is a `list<T>` for any `T`.
- `[A, B]` is a `list<A | B>`; `:tag(A)` is a `[sym, A]` and a
  `list<sym | A>`.
- `A` is an `A | B`. An `A | B` fits where a `C` is wanted if both `A`
  and `B` do.
- A `fun(A) -> B` fits where a `fun(C) -> D` is wanted if a `C` is an
  `A` and a `B` is a `D`, and the arities are equal.
- `task<T>`, `actor<M>` and `address<M>` are each a `pid` (an address is
  `pid?`). An `actor<M>` fits where an `actor<N>` is wanted if every `N`
  is an `M`: an actor that takes more messages can stand in for one that
  takes fewer.
- `any` fits everywhere, and everything fits `any`. That makes it
  gradual: where a type isn't known, nothing is checked.

## Annotations

All optional. Three forms, each erased once the checker has read it:

| `.sly` | s-expression | |
|---|---|---|
| `type Name = T;` | `(deftype Name T)` | an alias; `type Name<A, B> = T;` and `(deftype (Name A B) T)` take parameters |
| `fun f(x: int, y): str { ... }` | `(sig f (int any) str)` | a function's parameters and result |
| `let x: T = e;` | `(let x (the T e))` | an expression's type, checked |

- In `.sly`, any parameter, a function's result, or a `let`'s name may
  have a type after a `:`, and what's left out is `any`. The reader
  makes a `sig` from them, just before the `defun`, so the
  s-expressions for an annotated function are its `defun` and a `sig`.
- In s-expressions, a `sig` goes anywhere at the top level, one for
  each function at most, as Typed Racket's `:` does. `defun` itself
  doesn't change.
- A generic function lists its type variables: `fun map<A, B>(f: fun(A)
  -> B, xs: list<A>): list<B>` and `(sig (map A B) ((fun (A) B) (list
  A)) (list B))`. Only a `sig` is generic; the checker never infers one.
  At each call, it works out what `A` and `B` are from the arguments.
- A function that waits says what its actor takes, after its result:
  `fun counter(n: int) recv CounterMsg { ... }`, and `(sig counter (int)
  any (recv CounterMsg))`. `recv` is reserved already, so no new word.
- `(the T e)` is `e`, checked to be a `T`, and taken to be one after:
  not a cast. It's how `.sly` writes a typed `let`, and how a lambda is
  given a type: `fun(x: int) => x + 1` is `(the (fun (int) any) (lambda
  (x) (+ x 1)))`. Most lambdas need none, since a lambda's parameters
  take their types from where it goes: in `map(fun(x) => x + 1, xs)`,
  `x` is whatever `xs` holds.

Types have names of their own, apart from values', so a `type` named
`Msg` doesn't clash with a function named `Msg`. Aliases are capitalized
by convention, not by rule.

## Inference

The checker runs over each function's body, knowing what it was given:

- **Literals** are their types: `1` is an `int`, `"a"` a `str`, `:ok`
  the keyword `:ok`, `[1, :a]` a `[int, :a]`. A `let` widens a number or
  a string to `int`, `float` or `str`, as TypeScript's `let` does;
  keywords stay as they are, since they're tags.
- **Builtins** have their types in a table, beside the one in
  `codegen.ts` that gives their arities: `str::len` is `fun(str) ->
  int`, `str_to_int` is `fun(str) -> int | false` (and takes a radix too), `json::parse`
  is `fun(str) -> Result<Json, :bad-json(int)>`. Arithmetic follows the
  numbers: `int + int` is an `int`, a `float` with either is a `float`,
  and `num + num` is a `num`; `/` is a `float`.
- **The prelude's functions** have `sig`s, in `lib/prelude.slight`:
  `lookup<K, V>(k: K, table: list<[K, V]>): V | :not-found`, `nth<T>(i:
  int, xs: list<T>): T | nil`, and so on.
- **A parameter without a type is `any`,** in the function's body.
  Inferring parameters' types from how they're used (Dialyzer's success
  typings) is a question below.
- **A function's result,** when it has no `sig`, is worked out from its
  body: the types of all the values it can end with, together. A
  function that calls itself starts as `never` and is worked out again
  till it stops changing. After three rounds it's `any`, so the checker
  always finishes.
- **`cond`, `match` and `recv`** give the union of their arms.

### Narrowing

A pattern says what shape a value has, so in each arm of `match` or
`recv` the topic's type is narrowed by the arm's pattern, and the names
it binds get the types of the parts they match. Later arms get what's
left: an arm whose pattern takes all of a union's member (a literal, a
name, `_`, `[]`, or a tagged list whose parts are all names) takes it
out of the type the arms after it see.

A test in `cond` (and so in `if`, `when` and `case`) narrows too, when
it's one the checker knows: a type predicate (`int?(x)`, `str?(x)`,
`nil?(x)`, `cons?(x)` and the rest), `x == lit` or `eq?(x, lit)`, `!`
of one of those, and `&&` of them. In the clause it's true, and its
complement in the clauses after. Nothing else narrows: there are no
user-written guards, and nothing follows a value into a function.

```
fun arg(i: int, args: list<int | false>, default: int): int {
    match nth(i, args) {     // int | false | nil
        []    => default,
        false => default,
        k     => k,          // int: [] and false are taken
    }
}
```

## What it finds

Each warning is a fault, or a dead letter, that a run would meet if it
got there. The default ones are certain: the types don't overlap at
all. With `--strict`, the checker also says where they might, where the
type found is wider than the type wanted (`int | str` where an `int`
goes).

| Warning | What it would be when the program runs |
|---|---|
| A builtin given a value of the wrong type: `str::len(n)` with `n` an `int` | `:not-a-string`, `:not-an-int`, `:not-a-number`, `:not-a-cons`, `:not-a-tuple` ... |
| `cons` onto something that isn't a list | `:not-a-list` |
| A test that isn't a boolean: `if lookup(k, t) { ... }` | `:not-a-bool` |
| A call of something that isn't a function, or with the wrong number of arguments through a function value | `:not-a-function`, `:arity` |
| A `match` whose arms don't cover the topic's type, when it has no `any` in it | `:no-match` |
| An arm no value can reach: its pattern can't fit what's left | (dead code) |
| A message no `recv` of the actor can take | a dead letter |
| `join` of an actor; `send`, `after` or `monitor` with a task | `:not-a-task`, `:not-an-actor` |
| `address`, `read` or `open` in a function that waits | `:not-a-task` |
| A device's messages that the actor connected to it can never take | every one a dead letter |

The fifth row closes a question `SYNTAX.md` left open (3, D246): a
message sent as `:stop()` to an actor that takes `:stop` is a dead
letter, and it's found when the program is compiled.

## Processes

A pid's type says what kind of process it is, which the language
already distinguishes (D243): `fork e` gives a `task<T>`, `T` being
`e`'s type, and `join` on it gives `:ok(T) | :error(any) | :ended`;
`spawn e` and `connect(...) { e }` give an `actor<M>`; `address()`
gives an `address<M>`; `self` is the actor's own `actor<M>`.

### An actor's mailbox, from its `recv`s

There's no selective receive: whatever `recv` an actor is in takes the
next message, or drops it as a dead letter. So a message that no `recv`
the actor could ever be in can take is certainly a dead letter, and
what the actor takes is the union of all those `recv`s' patterns.

`classify.ts` already knows which functions wait, and how they call
each other: each `recv` reachable by tail calls from the function a
`spawn` starts is one the actor can be in. For each `spawn`, the
checker takes the union of those `recv`s' patterns as types: a literal
is its type, `:tag(x, y)` is `:tag(any, any)`, and a name or `_` alone
is `any`. That's `M`, with no annotation:

```
fun counter(n) {
    recv {
        :inc()   => counter(n + 1),
        :get(to) => { send(to, :count(n)); counter(n) }
        :stop    => n,
    }
}

let c = spawn counter(0);    // actor<:inc() | :get(any) | :stop>
send(c, :inc);               // warning: counter takes :inc(), not :inc
```

An actor with an arm that takes anything (`msg => ...`) takes `any`,
and nothing sent to it is warned about. That's right: it takes
everything.

A `recv` annotation gives the parts their types, so the arms that bind
them know what they hold, and the checker checks the arms against it.
An arm for a message that isn't in `M` is unreachable, and a message in
`M` that no arm takes is a dead letter waiting to happen:

```
type CounterMsg = :inc() | :get(address<:count(int)>) | :stop

fun counter(n: int) recv CounterMsg {
    recv {
        :inc()   => counter(n + 1),
        :get(to) => { send(to, :count(n)); counter(n) }   // to: address<:count(int)>
        :stop    => n,
    }
}
```

### Messages from the runtime

A device sends what `LANGUAGE.md` says it does, and those have types
too: the keyboard sends `[str | sym, ...list<sym>]`, a timer sends what
`after` was given, `monitor` brings `:exit(pid, any)`, and a file read by lines
`:open(pid) | :line(pid, str) | :eof(pid)`. These are checked
against the connected actor's mailbox as `send`s are. `open` gives a
`Result<pid, any>` and `read` of a device an `any`, at first; a typed
device is a question below.

`ask(pid, msg)` gives `any`, since its answer's type is the protocol's,
not `ask`'s. `let n: int = ask(c, :get())` says what's expected, and is
checked from there on.

## Reporting

Warnings go to stderr as compile errors do, with the place and what a
run would do:

```
counter.sly:9:1: warning: counter takes :inc() | :get(any) | :stop; :inc is none of those, a dead letter
```

`slightc` still compiles and links, and exits 0. With `--strict`, it
also reports what might go wrong, and exits 1 if it reported anything.
`slightc --check file` checks and reports without compiling, for
editors and for tests.

A program in the repository is to check clean: `make types` runs
`slightc --check` over every test, example and the guide's code, and
fails on a warning unless the program's header lists it (`; warns:
9:1`), as some tests mean to fault. The checker's own tests are unit
tests, `compiler/tests/check.test.ts`, as the reader's are, and golden
tests of whole programs' warnings.

## In the compiler

- `compiler/src/types.ts`: types as s-expressions, as everything else
  is: what fits what, union, narrowing by a pattern, and reading a type
  from its s-expression.
- `compiler/src/check.ts`: the pass. It runs on the forms `expand.ts`
  gives (all `cond` and `match`, includes spliced in), before
  `split.ts`, so its places are the source's, and it uses
  `classify.ts`'s graph of functions that wait. It drops `deftype`,
  `sig` and `the` from what goes on, so code generation doesn't change.
- `compiler/src/sly.ts`: the `:` annotations, `type`, and `fun f<A>`.
- Builtins' types next to their arities; the prelude's in `sig`s.

A guess at its size: 1,500 to 2,500 lines of TypeScript, most of it
narrowing and what fits what, against the compiler's 4,800 now. It
would be ported with the rest when the compiler self-hosts.

## Building it

1. **The lint.** `types.ts` and `check.ts`, builtins' types, inference
   in a body, narrowing, and the warnings that need no annotations:
   builtins, tests, `:no-match` and unreachable arms. `slightc
   --check`, and `make types` clean over the repository.
2. **Processes.** Task, actor and address types; mailboxes from
   `recv`; dead letters; `join` of an actor and `send` to a task.
3. **Annotations.** `deftype`, `sig`, `the`, the `.sly` syntax for
   them, generics, and the prelude's `sig`s. Needs Stevan's word first:
   these are new forms.
4. **`--strict`.**
5. Perhaps, later and measured: types in code generation (a tag test
   left out where the type is certain, floats kept unboxed in a loop).

Each step leaves every program compiling and running as before, and
the tests passing.

## Questions

1. **Warnings, or errors?** Proposed: warnings, since a slight program
   with a bug in one process still runs the rest, and `--strict` for
   errors. Rejected: errors by default, which makes every gap in the
   checker a program that won't build.
2. **The `:` in `.sly`.** `x: int` needs a `:` that isn't a keyword's.
   Proposed: a `:` followed by white space is an annotation's, and
   `x:int` an error that says to add the space. Rejected: `x :: int`,
   since `::` is in names already, and `int x`, C's, which reads badly
   with `fun`.
3. **New forms.** `deftype`, `sig` and `the` are special forms, though
   ones that compile to nothing. Rejected: types in comments, as JSDoc
   and Flow's comments do, which keeps the language as it is but gives
   the `.sly` reader nowhere to put `x: int`.
4. **Parameters' types from their use.** A parameter used as `x + 1`
   is a `num`, so `wrap_around("a", 1)` could be found with no
   annotation (Dialyzer's success typings). Proposed: not at first:
   parameters are `any` till annotated, which is simpler and never
   wrong. Worth trying once step 3 is in.
5. **Contracts (depth 1).** `SLIGHT_CHECK=1` could check each `sig`
   when its function is entered, faulting where a wrong value comes in
   from `any`. Proposed: not now.
6. **Typed devices.** `open(:fs/read, p)` could give a
   `device<:line(pid, str) | :eof(pid) | ...>`, and `read` of it that
   type, and `(:read how)` would change it. Proposed: later; `read` of a
   device is `any` till then.
7. **Types in code generation.** Proposed: not until the checker has
   been used for a while, and then only with benchmarks to show it.
   Erasure is what lets a wrong checker cost only a warning.

## Prior art

- **Dialyzer**, Erlang's: success typings, inferred with no annotations,
  warning only about what's certain. The rule this proposal takes, for
  the default.
- **Elixir's set-theoretic types** (since 1.17): gradual, inferred from
  patterns and guards, reported as warnings, in a language whose values
  and processes are much like slight's. The nearest relative.
- **TypeScript**: structural types, literal types, unions narrowed by
  tests, `any`. What `.sly`'s annotations look like, and what Stevan
  writes every day.
- **Typed Racket**: types for a Lisp that already existed, with `(: f
  ...)` beside the definition, and occurrence typing, its narrowing.
- **Gleam**, **Akka Typed** and **Pony**: typed actors, a pid with the
  type of what it takes (`Subject(msg)`, `ActorRef[T]`). The idea of
  `actor<M>`; here `M` is inferred from `recv` rather than declared.
