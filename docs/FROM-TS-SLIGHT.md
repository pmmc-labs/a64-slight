# a64-slight, for someone who knows ts-slight

Stevan, this is a crib sheet for getting used to the changes. Every
special form and builtin we have is listed. A name with no note works as
it did in ts-slight; a note means something changed. DESIGN.md has the
full story, and DECISIONS.md has the reasons.

## The three big changes

If you read nothing else, read this.

1. **`recv` is no longer an expression.** It's the whole body of a
   top-level `defun`, written as a list of patterns, and you loop by
   tail-calling the function. `(let msg (recv))` in the middle of a
   function is gone, so send-then-wait becomes two functions.
2. **Tests must be booleans.** There's no truthiness. `cond`, and so
   `if`, `when`, `and` and `or`, faults on a test that isn't `#true` or
   `#false`. A `cond` that runs out of clauses faults too.
3. **Errors are values.** `join` gives `(:ok value)` or `(:error reason)`,
   not the bare value. A fault ends only its own process, with
   `(:error (kind value site))`.

## How a program runs

- The program's shape is the same: top-level `defun`s are global, and
  every other top-level form, in order, is the root process's body.
  Several files can go on the command line, and together they're one
  program.
- **`@ARGV`** is the program's arguments, as a list of strings, but only
  the top level can see it. Internally the top-level forms are the body
  of a function, and `@ARGV` is that function's parameter. A `defun` that
  names it is a compile error, so pass it to the functions that need it.
  A lambda or a `fork` at the top level captures it like any other local.
  A number arrives as a string: `(string->int (car @ARGV))`.
- The runtime prints the root's value and a newline once nothing else can
  run. That isn't when the root ends: other processes keep going after
  it, and the root's value comes out last.
- Faults are logged to stderr as they happen: `fault: ...` in the root,
  `fault in #<pid N>: ...` anywhere else. Dead letters are logged too
  (see `recv`), and so is an error that ends the root (`error: ...`).
- The exit status is 1 if the root ended with an error, or if it never
  ended (`deadlock: ...`, when everything is waiting and nothing can wake
  it up). Otherwise it's 0.
- Preemption is automatic, every 1000 reductions, so the `(yield (loop
  ...))` wrappers aren't needed any more.
- Integers are 63-bit, and overflow faults. Floats are a type of their
  own: to `eq?`, `3.0` and `3` are different values.
- Each process's heap holds up to 64 MB, and it's collected only when a
  receive function asks for its next message. A long loop that allocates
  inside one handler never collects, and neither does the root (unless it
  ends in a receive function), so either can hit the limit and fault with
  `:heap`. Split long work across messages.

## Special forms

**`defun`**: top level only, so there are no local defuns. A function
takes at most 8 parameters.

**`lambda`**: captures its free variables by value (a copy, not an
environment). Data is immutable, so you'll rarely notice, with one
exception: a lambda can't call itself, because its `let` name isn't bound
yet when it's made. Use a `defun` for that. A lambda takes at most 8
parameters, and it can't contain `recv` or call a function that waits in
one.

**`let`**: as before. It's allowed only directly in a body.

**`cond`**: each test must be `#true` or `#false` (otherwise the fault is
`:not-a-bool`), and running out of clauses faults (`:no-clause`). End with
`(#true ...)` if you want a default.

**`do`**: no change.

**`quote`**: no change, including the gotcha that `:a` inside a quoted
list reads as `(quote a)`. So `'(:inc)` is not the message `(:inc)`: write
`(list :inc)`, or `'(inc)`. This one bit me while writing this document.

**`fork`**: `(fork expr)` as before, and `$$` inside `expr` is the child.
What's new: `expr`'s free variables are deep-copied into the child, and
there can be at most 8 of them. `expr` may tail-call a receive function,
which is the usual way to start an actor: `(fork (counter 0))`.

**`recv`**: it has to be the whole body of a top-level `defun`, which
makes that function a *receive function*:

    (defun counter (n)
        (recv
            ((:inc)      (counter (+ n 1)))
            ((:get from) (send from (list :count n)) (counter n))))

The patterns are:

- `(:tag a b)` matches a list of exactly that length headed by `:tag`,
  and binds the rest by position. `_` in a position skips it.
