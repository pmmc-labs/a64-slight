
<!---------------------------------------------------------------------------->
# Calling Conventions
<!---------------------------------------------------------------------------->

> **Historical.** Copied from stevan/VM3 `docs/CALLING_CONVENTIONS.md` (branch
> `claude/gifted-cray-sfohis`, commit 22ec581). It is the design the AArch64
> spike in `aarch64/` implements. a64-slight does **not** use REQUEST/AWAIT
> or tagged modules: see `docs/DESIGN.md` and `docs/DECISIONS.md` for what
> replaced them and why. Kept for the register convention, the context
> switch, and the reasoning.
>
> **Revised draft.** This replaces the August 2024 version (see git history).
> The differences, and the reason for each, are listed under *Changes* at
> the end.

Everything happens within the context of a process, meaning that there are
no globals. A process owns a program counter, an operand stack, a frame
stack, a mailbox and a small amount of process state.

There are two calling conventions, and they are built on the same thing
(frames):

- **sync**  ... `CALL` a label inside this process, get one value back
- **async** ... `REQUEST` a tag of another process, `AWAIT` the reply

Underneath those sit spawning, receiving messages, and signals.

<!---------------------------------------------------------------------------->
## Process Anatomy
<!---------------------------------------------------------------------------->

- registers
    - `pc`     ... program counter
    - `entry`  ... where the process continues after a `YIELD`
    - `cursor` ... position in the mailbox of the message `RECV` last looked at
- operand stack
    - values only, frames are kept on a separate frame stack
      (as `VM::Kernel::Process` already does with `@frames`)
- frame stack
    - the bottom frame is always the PROCESS frame, and it lives as long
      as the process does
- mailbox
    - an ordered list of envelopes, appended to by the runtime
- state
    - the locals of the PROCESS frame, declared in the module header
      with `@state`, and reachable from any frame depth with
      `LOAD_STATE` / `STORE_STATE`

### Frames

```
Frame {
    kind   : PROCESS | CALL | MESSAGE | SIGNAL
    argc   : number of arguments
    base   : height of the operand stack when the frame was pushed
    return : pc to go back to (CALL and SIGNAL only)
    locals : slots for LOAD_LOCAL / STORE_LOCAL
    env    : the envelope of the message (MESSAGE only)
}
```

A frame's arguments sit on the operand stack directly below `base`, in
the order they were pushed, so for every kind of frame:

```
LOAD_ARG i  =>  stack[ base - argc + i ]
```

Argument 0 is the *first* one pushed (left to right, like the JVM) so a
compiler can evaluate arguments in source order. Everything above `base`
is the frame's scratch space.

In the stack comments below, `|` marks the current frame's `base`. The
arguments are on its left and scratch values are on its right.

```
x y | m x y-1                  # args (x, y), then three scratch values
```

<!---------------------------------------------------------------------------->
## Synchronous (Function) Calls
<!---------------------------------------------------------------------------->

- CALL &label, argc            `( a0 .. an-1 -- )`, then `( -- v )` once it returns
    - push a CALL frame `{ argc, base: sp, return: pc }`
    - pc = &label

- RETURN                       `( v -- )`
    - v = POP
    - truncate the stack to `base - argc` (drops the args and any scratch)
    - pc = frame.return
    - pop the frame
    - PUSH v
    - fault if the current frame is not a CALL frame

- LOAD_ARG i                   `( -- v )`
    - argument `i` of the *current* frame, whatever kind it is

- LOAD_LOCAL $x / STORE_LOCAL $x
    - locals live in the frame and disappear with it
    - there is no ALLOC_LOCAL / FREE_LOCAL, the assembler numbers the
      `$names` used inside each function

Functions can be called from anywhere, including message and signal
handlers, and a function can itself `REQUEST` and `AWAIT` (see below).

> NOTE: `VM::Kernel::Process` already implements this, with
> `StackFrame->sp` as `base`. The one fix needed is `LOAD_ARG`, which
> currently indexes the stack with the frame *depth* (`fp`), not `base`.

<!---------------------------------------------------------------------------->
## Processes
<!---------------------------------------------------------------------------->

