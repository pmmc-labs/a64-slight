# A C-style syntax for slight

Agreed (D246); the reader is built (`compiler/src/sly.ts`), the warning
and the converters below aren't yet. Nothing here changes what a program
means: this is a second way to write the same programs, read into the
same s-expressions the compiler already takes. Where it touches a choice
that is really about the language (what `==` means, say), it says so,
and says how it was settled, under Questions.

## Goals

- **A front end, not a new language.** A second reader turns this syntax
  into the s-expressions `compiler/src/reader.ts` makes today, with
  their source positions, and everything from `expand.ts` on is
  unchanged. Every program can be written in either syntax, and the two
  can include each other.
- **Mechanical conversion both ways.** `slightc` can turn a file from
  one syntax into the other. A program converted and converted back
  means what it did; s-expressions converted and back may come back
  normalized (`(list 1 2)` as `'(1 2)`), never changed in meaning.
- **Familiar to C, JavaScript and Rust programmers,** with ML's
  expression-only habits: every construct has a value, and there are no
  statements.
- **Data stays s-expressions.** What `pprint`, `sexp/print`, `rpc`, log
  files and `:source` read and write is unchanged. Code and data look
  different, as they do in Elixir.

Not goals: macros, new semantics, or replacing the s-expression
syntax. Types came later, for both syntaxes (D247, `docs/TYPES.md`);
this says how `.sly` writes them.

## Files

A file in this syntax ends in `.sly`; one in s-expressions keeps
`.slight`. `slightc` picks the reader by the
extension, and `@include` of either kind works from either kind. The
prelude and `lib/` stay s-expressions. A module's header (D248) is
`X.slyi` in this syntax, `X.sli` in s-expressions, and either can go with
either kind of module.

## Lexical structure

**White space** separates tokens and is otherwise ignored. Items are
separated by `;`, which can be left out after a `}` (see Grammar).
Newlines mean nothing but in one place: a `}` that ends a line ends the
expression before a `(` or `-` that starts the next.

**Comments:** `//` to the end of the line. There is no block comment.

**Doc blocks** are as now: between top-level items, a line `=doc` begins
one, and a line `=cut` (or the end of the file) ends it. A line that
starts with `=` and a letter anywhere else is an error, as now.

**Identifiers** name functions, locals and builtins:

```
ident  = "$"? (letter | "_") (letter | digit | "_")* "?"?
name   = ident ("::" ident)*
```

So `table::get`, `nil?`, `str_to_int`, `Consumer`, `tty::screen::rows`,
`$f`. A name may start with the sigil `$`, as a slight name may, which
the rules below leave as it is: `$my_var` is `$my-var` (D246). `$$`
isn't a name: it's `self`.
Case is significant and carries no meaning: capitals are free for
anything (`Consumer` for a function an actor runs, say).

A **raw name**, between backticks, is any slight name as it is:
`` `str/<` ``, `` `go-to-x` ``. It's for the names the mapping below
can't carry, and the converter writes one wherever it must.

**Name mapping.** An identifier means the slight name made by these
rewrites, in order:

1. `::` becomes `/`
2. `_to_` becomes `->`
3. `_` becomes `-`

So `table::del_loop` is `table/del-loop`, `str_to_int` is `str->int`,
`map_to_list` is `map->list`, `ask_within` is `ask-within`. A lone `_`
is the wildcard, in patterns only. The converter, going the other way,
writes a name in identifier form only if the rewrites give it back
exactly, and as a raw name otherwise: `go-to-x` would come back as
`go->x`, so it's written `` `go-to-x` ``.

**Keywords** keep slight's spelling, hyphens and slashes included,
because nothing after `:` can be an operator:

```
keyword = ":" kchar+
kchar   = any character but white space and ( ) [ ] { } , ; " ' `
```

So `:on-next`, `:fs/read`, `:tcp/listen`, `:RECV`. A keyword followed
by `(` is a tagged list (below). A keyword doesn't end in `>`, so that
a type's `actor<:ok>` closes (D247); `:>` alone is a keyword.

**A type's `:`** has white space on each side (D247): `x : int`. One
touching a name on its right begins a keyword, and one touching what's
before it, `x: int`, is an error that says to write `x : int`.

**Literals:**

- Integers and floats as slight writes them: `42`, `-7`, `3.14`,
  `1.5e3`. A `-` directly before a digit, where an operand is expected,
  is part of the number.
- Strings as slight writes them, with the same escapes (`\"`, `\\`,
  `\n`, `\t`, `\r`, `\e`, `\u{hex}`), over several lines if need be.