- `:tag` matches that symbol.
- A bare name matches anything and binds it. A bare `_` matches anything
  and binds nothing.

The rules:

- Only the first message in the mailbox is looked at. There's no
  selective receive.
- If no clause matches, the message is dropped and logged
  (`dead letter: bogus (recv at file:line:col)`), and the function waits
  for the next one.
- There are no nested patterns, and no literals except the head keyword.
  Match the outer shape, then take the rest apart in the body.
- A *state function* is a receive function, or any function that
  tail-calls one. The compiler works out which functions they are, and
  only lets you call them in tail position, or as the body of `fork` or
  `connect`.

The payoff is that a waiting process has an empty stack, so it holds no
stack at all, and that's the moment the collector runs. Send-then-wait
looks like this:

    (defun ask (c)
        (send c (list :get $$))
        (answer))

    (defun answer ()
        (recv
            ((:count n) n)))

**`yield`**: `(yield expr)` pauses first, then evaluates `expr` as a tail
call. ts-slight evaluated `expr` first. You'll rarely need it now.

**`connect`**: now a kind of `fork`. See Devices below.

**`$$`, `^$$`**: no change, except that the root's `^$$` is `()`.

## Forms that become cond

The expander rewrites these into `cond` before the compiler sees them, so
they follow `cond`'s rules: tests must be booleans.

- **`if`**: `(if test then else)`. The else is optional, and without it
  you get `()`.
- **`when`**: gives `()` when the test is `#false`.
- **`case`**: `(case topic (value body...) ... (#true body...))`. The
  topic is evaluated once and compared with `eq?`, which is structural,
  so strings and lists match. A `#true` clause is the default. Without
  one, a case with no match gives `()` rather than faulting.
- **`and`, `or`**: short-circuiting, with any number of operands, and
  Scheme's rule for the last one. Every operand but the last must be a
  boolean, and the last is given back as it is, so `(and #true 5)` is 5
  and `(or #false "five")` is `"five"`. `(and)` is `#true` and `(or)` is
  `#false`. In ts-slight they were two-operand builtins. The
  non-short-circuiting `and?` and `or?` are gone.
- **`not`**: a prelude function, booleans only.

None of these names can be used for your own functions or variables.

## Includes

- `(@include "path/file.slight")` splices in the file's forms where it
  stands. The path is relative to the file that has the include.
- `(@include :fs)` and `(@include :test)` bring in the built-in libraries
  from `lib/`.
- Includes are top level only. Each file is included once however often
  it's asked for, and a cycle is a compile error.
- The prelude is an unwritten `(@include :prelude)`. You can define a
  function with a prelude name: your code then uses yours, and the
  prelude keeps using its own.
- It's not a module system: there's one namespace, and the whole program
  is compiled at once.

## Processes and messages

- **`send`**: no change. A message is any value, conventionally `(list
  :tag ...)`, and there's no `msg` constructor. A message to a process
  that has ended disappears.
- **`join`**: gives `(:ok value)` or `(:error reason)`. It works on any
  pid, any number of times, even after the process has ended. `(join $$)`
  faults.
- **`kill`**: ends the process with `(:error :killed)`, whatever it's
  doing. Returns `()`.
- **`raise`**: ends the current process with `(:error reason)`. There's
  no catch; whoever cares finds out through `join` or `monitor`.