A module provides an `.init` label (required) and a `.signal` label
(only if it handles signals, see *Signals*).

- SPAWN /name/space, argc      `( a0 .. an-1 -- pid )`
    - create a new process P running `/name/space`, with `parent = self`
    - move the args onto P's stack, and push P's PROCESS frame `{ argc }`
    - P.pc = the `.init` of `/name/space`, and P is READY
    - PUSH P's pid
    - P starts running on the next tick, *in its own context*

`.init` reads the spawn arguments with `LOAD_ARG`, sets up `@state` with
`STORE_STATE`, and then usually finishes with `YIELD &loop`. Messages
sent to P before that point simply wait in its mailbox.

- YIELD &label                 `( -- )`
    - entry = &label (the operand is optional, without it `entry` is unchanged)
    - this is the commit point, it faults unless:
        - the PROCESS frame is the only frame
        - nothing is left above its `base`
    - cursor = head of the mailbox
    - pc = entry

`YIELD` does not suspend anything by itself. It ends the current
activation and goes back to `entry`, which should be a receive loop, and
it is the `RECV` there that suspends the process when the mailbox is empty.

- STOP                         `( -- )`
    - the process is finished, from any frame depth
    - at the end of the tick the runtime reaps it:
        - anything left in its mailbox goes to the dead-letter queue
        - `%EXIT` is sent to its parent and to all its watchers

- SELF                         `( -- pid )`

- WATCH                        `( pid -- )`
    - also send `%EXIT` to this process when `pid` stops
      (the parent always gets it)

- WAIT                         `( pid -- )`
    - continue if `pid` has stopped, otherwise become BLOCKED until it does

- WAIT_ALL                     `( -- )`
    - same as WAIT, but for all of this process's children

### Process States

```
READY    ... will run this tick
WAITING  ... suspended in RECV or AWAIT
             - woken by new mail or a signal
             - an AWAIT is also woken when the process it awaits stops
BLOCKED  ... suspended in WAIT / WAIT_ALL
             - woken by a signal, or by the exit it is waiting for
             - NOT woken by mail, which just queues up
STOPPED  ... finished, reaped at the end of the tick
```

### Suspension is restartable

`RECV`, `AWAIT`, `WAIT` and `WAIT_ALL` are the only instructions that
suspend. When one of them suspends it leaves the stack as it found it and
`pc` still points at it, so when the process wakes up the instruction just
runs again.

Nothing is unwound by a suspension. The operand stack, every frame and
every local survives it, which is what lets a handler (or a function
called from a handler) stop in the middle, wait for a reply, and carry on.
It also means there is no "function colouring": any function can `AWAIT`,
because every process has its own stack.

<!---------------------------------------------------------------------------->
## Messages
<!---------------------------------------------------------------------------->

```
Envelope {
    to    : pid
    from  : pid
    tag   : ^tag         # owned by the module of `to`
    args  : [ v0 .. vn-1 ]
    reply : REPLY_ADDR   # or null, set by REQUEST
    ref   : int          # or null, set on replies (copied from the REPLY_ADDR)
}
```

### Tags

Tags are declared in the module header, with their arity:

```
@tags = (
    ^mul/2
    ^product/1
)
```

A TAG value knows its module, index, name and arity. `SEND` and
`REQUEST` fault unless:

- the target's module owns the tag (a pid knows its module), and
- `argc` equals the tag's arity

So a process only ever receives its *own* tags, with the right number of
arguments, which is what makes dispatching with `SWITCH` safe.

### Sending

- SEND ^tag, argc              `( pid a0 .. an-1 -- )`
    - the target is *below* the args, like the receiver of a method call
    - envelope `{ to: pid, from: self, tag, args, reply: null }`
    - goes into the outbox and is delivered at the start of the next tick

### Receiving

- RECV                         `( -- ^tag )`
    - look at the message under the cursor, but do not remove it
    - if there is one: PUSH its tag
    - if there is not: suspend (WAITING)

- SWITCH &default, ( ^a => &a, ^b => &b, ... )      `( key -- )`
    - pop a TAG, SIGNAL or INT and jump to its label, or to `&default`
    - the assembler builds a dense jump table indexed by the key, so the
      order of `@tags` no longer matters