- `true` and `false` are `#true` and `#false`.
- `[]` is `()`.

**Reserved words:** `fun`, `let`, `if`, `else`, `cond`, `case`, `match`,
`recv`, `spawn`, `fork`, `connect`, `yield`, `true`, `false`, `self`,
`parent`. A slight name that is one of these is written as a raw name.

**Special names:** `self` is `$$`, `parent` is `^$$`, and `@ARGV`,
`%ENV` and `@include` are written as they are. `%ENV` is one token
wherever `%` is directly followed by `ENV`.

**Punctuation:** `( ) [ ] { } , ; => ... :: |> '` and the operators
below; and in types, `:`, `|` and `->`.

## Grammar

In EBNF, with `{ x }` for zero or more, `[ x ]` for optional, and
quoted terminals. `sep(x, s)` is `[ x { s x } [ s ] ]`: a list of `x`
separated by `s`, a trailing one allowed.

```
file      = { item }
item      = fndef | doc | include ";" | typedef ";" | "let" ident [ ":" type ] "=" expr end | expr end
fndef     = "fun" name [ tvars ] "(" sep(param, ",") ")" [ ":" type ] [ "recv" type ] ( block | ";" )
param     = ident [ ":" type ]
tvars     = "<" sep(ident, ",") ">"
typedef   = "type" name [ tvars ] "=" type      (type is a name, but here)
include   = "@include" "(" (string | keyword) ")"
rawname   = "`" any characters but "`" "`"

block     = "{" { stmt } [ expr ] "}"
stmt      = "let" pattern [ ":" type ] "=" expr end | expr end
end       = ";"            (optional after an expr that ends with "}")

expr      = "spawn" expr | "fork" expr | "yield" expr
          | "connect" "(" sep(expr, ",") ")" block
          | pipe
pipe      = or { "|>" or }
or        = and { "||" and }
and       = eq { "&&" eq }
eq        = cmp [ ("==" | "!=") cmp ]
cmp       = add [ ("<" | "<=" | ">" | ">=") add ]
add       = mul { ("+" | "-" | "++") mul }
mul       = unary { ("*" | "/" | "%") unary }
unary     = ("-" | "!") unary | postfix
postfix   = primary { "(" sep(expr, ",") ")" }
primary   = literal | name | rawname | keyword | tagged | list | tuple
          | "self" | "parent" | "@ARGV" | "%ENV"
          | lambda | if | cond | case | match | recv | block
          | "(" expr ")"

tagged    = keyword "(" sep(expr | spread, ",") ")"
list      = "[" sep(expr, ",") [ "," spread ] "]" | "[" spread "]"
spread    = "..." expr
tuple     = "#(" sep(expr, ",") ")"
lambda    = "fun" "(" sep(param, ",") ")" [ ":" type ] ( "=>" expr | block )

