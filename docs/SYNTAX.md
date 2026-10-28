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

Not goals: types, macros, new semantics, or replacing the s-expression
syntax.

## Files

A file in this syntax ends in `.sly`; one in s-expressions keeps
`.slight`. `slightc` picks the reader by the
extension, and `@include` of either kind works from either kind. The
prelude and `lib/` stay s-expressions.

## Lexical structure

**White space** separates tokens and is otherwise ignored. Newlines
mean nothing: items are separated by `;`.

**Comments:** `//` to the end of the line. There is no block comment.

**Doc blocks** are as now: between top-level items, a line `=doc` begins
one, and a line `=cut` (or the end of the file) ends it. A line that
starts with `=` and a letter anywhere else is an error, as now.

**Identifiers** name functions, locals and builtins:

```
ident  = (letter | "_") (letter | digit | "_")* "?"?
name   = ident ("::" ident)*
```

So `table::get`, `nil?`, `str_to_int`, `Consumer`, `tty::screen::rows`.
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
by `(` is a tagged list (below).

**Literals:**

- Integers and floats as slight writes them: `42`, `-7`, `3.14`,
  `1.5e3`. A `-` directly before a digit, where an operand is expected,
  is part of the number.
- Strings as slight writes them, with the same escapes (`\"`, `\\`,
  `\n`, `\t`, `\r`, `\e`, `\u{hex}`), over several lines if need be.
- `true` and `false` are `#true` and `#false`.
- `[]` is `()`.

**Reserved words:** `fn`, `let`, `if`, `else`, `cond`, `case`, `match`,
`recv`, `spawn`, `fork`, `connect`, `yield`, `true`, `false`, `self`,
`parent`. A slight name that is one of these is written as a raw name.

**Special names:** `self` is `$$`, `parent` is `^$$`, and `@ARGV`,
`%ENV` and `@include` are written as they are. `%ENV` is one token
wherever `%` is directly followed by `ENV`.

**Punctuation:** `( ) [ ] { } , ; => ... :: |>` and the operators below.

## Grammar

In EBNF, with `{ x }` for zero or more, `[ x ]` for optional, and
quoted terminals. `sep(x, s)` is `[ x { s x } [ s ] ]`: a list of `x`
separated by `s`, a trailing one allowed.

```
file      = { item }
item      = fndef | doc | include ";" | "let" ident "=" expr ";" | expr ";"
fndef     = "fn" name "(" sep(ident, ",") ")" block
include   = "@include" "(" (string | keyword) ")"
rawname   = "`" any characters but "`" "`"

block     = "{" { stmt } [ expr ] "}"
stmt      = "let" pattern "=" expr ";" | expr ";"

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
lambda    = "fn" "(" sep(ident, ",") ")" ( "=>" expr | block )

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
be left out, as in Rust.

The expression after `if`, `match` or `case`, before its `{`, can't
itself be a bare block, so `if x {` always opens the `if`'s block. Nor
can it be a bare `recv`: in `match recv {`, the `{` opens the `recv`'s
arms. Write `recv { ... }` itself, or `match (recv) { ... }`.

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
| `fn f(a, b) { body }` | `(defun f (a b) body...)` |
| `fn(a) => e` | `(lambda (a) e)` |
| `fn(a) { body }` | `(lambda (a) body...)` |
| `let x = e;` | `(let x e)` |
| `let p = e; rest` (`p` not a name) | `(match e (p rest...))` |
| `{ s1; s2; e }` as a body | `s1 s2 e`, the forms of the body |
| `{ s1; s2; e }` as an expression | `(do s1 s2 e)` |

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
`(list ...)`, and is made once rather than each time. This is why
`quote` has no syntax of its own. It also does away with one of the
s-expression syntax's traps: `'(:inc)` is `((quote inc))`, but
`[:inc]` is the list `(inc)`.

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

**Top level:** a file is items: `fn` definitions, includes, doc blocks,
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
fn log(what, msg) {
    pprint([self, what, msg])
}

fn Consumer(f) {
    let msg = recv;
    log(:RECV, msg);
    match msg {
        :on-next(v)     => { f(v); Consumer(f) }
        :on-completed() => [],
        :on-error(e)    => raise(e),
    }
}

fn Operation(op, next) {
    let msg = recv;
    log(:OPER, msg);
    match msg {
        :on-next(v) => { send(next, :on-next(op(v))); Operation(op, next) }
        _           => send(next, msg),
    }
}

fn Producer(sub, items) {
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

let op1 = spawn Operation(fn(x) => x + 100, o1);
let op2 = spawn Operation(fn(x) => x + 200, o2);

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

fn table::get(key, table) {
    match table {
        []                => :not-found,
        [[k, v], ...rest] => if k == key { v } else { table::get(key, rest) },
    }
}

fn table::del(key, table) {
    table::del_loop(key, table, [])
}

fn table::del_loop(key, table, seen) {
    match table {
        []              => reverse(seen),
        [pair, ...rest] =>
            if car(pair) == key { append(reverse(seen), rest) }
            else { table::del_loop(key, rest, [pair, ...seen]) }
    }
}

fn table::put(key, value, table) {
    [[key, value], ...table::del(key, table)]
}

fn table::keys(table) {
    table |> map(car)
}
```

```
// The store, as chapter 4 leaves it: an actor that runs commands on a
// table, and answers each at the address that came with it.

@include("tut-table.sly");

fn run_command(table, cmd) {
    match cmd {
        :put(key, value) => [table::put(key, value, table), :ok(value)],
        :get(key)        => [table, found(table::get(key, table))],
        :del(key)        => [table::del(key, table), :ok([])],
        :keys()          => [table, :ok(table::keys(table))],
        :incr(key)       => incr(key, table),
        _                => [table, :error(:unknown(cmd))],
    }
}

fn found(value) {
    if value == :not-found { :error(:not-found) } else { :ok(value) }
}

// A key that isn't there counts from 0.
fn incr(key, table) {
    let n = table::get(key, table);
    let next = if n == :not-found { 1 } else { n + 1 };
    [table::put(key, next, table), :ok(next)]
}

fn store(table) {
    recv {
        :do(cmd, to) => {
            let [next, answer] = run_command(table, cmd);
            reply(to, answer);
            store(next)
        }
    }
}

// A task's way to run a command: send it, and wait for the reply.
fn call(s, cmd) {
    ask(s, :do(cmd))
}
```