- MSG_ACCEPT                   `( -- a0 .. an-1 )`
    - remove the message under the cursor from the mailbox
    - cursor = head of the mailbox
    - PUSH its args, then push a MESSAGE frame `{ argc, base: sp, env }`

- MSG_SKIP                     `( -- )`
    - leave the message in the mailbox and move the cursor to the next one
    - follow it with a `JUMP` back to the `RECV`

- MSG_DROP                     `( -- )`
    - remove the message under the cursor and put it in the dead-letter queue
    - the cursor now points at the message after it

- MSG_DONE                     `( a0 .. an-1 | s0 .. sk -- s0 .. sk )`
    - remove the frame's args and pop the MESSAGE frame
    - anything pushed above `base` is *kept*, so a value computed from a
      message can outlive it
    - fault if the current frame is not a MESSAGE frame

- MSG_SENDER                   `( -- pid )`
- MSG_REPLY_ADDR               `( -- reply_addr )` (or null)
    - both read the *nearest* MESSAGE frame, so a helper function called
      from a handler can use them

The args of a message are the args of its frame, so they are read with
`LOAD_ARG`.

`MSG_ACCEPT`, `MSG_SKIP` and `MSG_DROP` all act on the message `RECV`
last looked at, and fault if there is not one.

Use `MSG_DROP` for tags the top-level loop does not handle (a late reply,
for example). A skipped message stays put until some later receive takes
it, and since the cursor resets on every `YIELD`, the top-level loop would
just keep skipping it. `MSG_SKIP` is for receives *inside* a handler.

### Anatomy of a Process

```
:module /greeter

:header
    @tags = (
        ^hello/1                    # (name)
    )
:end

.init                               # PROCESS frame, no spawn args
    YIELD &loop

.loop
    RECV                            # | ^tag
    SWITCH &loop.drop, (
        ^hello => &hello
    )                               # |
    .loop.drop
        MSG_DROP
        JUMP &loop

.hello
    MSG_ACCEPT                      # name |
    PUSH s("Hello ")                # name | "Hello "
    LOAD_ARG 0                      # name | "Hello " name
    SYS_CALL ::say, 2               # name |
    MSG_DONE                        # |
    STOP

:end

.main                               # the implicit /main, needs no header to SEND or WAIT
    SPAWN /greeter, 0               # | g
    DUP                             # | g g
    PUSH s("World")                 # | g g "World"
    SEND ^/greeter:hello, 1         # | g
    WAIT                            # |         BLOCKED until g has stopped
    STOP
```

<!---------------------------------------------------------------------------->
## Asynchronous Calls
<!---------------------------------------------------------------------------->

An async call is a `CALL` whose callee is another process. The args go in
a message, and instead of pushing a return `pc` the caller sends a
*reply address*: its own pid, one of its own tags, and a fresh `ref`.

- REQUEST ^tag, ^reply, argc   `( pid a0 .. an-1 -- pending )`
    - same as SEND, but the envelope gets `reply: REPLY_ADDR { self, ^reply, ref }`
    - `^reply` must be one of the caller's own tags
    - `ref` is new for every REQUEST (a per-process counter)
    - PUSH a `PENDING { pid, ^reply, ref }` for AWAIT to use

- AWAIT ^reply, &failed        `( pending -- r0 .. rn-1 )`
    - wait for the reply that matches `pending`, see below

- REPLY argc                   `( r0 .. rn-1 -- )`
    - send `( r0 .. rn-1 )` to the reply address of the nearest MESSAGE frame
    - envelope `{ to: addr.pid, from: self, tag: addr.tag, args, ref: addr.ref }`
    - fault if there is no reply address, or if `argc` is not the arity of `addr.tag`

- REPLY_TO argc                `( reply_addr r0 .. rn-1 -- )`
    - same, but with a reply address taken earlier with `MSG_REPLY_ADDR`
    - this is what makes delegation and the run-to-completion style (below) possible

### AWAIT

This is the heart of the convention, so here it is step by step:

```
AWAIT ^reply, &failed

    h = POP                                    # PENDING { pid, tag, ref }
    fault unless h.tag == ^reply

    scan the mailbox from the head for the first message
    with tag == ^reply AND ref == h.ref

    if found:
        remove it from the mailbox
        cursor = head of the mailbox
        PUSH its args                          # arity(^reply) values, arg 0 first
        continue with the next instruction
    else if h.pid has stopped:
        pc = &failed                           # nothing pushed, h is gone
    else:
        PUSH h                                 # restore the stack
        suspend (WAITING)                      # and run this AWAIT again on wake
```

- every other message stays in the mailbox, untouched and in order, so a
  stray message can never be mistaken for the reply
- the mailbox is always scanned *before* checking whether `h.pid` has
  stopped. Together with the tick ordering (see *Delivery*), this means a
  reply sent just before the callee stops is never lost
- the scan matches on `ref`, not on the sender, so a reply still matches
  if the callee hands the request to a worker that replies with `REPLY_TO`

### Side by Side

```
    # sync                                   # async
    PUSH i(4)               # | 4            LOAD_LOCAL $m           # | m
    PUSH i(10)              # | 4 10         PUSH i(4)               # | m 4
    CALL &mul, 2            # | 40           PUSH i(10)              # | m 4 10
                                             REQUEST ^/multiplier:mul, ^result, 2
                                                                     # | h
                                             AWAIT ^result, &failed  # | 40
```

| | sync | async |
|---|---|---|
| callee named by | `&label` | `pid` + `^tag` |
| args | pushed, read with `LOAD_ARG` | pushed, copied into the message, read with `LOAD_ARG` in the MESSAGE frame |
| return address | `pc` in the CALL frame | `REPLY_ADDR` in the envelope |
| callee enters with | CALL frame | `MSG_ACCEPT` pushes a MESSAGE frame |
| callee returns with | `RETURN` (1 value) | `REPLY argc` (arity of the reply tag), then `MSG_DONE` |
| caller gets back | the value, pushed | the reply's args, pushed by `AWAIT` |
| failure | none (same process) | `AWAIT` jumps to `&failed` if the callee stops without replying |

### Worked Example: the recursive multiplier

This is `t/003-maths.t` from AVM, rewritten. That version used a plain
`RECV` to wait for its child, and a single stray message turned `4 × 10`
into `1003`. Here `main` sends a *forged* `^product` on purpose, and
the answer is still 40.

```
:module /multiplier

:header
    @tags = (
        ^mul/2                              # (x, y) -> replies x * y
        ^product/1                          # reply from a child /multiplier
        ^sum/1                              # reply from an /adder
    )
:end

.init
    YIELD &loop

.loop
    RECV                                    # | ^tag
    SWITCH &loop.drop, (
        ^mul => &mul
    )                                       # |
    .loop.drop
        MSG_DROP                            # stray ^product / ^sum
        JUMP &loop

.mul
    MSG_ACCEPT                              # x y |
    LOAD_ARG 1                              # x y | y
    JUMP_IF_ZERO &mul.zero                  # x y |
    LOAD_ARG 1                              # x y | y
    PUSH i(1)                               # x y | y 1
    EQ_INT                                  # x y | y==1
    JUMP_IF_TRUE &mul.one                   # x y |

    # x * y = x + (x * (y - 1))

    SPAWN /multiplier, 0                    # x y | m
    LOAD_ARG 0                              # x y | m x
    LOAD_ARG 1                              # x y | m x y
    DEC_INT                                 # x y | m x y-1
    REQUEST ^mul, ^product, 2               # x y | h
    AWAIT ^product, &mul.failed             # x y | p

    SPAWN /adder, 0                         # x y | p a
    SWAP                                    # x y | a p
    LOAD_ARG 0                              # x y | a p x
    REQUEST ^/adder:add, ^sum, 2            # x y | h
    AWAIT ^sum, &mul.failed                 # x y | s
    JUMP &mul.reply

    .mul.zero
        PUSH i(0)                           # x y | 0
        JUMP &mul.reply
    .mul.one
        LOAD_ARG 0                          # x y | x
        JUMP &mul.reply
    .mul.failed
        STOP                                # a helper died without replying, so we die
                                            # too, and our caller's AWAIT fails in turn
    .mul.reply
        REPLY 1                             # x y |    to whatever reply tag the caller asked for
        MSG_DONE                            # |
        STOP                                # one multiplication per process

:end
```

