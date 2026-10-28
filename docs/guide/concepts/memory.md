# Memory

Every slight process has a heap of its own, collected on its own.
Nothing is shared between processes, and a message is a copy. This page
says what that means for a program: what's cheap, what costs, and what
happens when a process grows too big.

## A heap per process

<!-- code: ../code/con-memory.slight -->
```lisp
; A process that grows without end faults on its own, at its heap's cap,
; and the rest go on.
(defun grow (xs)
    (grow (cons 0 xs)))

(pprint (join (fork (grow ()))))

; A process waiting for a message holds no stack.
(defun idle ()
    (recv (_ :done)))

(let a (spawn (idle)))
(sleep 1)
(match (open :system)
    ((:ok s)
        (send s (list :process a))
        (match (read s)
            ((:process _ info) (list (lookup :state info) (lookup :stack info))))))
```

<!-- output: ../code/con-memory.expected -->
```
fault in #<pid 2>: heap exhausted (cons at docs/guide/code/con-memory.slight:4:11)
(error (heap () "cons at docs/guide/code/con-memory.slight:4:11"))
(recv #false)
```

- **Each heap has a cap**: 1 GB, or what `SLIGHT_HEAP_MAX` says (the
  guide's tests use 64 MB, which is why this runs quickly). In a
  container with a memory limit, it's a quarter of the limit. A process
  that reaches its cap faults with `:heap`, and only it ends, as
  `grow` did here. A runaway process doesn't take the program down, and
  the kernel doesn't have to kill anything.
- **Collection is per process.** A process's heap is collected when it
  waits for a message, or when it enters a function after its heap has
  grown enough. The collector has two generations: what's new goes in a
  small nursery, what survives moves to the old generation, and the old
  generation is collected rarely. A collection looks only at the
  process being collected, and takes time in proportion to what that
  process still holds, so a big process doesn't make the small ones'
  collections slower.
- **A waiting process holds no stack.** The `:system` device shows the
  idle actor waiting in `recv`, with no stack. Stacks are pooled, and
  held only while a process is running, paused, or waiting in `join`,
  `read` or `sleep` (see [the recv rule](recv-rule.md)). An idle actor is its
  function, its arguments, its mailbox and its heap.
- **A stack has a limit too**, 8 MB. Recursion that isn't a tail call
  uses it, and too much faults with `:stack`. Loops are tail calls, and
  take none.

## What a message costs

`send` copies the message into the receiver, in one walk over the
value. The copy is as big as the value, and so is the time it takes.

- **Small messages are cheap.** A message such as `(list :get key to)`
  is a few words, and a send is one allocation.
- **Big messages cost their size.** Sending a 100,000-element list
  copies 100,000 cells. If a process needs a big structure often, it's
  usually better for the process that owns it to answer questions about
  it than to send it whole.
- **`fork` and `spawn` copy too.** The locals their expression uses
  are copied into the new process whole. To fork a worker on part of a
  big list, take the part first, with `(let mine (take n big))`, then
  `(fork (work mine))`, so only the part is copied.
- **A device's data is the receiver's.** A line from a file arrives
  in the owner's heap. A line, count or value of 64 MB or more ends the
  owner with `:too-big`, so an endless line can't take all the memory.

## Seeing it

The `:system` device tells each process's `(:heap bytes)`,
`(:reductions n)`, `(:mail n)` (messages waiting) and `(:stack held)`,
and the runtime's totals. `examples/top.slight` shows them live, like
`top`. A process whose mail keeps growing is being sent more than it
can take. A process whose heap keeps growing is keeping more than it
needs, often in a `let` at the top of a long-running function, which
stays live until the function returns.

---

Previous: [Time](time.md) · [Concepts](README.md)
