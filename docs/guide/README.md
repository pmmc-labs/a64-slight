# The slight guide

This guide is for learning slight. It's written for people who already
program, and assumes no Lisp and no Erlang. For the reference, where
every form and builtin is, see [`docs/LANGUAGE.md`](../LANGUAGE.md).

Start with the **[tour](tour.md)**: fifteen minutes from hello world to
actors. Then the **[tutorial](tutorial/01-values.md)** goes over the same
ground slowly, building one program.

## What's here

The guide has four parts, each for a different need:

- **The tour** ([`tour.md`](tour.md)). A quick look at the whole
  language.
- **The tutorial** (`tutorial/`). One program, a key-value store, grown
  chapter by chapter:
  1. [Values, functions, and loops](tutorial/01-values.md)
  2. [Lists, `match`, and data as tagged lists](tutorial/02-match.md)
  3. [State in a process](tutorial/03-actors.md): an actor for the store
  4. [Asking and answering](tutorial/04-asking.md), and the `recv` rule
     in practice
  5. [When things go wrong](tutorial/05-errors.md): faults, errors,
     `raise`, a supervisor, and tasks and actors
  6. [Time, and testing](tutorial/06-time.md): keys that expire,
     the virtual clock, and `(@include :test)`
  7. [Keeping the store in a file](tutorial/07-files.md): devices, and
     a log the store starts again from
  8. [Serving the store](tutorial/08-serving.md) over a socket with
     `(@include :rpc)`, to programs and people
- **[Concepts](concepts/README.md)**. The ideas you have to adopt to
  write slight well, and why they're there:
  - [Nothing changes](concepts/immutability.md): where state goes
    instead
  - [The `recv` rule](concepts/recv-rule.md)
  - [Tasks and actors](concepts/tasks-and-actors.md)
  - [Failure](concepts/failure.md): faults, errors, and how processes
    end
  - [Time](concepts/time.md): the real clock, the virtual one, and
    replaying a run
  - [Memory](concepts/memory.md): a heap per process, and what a
    message costs
- **[How-to](howto/README.md)**. Short recipes:
  [reading a file](howto/read-a-file.md),
  [JSON](howto/json.md),
  [arguments and the environment](howto/arguments.md),
  [running a command](howto/run-a-command.md),
  [an HTTP server and client](howto/http.md),
  [shipping a program in a container](howto/containers.md),
  [tests](howto/testing.md),
  [finding a deadlock](howto/deadlocks.md) with `:system`, and
  [finding a race](howto/races.md) with `SLIGHT_SCHED` and a replay.

## The code is tested

Every program in the guide is in [`code/`](code), with the output it
should print beside it, and runs with the golden tests; a file of
functions that programs share, such as the tutorial's
`tut-store.slight`, is run by the programs that include it. A `lisp` block
in a page is preceded by `<!-- code: path -->` naming its program, and
an output block by `<!-- output: path -->`; `t/guide.py` (`make guide`)
checks that each block is in its file as it stands. So when the
language changes, a page that's out of date fails `make test` rather
than misleading someone. A sketch that isn't meant to run is fenced
without the `lisp`.