```
:module /adder

:header
    @tags = (
        ^add/2                              # (x, y) -> replies x + y
    )
:end

.init
    YIELD &loop

.loop
    RECV                                    # | ^tag
    SWITCH &loop.drop, (
        ^add => &add
    )                                       # |
    .loop.drop
        MSG_DROP
        JUMP &loop

.add
    MSG_ACCEPT                              # x y |
    LOAD_ARG 0                              # x y | x
    LOAD_ARG 1                              # x y | x y
    ADD_INT                                 # x y | x+y
    REPLY 1                                 # x y |
    MSG_DONE                                # |
    STOP

:end
```

```
:module /main

:header
    @tags = (
        ^result/1
    )
:end

.main                                       # the .init of /main
    SPAWN /multiplier, 0                    # | m
    DUP                                     # | m m
    PUSH i(4)                               # | m m 4
    PUSH i(10)                              # | m m 4 10
    REQUEST ^/multiplier:mul, ^result, 2    # | m h

    SWAP                                    # | h m
    PUSH i(999)                             # | h m 999
    SEND ^/multiplier:product, 1            # | h        forged: the right tag, but no ref

    AWAIT ^result, &failed                  # | 40
    SYS_CALL ::say, 1                       # |
    STOP

    .failed
        PUSH s("the multiplier died")
        SYS_CALL ::say, 1
        STOP

:end
```

The forged `^product` arrives in `m`'s mailbox right after the `^mul`.
`m` accepts the `^mul`, and its `AWAIT ^product` skips the forgery
because its `ref` is null. When `m` stops, the forgery goes to the
dead-letter queue. If it had arrived while `m` was in its top-level loop
instead, `SWITCH` would have sent it to `MSG_DROP`.

### Run-to-Completion Style

Replies are ordinary messages, so a process does not have to `AWAIT`
them. It can let them come back through its loop instead, keeping what it
needs in `@state`. This is the style that the `promise` / `reply-to` in
the target language sketch (TODO.md) would compile to:

```
.loop
    RECV
    SWITCH &loop.drop, (
        ^mul     => &mul,
        ^product => &product,
        ^sum     => &sum,
    )

.mul
    MSG_ACCEPT                              # x y |
    MSG_REPLY_ADDR                          # x y | r
    STORE_STATE $caller                     # x y |
    # ... store x, SPAWN, push args ...
    REQUEST ^mul, ^product, 2               # x y | h
    POP                                     # x y |    not awaiting, the reply comes via .loop
    MSG_DONE                                # |
    YIELD &loop

.product
    # ... MSG_ACCEPT, REQUEST ^/adder:add, ^sum, 2, POP, MSG_DONE, YIELD &loop ...

.sum
    MSG_ACCEPT                              # s |
    LOAD_STATE $caller                      # s | r
    LOAD_ARG 0                              # s | r s
    REPLY_TO 1                              # s |
    MSG_DONE                                # |
    STOP
```

The VM does not have to choose between the Erlang style (block in the
middle of a handler) and the Pony / E style (handlers run to completion).
Both use the same instructions.

<!---------------------------------------------------------------------------->
## Asynchronous Signals
<!---------------------------------------------------------------------------->

Signals are a small, fixed set. Each one carries two values: the pid it
is about, and a reason (null, or a STRING if a process faulted).

```
%EXIT  (pid, reason)   ... a child, or a watched process, has stopped
%STOP  (pid, null)     ... `pid` is asking this process to stop
```

- a module lists the signals it handles in `@signals` and provides `.signal`
- any signal not listed gets its default action:
    - `%EXIT` is ignored
    - `%STOP` stops the process
- signals are always handled before messages

### Delivery

Before a process runs in a tick (and after every `SIG_RESUME`), if it has
a pending signal:

- push a SIGNAL frame `{ argc: 2, return: pc }` over the args `( pid, reason )`
- PUSH the signal itself (`%EXIT`, ...) so it can be dispatched with `SWITCH`
- pc = `.signal`

