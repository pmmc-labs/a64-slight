# Concepts

These pages explain the ideas slight is built on: what each one means
for a program, why it's there, and what it costs. They assume you've
read the [tour](../tour.md); the [tutorial](../tutorial/01-values.md)
puts each to work.

1. [Nothing changes](immutability.md): where state goes when nothing
   can be mutated, and why the language is built that way.
2. [The recv rule](recv-rule.md): where a process can wait, what that
   buys, and how to write actors with it.
3. [Tasks and actors](tasks-and-actors.md): the two kinds of process,
   what each is for, and how to choose.
4. [Failure](failure.md): error values, faults, `raise` and `kill`, and
   how the processes that care find out.
5. [Time](time.md): the real clock and the virtual one, what a run
   depends on, and recording and replaying it.
6. [Memory](memory.md): a heap per process, what a message costs, and
   what happens when a process grows too big.

Each page links to the decisions behind it, as D-numbers, in
[`docs/DECISIONS.md`](../../DECISIONS.md), for anyone who wants the
history and what was turned down.