- **`monitor`** (new): after `(monitor pid)`, you're sent `(:exit pid
  result)` when that process ends, or straight away if it already has.
  There are no links, and nothing is sent to a parent automatically.
- **`after`** (new): `(after ms pid msg)` sends `msg` once `ms`
  milliseconds have passed.
- **`sleep`**: a builtin now, rather than a syscall. It blocks only the
  calling process, and returns `()`.
- **Fault reasons** are `(kind value site)`. `kind` is a keyword such as
  `:overflow`, `:not-a-cons`, `:no-clause`, `:arity` or `:heap`. `value`
  is the offending value. `site` is a string like `"car at
  file:line:col"`. So `(join (fork (car ())))` gives `(:error
  (:not-a-cons () "car at ..."))`. The full list of kinds is in
  `runtime/rt.h`.

## Devices: the keyboard, files and sockets

`connect` is `fork` plus a connection: `(connect source [arg] expr)` runs
`expr` in a new process and hooks that process up to the source.
ts-slight's `(connect :keypress (Keyboard ...))` reads the same as it did.

**`:keypress`**: keys have the same shape as before, `(key mods...)`. A
printable key is a one-character string, and any other key is a name like
`:ArrowUp` or `:Enter`. The mods come in the order `:ctrl :alt :shift`.
Every connected process gets every key. Ctrl-C restores the terminal and
exits 130.

**Files**: `(connect :fs/read path expr)`.

- The new process's first message is `(:open f)`, where `f` is the
  device's pid.
- Then come lines, as `(:line f s)`, one at a time: the next line is read
  only when you take the last one.
- Finally `(:eof f)` arrives.
- `:fs/write` (which truncates the file) and `:fs/append` take `(:write x
  ...)` messages, rendered as `tty/write` renders them.
- If a file can't be opened, read or written, its owner ends with
  `(:error (:enoent path))` or similar. The names are errno's.

A reader:

    (defun reader (lines)
        (recv
            ((:open f)   (reader lines))
            ((:line f s) (reader (cons s lines)))
            ((:eof f)    (reverse lines))))

    (join (connect :fs/read "notes.txt" (reader ())))

**`disconnect`**: `(disconnect f)` closes the device `f`. A device also
closes when its owner ends.

**TCP clients**: `(connect :tcp "host:port" expr)`.

- The first message is `(:open c)`.
- Then come lines, as a file sends them, and `(:eof c)` when the other
  side closes.
- `(:write ...)` is buffered, so `send` still never blocks.

**TCP servers**: `(connect :tcp/listen port expr)`.

- The first message is `(:open l port)`. Port 0 means "pick one", and
  you're told which.
- Then `(:accept l conn)` arrives for each connection.
- `(connect conn expr)` hands a connection to a new process of its own,
  which then gets `(:open conn)`.

Socket errors are errno's names too (`:econnrefused`, `:eaddrinuse`), plus
`:enotfound` for a host that can't be found. For now, files and sockets
deal only in lines (no chunks yet), and sockets are IPv4 only.

## Builtins

### Numbers

- **`+ - *`**: an integer and a float give a float. Integer overflow
  faults.
- **`/`**: always returns a float, so `(/ 6 2)` is `3.0`. Dividing by
  zero faults instead of giving Infinity.
- **`div`** (new): integer division, truncating toward zero.
- **`%`**: integers only. It truncates toward zero, so `(% -7 2)` is -1,
  as in JavaScript.
- **`ceil floor round trunc`**: return integers. `round` sends halves up,
  as JavaScript does, so `(round -2.5)` is -2.
- **`sqrt pow sin cos tan exp`**: always return floats.
- **`abs min max`**: keep their argument's type.
- **`PI`**: no change.
- **`inc dec`**: no change; they're in the prelude.
- **Gone**: `rand` and `hex`.

### Comparison

- **`== != < <= > >=`**: numbers only, so anything else faults.
  `(== 1 1.0)` is `#true`.
- **`eq? ne?`**: structural, on any values. `(eq? 1 1.0)` is `#false`,
  since one is an integer and the other a float.

### Predicates

- **`nil? cons? sym? str? num? bool? lambda? pid?`**: no change. `sym?`
  says `#true` to `#true` and `#false`, since they're symbols.
- **`int?` and `float?`**: new.
- **Gone**: `true? false? builtin? list? atom? literal? callable?
  type-of`.

### Lists

- **`cons car cdr list`**: no change.
- **`c[ad]r`**: every combination up to four letters works (`cdar`,
  `cadddr`, ...), not just ts-slight's fixed set.
- **Gone**: `head` and `tail`.

### Strings

Strings are now bytes (UTF-8 by convention) rather than JavaScript
strings, so lengths and indexes count bytes: `(str-len "héllo")` is 6.

- **`str-len substring index-of`**: by byte.
- **`concat`**: no change. Non-strings are rendered as `pprint` shows
  them.
- **`~`**: two strings only.
- **`str-join`**: `(str-join sep xs)` takes a list. It used to be
  `concat-list`.