A signal can interrupt any frame depth, so a process suspended in `AWAIT`
or `WAIT` still sees `%STOP` and `%EXIT`. Signals are handled one at a
time and do not nest.

Inside a signal handler, anything goes except `RECV`, `AWAIT` and `YIELD`,
which fault. `WAIT`, `SEND`, `NOTIFY` and `STOP` are all fine. A typical
`%STOP` handler does `NOTIFY %STOP` to its children, then `WAIT_ALL`,
then `STOP`.

- SIG_RESUME                   `( ... -- )`
    - drop the SIGNAL frame (its args and any scratch)
    - pc = frame.return
    - if the process was suspended, the instruction it was suspended in
      simply runs again (it is restartable), and suspends again if nothing changed

- NOTIFY %STOP                 `( pid -- )`
    - queue `%STOP (self, null)` for `pid`, delivered next tick
    - only `%STOP` can be sent, `%EXIT` only ever comes from the runtime

```
.signal                                     # pid reason | %SIG
    SWITCH &signal.done, (
        %EXIT => &signal.exit
    )                                       # pid reason |
    .signal.exit
        LOAD_ARG 0                          # pid reason | pid
        # ... forget about this child ...
        POP
    .signal.done
        SIG_RESUME
```

<!---------------------------------------------------------------------------->
## Delivery and Ordering
<!---------------------------------------------------------------------------->

Time is measured in ticks, not wall-clock time, so a run is deterministic.
Each tick runs these steps:

```
1. deliver   the bus -> mailboxes, in send order
             WAITING processes that got new mail -> READY
2. signals   queued signals -> processes
             WAITING / BLOCKED processes with a signal -> READY
             AWAITs whose PENDING pid has stopped -> READY (the AWAIT will now fail)
             WAITs whose pid has stopped -> READY
3. run       each READY process, in pid order, for up to QUOTA instructions
             (a process that hits its quota stays READY and continues next tick)
4. collect   outboxes -> the bus, so nothing sent in tick N is seen before tick N+1
5. reap      STOPPED processes:
                 mailbox -> dead-letter queue
                 queue %EXIT (pid, reason) for the parent and watchers
```

This gives the following guarantees:

- **per-pair FIFO:** messages from A to B arrive in the order A sent them
- **replies beat exits:** a process's last messages hit the bus in step 4
  and are delivered in step 1 of the next tick, before its `%EXIT`
  (step 2), and `AWAIT` always scans the mailbox before checking whether
  the callee has stopped