if        = "if" expr block [ "else" ( if | block ) ]
cond      = "cond" "{" sep(expr "=>" body, ",") "}"
case      = "case" expr "{" sep(caseval "=>" body, ",") "}"
caseval   = expr | "_"
match     = "match" expr "{" sep(pattern "=>" body, ",") "}"
recv      = "recv" [ "{" sep(pattern "=>" body, ",") "}" ]
body      = expr
```

An arm's body is one expression; a block is one, for several. After an
arm whose body ends with `}` (a block, an `if`, a `match`), the `,` may
be left out, as in Rust; and so may the `;` after an item or a statement
that ends with `}`:

```
fun count(table) {
    let n = match table { [] => 0, [_, ...rest] => 1 + count(rest) }
    if n > 9 { tty::write("big\n") }
    n
}
```

Since that rule looks at tokens, a `}` followed by `(` or `-` would go
on as a call or a subtraction, so a `}` that ends a line ends the
expression when the next line starts with either, as a statement could.
Any other operator there, `|>` say, goes on, since no statement starts
with one.

The expression after `if`, `match` or `case`, before its `{`, can't
itself be a bare block, so `if x {` always opens the `if`'s block. Nor
can it be a bare `recv`: in `match recv {`, the `{` opens the `recv`'s
arms. Write `recv { ... }` itself, or `match (recv) { ... }`.

### Types

```
type      = one { "|" one }
one       = name [ "<" sep(type, ",") ">" ]          int, list<T>, task<T>, Tree<int>
          | "true" | "false" | keyword
          | keyword "(" sep(type | "..." type, ",") ")"
          | "[" sep(type, ",") [ "," "..." type ] "]" | "[" "..." type "]"
          | "#(" sep(type, ",") ")"
          | "fun" [ "(" sep(type, ",") ")" "->" type ]
          | "(" type ")"
```

A type's `->` takes all the type after it: `fun(A) -> B | C` gives
`B | C`.

### Patterns

```
pattern   = "_" | ident | literal | keyword
          | "[" sep(pattern, ",") [ "," "..." pattern ] "]" | "[" "..." pattern "]"
          | keyword "(" sep(pattern, ",") [ "," "..." pattern ] ")"
          | "#(" sep(pattern, ",") ")"
```

`ident` here is a plain identifier (no `::`), which binds. A literal is
a number (negative ones too), a string, `true`, `false` or `[]`.

## Operators

From tightest to loosest:

| Operators | Associativity | slight |
|---|---|---|
| `f(args)` | left | `(f args...)` |
| `-x` `!x` | prefix | `(- 0 x)` for a non-literal (so `-x` of `0.0` is `0.0`, not `-0.0`), `(not x)` |
| `*` `/` `%` | left | `(* a b)` `(/ a b)` `(% a b)` |
| `+` `-` `++` | left | `(+ a b)` `(- a b)` `(~ a b)` |
| `<` `<=` `>` `>=` | none | `(< a b)` ... |
| `==` `!=` | none | `(eq? a b)` `(ne? a b)` (open question 1) |
| `&&` | left | `(and a b)` |
| `\|\|` | left | `(or a b)` |
| `\|>` | left | `x \|> f(a)` is `(f a x)`; `x \|> f` is `(f x)` |
| `spawn` `fork` `yield` | prefix, takes the whole expression after it | `(spawn e)` ... |

`a + b + c` is `(+ (+ a b) c)`, since slight's `+` takes two. `a < b < c`
is an error, as is `a == b == c`: write `&&`. `/` is slight's `/`, which
always gives a float; integer division stays the function `div(a, b)`.
`&&` and `||` take booleans, as `and` and `or` do: there's still no
truthiness.

`|>` puts its left side last among the arguments of the call on its
right (the outermost one, if that's a call of a call), which suits the
prelude, whose functions take their data last:
`xs |> filter(even?) |> map(square)` is
`(map square (filter even? xs))`. The `str::` builtins take the string
first, so the pipe doesn't help with them.

## What each form becomes

**Definitions and bindings**

| This syntax | slight |
|---|---|
| `fun f(a, b) { body }` | `(defun f (a b) body...)` |
| `fun(a) => e` | `(lambda (a) e)` |
| `fun(a) { body }` | `(lambda (a) body...)` |
| `let x = e;` | `(let x e)` |
| `let p = e; rest` (`p` not a name) | `(match e (p rest...))` |
| `{ s1; s2; e }` as a body | `s1 s2 e`, the forms of the body |
| `{ s1; s2; e }` as an expression | `(do s1 s2 e)` |
| `fun f(x : int, y) : str recv M { body }` | `(sig f (int any) str (recv M))` then the `defun` (D247) |
| `fun f<A>(x : A) : A { body }` | `(sig (f A) (A) A)` then the `defun` |
| `let x : T = e;` | `(let x (the T e))` |
| `fun(x : T) => e` | `(the (fun (T) any) (lambda (x) e))` |
| `type Name<A> = T;` | `(deftype (Name A) T)` |
| `fun f(x : int) : str;` | `(sig f (int) str)`: a prototype, as a header (`X.slyi`, D248) declares a function |

A block's value is its last expression, whether or not a `;` follows it.
An empty block, `{}`, is `()`.

A `let` whose pattern isn't a plain name is a one-clause `match` over
the rest of the block, so `let [next, answer] = run(cmd);` binds both,
and a value of another shape faults with `:no-match`. It can't be the
last item of a block: there would be nothing to bind for.

**Data**

| This syntax | slight |
|---|---|
| `[a, b, c]` | `(list a b c)` |
| `[a, b, ...xs]` | `(cons a (cons b xs))` |
| `[...xs]` | `xs` |
| `:tag` | `'tag` |
| `:tag(a, b)` | `(list :tag a b)` |
| `:tag()` | `(list :tag)` |
| `:tag(a, ...xs)` | `(cons :tag (cons a xs))` |
| `#(a, b)` | `(tuple a b)` |

A list or tagged list whose elements are all literals (numbers, strings,
booleans, keywords, `[]`, and such lists) is read as a quoted constant:
`[1, 2, [:a, "b"]]` is `'(1 2 (a "b"))`. It means the same as
`(list ...)`, and is made once rather than each time. It also does
away with one of the s-expression syntax's traps: `'(:inc)` is
`((quote inc))`, but `[:inc]` is the list `(inc)`.

**Quoted s-expressions.** `'` and what follows it is that datum,
quoted, read as the s-expression reader reads one: `'((lambda (x y) (+
x y)) 10 20)` is the program it looks like, as data, where the same
written with `[...]` and keywords would be `[[:lambda, [:x, :y], [:+,
:x, :y]], 10, 20]` (D246). A list runs to its matching `)`, and inside
it slight's rules apply, `;` comments included. An atom (`'zebra`,
`'42`, `'"s"`) ends where a keyword does, so `['a, 'b]` is the list
`(a b)`. A quoted datum is a literal, in a pattern too.

Spread is only at the end, since a list can only be added to at the
front; `[...xs, a]` is an error (write `append(xs, [a])`).

**Control**

| This syntax | slight |
|---|---|
| `if c { a } else { b }` | `(if c a b)` |
| `if c { a }` | `(if c a)`, `()` when `c` is false |
| `if c { a } else if d { b } else { e }` | `(if c a (if d b e))` |
| `cond { c => a, d => b }` | `(cond (c a) (d b))`, which faults if neither holds |
| `case x { 1 => a, _ => b }` | `(case x (1 a) (#true b))` |
| `match x { p => a, ... }` | `(match x (p a) ...)` |

`when` has no syntax of its own: it's an `if` without an `else`.

**Processes**

| This syntax | slight |
|---|---|
| `recv { p => a, ... }` | `(recv (p a) ...)` |
| `recv` | `(recv)` |
| `spawn e` | `(spawn e)` |
| `fork e` | `(fork e)` |
| `yield e` | `(yield e)` |
| `connect(:tcp, "h:7000") { body }` | `(connect :tcp "h:7000" body...)` |
| `connect(conn) { body }` | `(connect conn body...)` |
| `self`, `parent` | `$$`, `^$$` |

The `recv` rule is unchanged, and reads naturally: a `recv`, and a call
to a function that waits, can go where a tail call could, which is the
last expression of a block, or the value of a `let` there
(`let msg = recv;`).

**Calls** are `f(a, b)`, whatever `f` is: a function, a builtin, a
local holding a function, or an expression (`adder(1)(2)`). Builtins
keep their names, through the mapping: `send(pid, msg)`,
`join(p)`, `str::len(s)`, `math::sqrt(x)`, `tty::write(...)`.

**Top level:** a file is items: `fun` definitions, includes, doc blocks,
and `let`s and expressions, which are the root's body, in order, as
now. A top-level `let` with a pattern isn't allowed (it would need the
rest of the file as its body); write a `match`.

## Errors

A syntax error is reported as now, `prog.sly:line:col: message`, and
`slightc` exits 1. Every form the reader makes carries the position of
the token it came from, so the compiler's own errors and the runtime's
fault sites (`car at prog.sly:3:5`) point into the `.sly` file.

## Converting (not built yet)

- `slightc --to-sly file.slight` writes the program in this syntax, and
  `slightc --to-slight file.sly` in s-expressions. Comments and doc
  blocks are kept, attached to the item they precede.
- Converting `.sly` to `.slight` and back gives the same text, laid out by
  the converter's rules.
- Converting `.slight` to `.sly` and back gives the same program, but
  maybe not the same text: `(list 1 2)` comes back `'(1 2)`, `when` as
  `if`, and layout as the converter lays it out.
- The guide's code could be checked in both syntaxes: each program
  converted, and run to the same `.expected`.

## Questions

Settled by Stevan (D246):

1. **`==` is structural.** `==` and `!=` are slight's `eq?` and `ne?`,
   because that's what a C or JavaScript programmer reaches for, and in
   the programs converted so far every comparison was structural (keys,
   keywords). So `1 == 1.0` is false, as `(eq? 1 1.0)` is. slight's
   numeric `==` is the raw name `` `==`(a, b) ``. Rejected: `==` as
   slight's numeric `==`, which keeps the name's meaning and makes
   `name == "ann"` a fault; and both, with `===` for structure.
2. **The extension is `.sly`.** Rejected: `.sl`, which other tools use,
   and `.slc`.
3. **`:tag` and `:tag()` stay different,** as they are in slight, and
   the compiler is to warn when one tag is used in both shapes in one
   program: a message sent as `:tag()` and matched as `:tag` is a dead
   letter, found only when it runs. Not built yet. Rejected: making a
   bare `:tag` mean the one-element list in some positions, which
   changes what values are; and no warning.

Still open:

4. **A conditional expression,** `c ? a : b`. A one-line `if ... else`
   is longer than slight's `(if c a b)`. It's left out for now: `?` ends
   identifiers (`nil?`) and `:` starts keywords, so `c ? a :b` would
   read wrongly, and it needs spaces to be safe.
5. **`become`,** Rust's reserved word, for a tail call the compiler
   checks is one: `become counter(n + 1)`. It would make loops and an
   actor's next state visible. Left out of the first version: the
   compiler already knows what's a tail call.
6. **Printing values in this syntax.** Not proposed: data stays
   s-expressions, so what a program prints, logs and sends is the same
   whichever syntax wrote it.
7. **Self-hosting.** A parser for this syntax, written in slight, is
   larger than the s-expression reader, perhaps three times. The reader
   is a precedence climber over a token list, slight-shaped like the
   rest of the compiler, so that it ports the same way.

## Examples

### A pipeline of actors

```
fun log(what, msg) {
    pprint([self, what, msg])
}

fun Consumer(f) {
    let msg = recv;
    log(:RECV, msg);
    match msg {
        :on-next(v)     => { f(v); Consumer(f) }
        :on-completed() => [],
        :on-error(e)    => raise(e),
    }
}

fun Operation(op, next) {
    let msg = recv;
    log(:OPER, msg);
    match msg {
        :on-next(v) => { send(next, :on-next(op(v))); Operation(op, next) }
        _           => send(next, msg),
    }
}

fun Producer(sub, items) {
    match items {
        [] => {
            pprint([self, :DONE]);
            send(sub, :on-completed())
        }
        [item, ...rest] => {
            send(sub, :on-next(item));
            pprint([self, :SENT, item]);
            yield Producer(sub, rest)
        }
    }
}

let o1 = spawn Consumer(pprint);
let o2 = spawn Consumer(pprint);

let op1 = spawn Operation(fun(x) => x + 100, o1);
let op2 = spawn Operation(fun(x) => x + 200, o2);

let p1 = fork Producer(op1, [10, 20, 30, 40, 50, 60]);
let p2 = fork Producer(op2, [1, 2, 3, 4, 5, 6, 7, 8, 9]);

pprint([join(p1), join(p2)]);
pprint([read(o1), read(o2)]);
pprint([read(op1), read(op2)]);
pprint(:done);
```

### The tutorial's store

`docs/guide/code/tut-table.slight` and `tut-store.slight`:

```
// The table: a list of [key, value] pairs, newest first, taken apart
// with match.

fun table::get(key, table) {
    match table {
        []                => :not-found,
        [[k, v], ...rest] => if k == key { v } else { table::get(key, rest) },
    }
}

fun table::del(key, table) {
    table::del_loop(key, table, [])
}

fun table::del_loop(key, table, seen) {
    match table {
        []              => reverse(seen),
        [pair, ...rest] =>
            if car(pair) == key { append(reverse(seen), rest) }
            else { table::del_loop(key, rest, [pair, ...seen]) }
    }
}

fun table::put(key, value, table) {
    [[key, value], ...table::del(key, table)]
}

fun table::keys(table) {
    table |> map(car)
}
```

```
// The store, as chapter 4 leaves it: an actor that runs commands on a
// table, and answers each at the address that came with it.

@include("tut-table.sly");

fun run_command(table, cmd) {
    match cmd {
        :put(key, value) => [table::put(key, value, table), :ok(value)],
        :get(key)        => [table, found(table::get(key, table))],
        :del(key)        => [table::del(key, table), :ok([])],
        :keys()          => [table, :ok(table::keys(table))],
        :incr(key)       => incr(key, table),
        _                => [table, :error(:unknown(cmd))],
    }
}

fun found(value) {
    if value == :not-found { :error(:not-found) } else { :ok(value) }
}

// A key that isn't there counts from 0.
fun incr(key, table) {
    let n = table::get(key, table);
    let next = if n == :not-found { 1 } else { n + 1 };
    [table::put(key, next, table), :ok(next)]
}

fun store(table) {
    recv {
        :do(cmd, to) => {
            let [next, answer] = run_command(table, cmd);
            reply(to, answer);
            store(next)
        }
    }
}

// A task's way to run a command: send it, and wait for the reply.
fun call(s, cmd) {
    ask(s, :do(cmd))
}
```