- **`str-split`**: an empty separator splits into bytes.
- **`starts-with?`, `ends-with?`**: they've gained a `?`.
- **`uc lc`**: ASCII only. They're in the prelude now.
- **`pad-start pad-end str-repeat format-num`**: no change.
- **`string->int`** (new): gives the integer, or `#false`.
- **`symbol->string`, `string->symbol`** (new): `string->symbol` gives
  `#false` unless the program mentions that symbol somewhere, because
  symbols are made at compile time.
- **`byte-at`, `bytes->string`** (new): a byte as an integer, and a list
  of bytes back to a string.
- **`\n \r \t \e`**: no change.
- **Gone**: `last-index-of`, `str-split-at` and `str-splice-at`.

### Output

- **`pprint`**: no change, except for floats, which always show a `.` or
  an exponent: `3.0`, `1e+21`, `nan`, `inf`.
- **`tty/write`**: no change. It flushes every time.
- **`tty/screen/rows`, `tty/screen/cols`**: no change, except that they
  give 24 and 80 when stdout isn't a terminal.

### Functions

- **`apply`**: `(apply f xs)`, a builtin now. `xs` can have at most 8
  elements.
- **Functions as values**: a `defun` name, a lambda, or a builtin with a
  fixed number of arguments can be a value, as in `(map car xs)` or
  `(fold/l 0 + xs)`. Builtins with a varying number (`list`, `concat`,
  `tty/write`, `format-num`) can't, so `(apply list xs)` is a compile
  error.

## The prelude

The prelude is always there. These changed:

- **`filter` and `remove`**: `(filter f xs)` now *keeps* what `f` says
  `#true` to, as ts-slight's `grep` did. `(remove f xs)` drops it, as
  ts-slight's `filter` did. This is the one most likely to trip you up.
- **`range`**: `(range start end)`. There's no step, and the end is left
  out, so `(range 0 3)` is `(0 1 2)`. ts-slight's `(range 0 3 1)` was
  `(0 1 2 3)`.
- **`take`, `skip`**: stop at the end of the list instead of faulting.
- **`nth`, `find`**: give `()` when there's nothing.
- **`not`**: booleans only.
- **`fold/l`, `fold/r`**: they take the same arguments as before
  (`(f acc x)` and `(f x acc)`), but they're written in slight now.

These are unchanged: `inc dec reverse length append sum product map
member? dotimes assoc lookup uc lc pad-start pad-end str-repeat`.

These have left the prelude:

- `apply` and `sleep` are builtins.
- `slurp` and `spew` are in `lib/fs.slight`.
- `concat-list` is now `str-join`.
- `grep` is now `filter`.

## The libraries

**`(@include :test)`**: `run-tests`, `ok`, `is` and `diag`, a TAP library
as ts-slight's `Test.slight` was:

    (run-tests (list
        (ok (< 1 2) "one is less than two")
        (is (+ 1 1) 2 "addition")
        (diag "a comment")))

**`(@include :fs)`**: two functions. Both block only the caller.

- `(slurp path)` gives `(:ok lines)`, a list of lines without their
  newlines (not one string), or `(:error (name path))`.
- `(spew path lines)` writes each line followed by a newline, replacing
  what was in the file, and gives `(:ok ())`.

## Gone

- **Reflection and evaluation**: `gensym`, `slight/parse`, `slight/eval`
  and `slight/eval-in-top-level`. Compilation is ahead of time only.
- **System calls**: `syscall`, `localtime`, `time-it` and `time-it/end`.
- **Messages**: `msg`, and `recv` as an expression.
- **Predicates**: `true?`, `false?`, `builtin?`, `list?`, `atom?`,
  `literal?`, `callable?` and `type-of`.
- **Logic**: `and?` and `or?`.
- **Lists**: `head` and `tail`.
- **Strings**: `last-index-of`, `str-split-at`, `str-splice-at` and
  `ast->str`.
- **The deprecated set**: `sys/io/print-ln`, `poke`,
  `ansi/hide-cursor` and `ansi/show-cursor`.
- **Language features**: local `defun` and hot reload.

## Not here yet

HTTP (step 10f, in slight, on `:tcp`), and `defactor`, which we still
need to talk through.