- **determinism:** the same program always produces the same interleaving,
  so tests can assert exact output (as AVM's tests already do)
- **deadlock detection:** if there are live processes but none are READY,
  and the bus and signal queues are empty, the VM stops and reports a
  deadlock, rather than spinning

<!---------------------------------------------------------------------------->
## Faults
<!---------------------------------------------------------------------------->

A fault stops the process as if it had executed `STOP`, with the fault as
the reason in its `%EXIT`. Faults are:

- stack underflow
- `RETURN`, `MSG_DONE` or `SIG_RESUME` when the current frame is the wrong kind
- `YIELD` with frames other than the PROCESS frame, or scratch values left over
- `YIELD` without an operand when `entry` was never set
- `SEND` / `REQUEST` with a tag the target does not own, or the wrong `argc`
- `REPLY` with no reply address, or with the wrong `argc`
- `MSG_ACCEPT` / `MSG_SKIP` / `MSG_DROP` with no message under the cursor
- `AWAIT` whose tag does not match its PENDING
- `RECV`, `AWAIT` or `YIELD` inside a signal handler

<!---------------------------------------------------------------------------->
## System Calls
<!---------------------------------------------------------------------------->

- SYS_CALL ::name, argc

(Unchanged, and out of scope for this document.)

<!---------------------------------------------------------------------------->
## Instruction Summary
<!---------------------------------------------------------------------------->

| Op | Operands | Stack | Notes |
|---|---|---|---|
| `CALL` | `&label, argc` | `( a0..an-1 -- )`, then `( -- v )` | pushes a CALL frame |
| `RETURN` | | `( v -- )` | pops the CALL frame |
| `LOAD_ARG` | `i` | `( -- v )` | current frame |
| `LOAD_LOCAL` / `STORE_LOCAL` | `$x` | `( -- v )` / `( v -- )` | current frame |
| `LOAD_STATE` / `STORE_STATE` | `$x` | `( -- v )` / `( v -- )` | PROCESS frame |
| `SPAWN` | `/mod, argc` | `( a0..an-1 -- pid )` | child runs `.init` next tick |
| `SELF` | | `( -- pid )` | |
| `YIELD` | `[&label]` | `( -- )` | commit point, pc = entry |
| `STOP` | | `( -- )` | reaped at the end of the tick |
| `WATCH` | | `( pid -- )` | get `%EXIT` for pid |
| `WAIT` | | `( pid -- )` | BLOCKED, restartable |
| `WAIT_ALL` | | `( -- )` | BLOCKED, restartable |
| `SEND` | `^tag, argc` | `( pid a0..an-1 -- )` | |
| `REQUEST` | `^tag, ^reply, argc` | `( pid a0..an-1 -- pending )` | |
| `AWAIT` | `^reply, &failed` | `( pending -- r0..rn-1 )` | WAITING, restartable |
| `REPLY` | `argc` | `( r0..rn-1 -- )` | nearest MESSAGE frame |
| `REPLY_TO` | `argc` | `( addr r0..rn-1 -- )` | |
| `RECV` | | `( -- ^tag )` | WAITING, restartable |
| `SWITCH` | `&default, ( k => &l, .. )` | `( k -- )` | |
| `MSG_ACCEPT` | | `( -- a0..an-1 )` | pushes a MESSAGE frame |
| `MSG_SKIP` | | `( -- )` | cursor + 1 |
| `MSG_DROP` | | `( -- )` | to the dead-letter queue |
| `MSG_DONE` | | `( a0..an-1 \| s.. -- s.. )` | pops the MESSAGE frame |
| `MSG_SENDER` | | `( -- pid )` | nearest MESSAGE frame |
| `MSG_REPLY_ADDR` | | `( -- addr )` | nearest MESSAGE frame, or null |
| `NOTIFY` | `%STOP` | `( pid -- )` | |
| `SIG_RESUME` | | `( .. -- )` | pops the SIGNAL frame |

<!---------------------------------------------------------------------------->
## Changes
<!---------------------------------------------------------------------------->

| August 2024 | Now | Why |
|---|---|---|
| `.spawn` / `.despawn`, `ALLOC_PROCESS`, `FREE_PROCESS`, `SET_ENTRY`, `SET_SIG_HANDLER` | `SPAWN` runs `.init` inside the child, the runtime handles teardown, `.signal` is a well-known label | `.spawn` ran in the *parent* but allocated the child's locals |
| `%STARTING` | `.init` | init already runs in the child |
| `%STOPPING`, `DESPAWN` | `%STOP`, sent with `NOTIFY %STOP` | |
| `%TERMINATED` (no body) | `%EXIT (pid, reason)` | `WAIT` and supervisors need to know *who* stopped, and why |
| `%RESTARTING`, `RESTART` | deferred | needs a supervision design (see *Open Questions*) |
| `SIG_IGNORE` and `SIG_RESUME` | `SIG_RESUME` only | same effect, and unlisted signals get their default action |
| `MSG_REJECT` (to the dead-letter queue) | `MSG_SKIP` (stays in the mailbox) and `MSG_DROP` (to the dead-letter queue) | rejecting while waiting for a reply destroys legitimate requests, and a plain RECV takes them as the reply (AVM's `4 × 10 = 1003`) |
| `MSG_DISCARD`, the MP register, the MP == SP rule | `MSG_DONE` pops the MESSAGE frame | one frame mechanism, and the commit check moved to `YIELD` |
| `MSG_GET idx` | `LOAD_ARG idx` | message args are frame args |
| (none) | `REQUEST`, `AWAIT`, `REPLY`, `REPLY_TO`, `MSG_REPLY_ADDR` | the async calling convention itself |
| `ADVANCE_BY` jump tables, `NUM_SIGNALS`, `NUM_MESSAGES` | `SWITCH`, keyed by tag | the `× 2` assumed two-cell JUMPs, the bounds checks in ASSEMBLY.md were inverted, and the order of `@tags` mattered |
| `ALLOC_LOCAL` / `FREE_LOCAL`, and locals allocated in `.spawn` as process state | frame locals, plus `@state` with `LOAD_STATE` / `STORE_STATE` | |
| `LOAD_ARG i` = `stack[(fp - 3) - i]` (arg 0 is the last pushed) | `stack[base - argc + i]` (arg 0 is the first pushed) | evaluate args in source order |
| `SEND`: the target pid is on top of the args | the target pid is below the args | no `SWAP` needed after `SPAWN` |
| `^tag` | `^tag/arity`, and SEND checks that the target owns it | a process only ever receives its own tags |
| `BLOCKED` / `SUSPENDED` (defined the same) | `WAITING` / `BLOCKED` | |
| "functions can not be called from the top level" | functions can be called, and can `AWAIT`, from anywhere | suspension keeps the whole stack |

<!---------------------------------------------------------------------------->
## Impact on the Other Docs
<!---------------------------------------------------------------------------->

Not updated yet. These need to follow once this design settles:

- INSTRUCTIONS.md
    - the Process, Async and Signal sections, per the summary above
- MODULES.md
    - the header gains `@state`, and tag arity (`^tag/n`)
    - `.init` / `.signal` replace `@public '.spawn'` / `'.despawn'`
    - executable format: init address, signal handler address, state
      count, tags with their arity
- ASSEMBLY.md
    - rewrite the greeter and ping-pong examples
- RUNTIME.md
    - the tick structure
    - `/main` is a module whose `.init` is `.main`, and it can have a
      header (it needs one to `AWAIT` anything)
- DATATYPES.md
    - PID (knows its module), TAG (module, index, arity), REPLY_ADDR, PENDING

<!---------------------------------------------------------------------------->
## Open Questions
<!---------------------------------------------------------------------------->

- **Timeouts.** `AWAIT ^r, &failed, ticks` is where timers come in (the
  parked TimerWheel), keyed by ticks, not wall-clock time.
- **Delegation and failure.** If a callee hands a request to a worker and
  then stops, the caller's `AWAIT` fails even though the worker may still
  reply. Erlang has the same edge case with monitors.
- **Correlation in run-to-completion style.** A reply handled through the
  loop has no easy way to be matched against a stored PENDING. Possibly a
  `MSG_MATCHES` op, `( pending -- bool )`.
- **Shared protocols.** Tags are owned by a module, so two modules cannot
  implement the same protocol. Moving tag ownership to a `:protocol` would
  allow polymorphic receivers.
- **Supervision.** `%RESTART`, restart strategies, and links (should a
  child die with its parent?).
- **Mailbox scan cost.** `AWAIT` rescans from the head on every wake.
  BEAM remembers a marker for the point where the `ref` was created, and
  we could do the same.
- **Message values and memory.** Message args are copied and treated as
  immutable. Once the memory model (`t/999-ideas.t`) lands, messages will
  need to deep-copy or share immutable data.
- **Exactly-once replies.** Not enforced. A callee can reply twice, or
  never.

<!---------------------------------------------------------------------------->
## Acceptance Tests
<!---------------------------------------------------------------------------->

These prove the convention once it is implemented, using hand-built
opcode arrays (`VM::Assembly::SimpleOpcodeBuilder`) before any assembler
syntax work:

1. **echo**, ported from AVM `t/001-echo.t`
2. **ping-pong**, ported from AVM `t/002-ping.t`, with tags instead of code
   addresses in the message body
3. **multiplier**: the example above prints `40`, and the dead-letter queue
   holds exactly the forged `^product(999)`
4. **multiplier with a dying adder**: the adder `STOP`s without replying,
   the failure cascades through each `AWAIT ... &failed`, `main` prints
   "the multiplier died", and every process is reaped
5. **multi-core**, ported from AVM `t/010` / `t/011`: the same results with
   1 or N cores
6. **deadlock**: two processes `AWAIT` each other, and the VM reports a
   deadlock instead of spinning

<!---------------------------------------------------------------------------->
