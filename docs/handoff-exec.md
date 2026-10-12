# Next session: propose `:exec`, then build it (step 15)

Start a new session on pmmc-labs/a64-slight with: "Read
`docs/handoff-exec.md` and do what it says." (Or paste this file.)

## Where things are

Step 15, agents, has begun, before TLS (D180), with a local model over
plain HTTP. Done in the last session: `http/stream` (D181, D182), a
monitor that goes when either process ends (D183), and a chat agent on
ollama's `/api/chat` with a terminal to talk to it (D184,
`lib/agent.slight`, `examples/chat.slight`). Next: `:exec`, a device for
running a command, which a tool needs; then reading the environment (a
builtin, to propose); then tools as messages.

Nothing about `:exec` is decided beyond PLAN.md's sketch, so **propose
before building** (CLAUDE.md): the first job is a proposal for Stevan,
with options and a recommendation for each point below. Build only once
he has agreed, and record the outcome as D185.

The container needs the toolchain first (CLAUDE.md, "Toolchain"):
`apt-get update`, `apt-get install -y gcc-aarch64-linux-gnu qemu-user`,
then `npm install`. Check it with `t/run.sh t/000-int.slight` and
`TARGET=x86_64 t/run.sh t/000-int.slight`. valgrind is there too, and
works on the runtime despite its own stacks: `valgrind
--error-exitcode=9 ./prog` found a use-after-free in the last session
(D183). Use it on any change to the runtime.

## Read, in this order

1. `docs/PLAN.md`, step 15: the `:exec` sketch, and what's still to
   settle.
2. `docs/DECISIONS.md`: D184 (the agent tools will plug into), D158 to
   D160 (how a device cuts its messages, `(:read how)`, `:too-big`),
   D130, D136, D138 and D139 (devices, errno's names, handing a device
   over, sockets), D170 (everything from the world goes through
   `runtime/replay.c`), D171 (`:system` lists devices).
3. `docs/LANGUAGE.md`: Devices (Files, Sockets), and "(@include :agent)".
4. `runtime/process.c`, the devices: `rt_device_t` and its modes
   (`DEV_TCP`, `DEV_LISTEN`, `DEV_SYSTEM`), `wait_for` (the one
   `select()`), `sockets_waiting` and `idle` (what counts as something
   that can still happen, and the virtual clock), `socket_event`, `fill`
   and `cut`, `shut` and `close_now`. And `runtime/io.h`, the calls that
   bring something in.

## Points to settle in the proposal

PLAN.md's sketch: `(connect :exec (list "ls" "-l") expr)` runs the
command; its owner hears `(:open p)`, then the output as other devices
give theirs, then `(:exit p status)`. A command is a list of arguments,
with no shell unless asked for (`(list "sh" "-c" cmd)`).

- **The last message's name.** `(:exit p status)` has the shape of
  `monitor`'s `(:exit pid result)`, so a process that owns a command and
  monitors anything (as the agent's turn monitors the agent) would take
  one for the other. Another word, such as `(:exited p status)`, or
  `(:eof p)` and then `(:status p n)`?
- **What status is:** the exit code, and what a signal gives (`(:signal
  :term)`?).
- **stderr:** merged with stdout, its own messages (`(:err p line)`), or
  passed through to the program's stderr. A tool's caller (a model) will
  want to see errors.
- **stdin:** `(:write ...)` to the device, as a socket takes it; and how
  the input is closed while the output is still read, since `disconnect`
  closes the whole device (the child would get EOF, then SIGPIPE?).
- **Ending it:** what `disconnect`, or the owner ending, does to a child
  still running (SIGTERM, then SIGKILL after a while? its process group?),
  and when the child is reaped (`waitpid` with `WNOHANG` once its pipes
  close, or a `SIGCHLD` and a pipe to wake `select()`). A timeout is the
  caller's own `after`, as for streams.
- **Starting it:** `posix_spawnp` (the same on macOS and Linux, and no
  `fork` of a runtime with big stacks mapped) with file actions for the
  pipes; a command that isn't found ends the owner with `(:error (:enoent
  "ls"))`, as a file that isn't there does (D136).
- **Replay:** a child's output and its status come from the world, so
  they go through `replay.c`, and under `SLIGHT_REPLAY` the command
  mustn't run at all.
- **The virtual clock:** a child's pipes are waited for in `select()` as
  sockets are; `idle` must count a running child as something that can
  still happen.
- **Limits a tool runs under** (PLAN.md): probably none beyond a timeout
  and `:too-big` for now; say so.

## Tests

Golden tests from `t/225` on, on the virtual clock, with output worked
out by hand. Use commands that behave the same on macOS and Linux:
`sh -c` with `printf`, `cat` and `exit n`; macOS's `/bin/sh` is bash 3.2
in POSIX mode. Nothing that takes real time (`sleep`). Cover: output as
lines and as chunks, stdin through `cat`, a status that isn't 0, stderr,
a command that isn't found, killing the owner while the child runs (the
child must end, and be reaped), and replay (`t/replay.sh` has the way).
Run them under a few `SLIGHT_SCHED` seeds and `SLIGHT_GC_STRESS`, and
valgrind.

## Loose ends from the last session

- The chat hasn't met a real ollama: the cloud container can't fetch a
  model (registry.ollama.ai and Hugging Face are blocked by its network
  policy), so it was checked against a Python stand-in for `/api/chat`.
  Stevan: `ollama pull llama3.2`, `node bin/slightc.ts -o chat
  examples/chat.slight`, `./chat`. A "thinking" model (qwen3,
  deepseek-r1) may send its thinking with an empty `content`, which the
  agent skips, so the chat would sit silent while it thinks.
- Golden tests 217 to 224 haven't run on macOS yet. Their output doesn't
  depend on how loopback splits or delays the bytes (D182), but watch
  them there.
- `t/222` depends on the run queue's first-in-first-out order, as
  `t/126` does, so it fails under `SLIGHT_SCHED`; only the order its
  watchers report in changes. `t/223` needs `SLIGHT_GC_STRESS` of about
  1,000 to finish in time.

## Finishing

- `docs/LANGUAGE.md`: `:exec` under Devices; `docs/DECISIONS.md`: D185;
  `docs/DESIGN.md`; `docs/PLAN.md`, step 15; `CLAUDE.md`'s status.
- `make test` and `make check` pass, on both targets.
- Work on a new branch, commit, and push it. Don't open a pull request;
  Stevan will.
- Delete this file in the same branch.
