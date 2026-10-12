# Next session: build `http/stream` (D181)

Start a new session on pmmc-labs/a64-slight with: "Read
`docs/handoff-http-stream.md` and do what it says." (Or paste this file.)

## Where things are

Step 15, agents, is next, before TLS (D180): a local model over plain
HTTP first. The first thing it needs is an HTTP body streamed to its
caller as it arrives. The shape is decided (D181); nothing is built.

The container needs the toolchain first (CLAUDE.md, "Toolchain"):
`apt-get install -y gcc-aarch64-linux-gnu qemu-user`, then `npm install`.
Check it with `t/run.sh t/000-int.slight` and `TARGET=x86_64 t/run.sh
t/000-int.slight`.

Read, in this order:

1. `docs/DECISIONS.md`: D181 (the shape), D180 (why it's next), D172 to
   D174 (HTTP as built), D158 (a device cuts its next message when its
   owner waits again), D136 (a device that fails ends its owner).
2. `docs/LANGUAGE.md`, the section "(@include :http)".
3. `lib/http.slight`, the client: `http/request`, `http/exchange`,
   `http/head`, `http/body`, the body readers (`http/length`,
   `http/chunk-size` through `http/trailers`, `http/to-eof`), `http/done`
   and `http/fail`.
4. The HTTP golden tests, `t/204` to `t/213`, and their raw server and
   client, `t/data/http-raw.slight`.

## The task

Add `(http/stream method url headers body how)` to `lib/http.slight`, as
D181 says:

- It gives the stream's pid at once: the process that owns the
  connection, so `(kill s)` closes it.
- The process that called it is sent `(:head s status headers)`, then the
  body as it comes, `(:chunk s bytes)` when `how` is `:chunks` or
  `(:line s text)` when it's `:lines`, then `(:eof s)`. If anything fails,
  `(:error s reason)` comes in their place, with the reasons
  `http/request` gives: `(:bad-http what)`, `(:bad-url url)`, `:no-tls`,
  `:closed`, or the device's own, such as `(:econnrefused "host:port")`.
- Chunked encoding is undone on the way, so the pieces are the body's.
- `http/request` stays as it is. The two share the head and framing code
  rather than each having a copy.

Settle these while building, and record them under D181 (or as D182):

- a line's `\r`: drop one, as the head's lines do (D174), I'd say;
- a last line with no newline still comes;
- a reply with no body (HEAD, 1xx, 204, 304) is `:head` and then `:eof`.

If something bigger turns up, such as a change to the runtime or the
language, stop and ask before building it (CLAUDE.md, "Propose before
building").

Watch for:

- A device that fails ends its owner at once (D136), so the stream's own
  process can't send `(:error s reason)` for a refused connection or a
  reset. Something has to see it end and tell the caller: a small process
  that monitors it, say. Decide what the caller hears after its own
  `(kill s)`: nothing, I'd suggest, since it asked for it.
- In `:lines`, a line can be split across chunks of the chunked encoding,
  and across the device's reads; and one chunk can hold several lines.
- `:chunks` gives what the device gives, de-chunked; don't promise sizes.
- A line needs a limit, as the head has (64 KB), so a server that never
  sends a newline can't grow a buffer without end.

## Tests

Golden tests from `t/217` on, on the virtual clock, with their expected
output worked out by hand, never copied from what the program printed:

- Extend `t/data/http-raw.slight` (or add a helper beside it) with a
  server that sends a reply in pieces, with `after` between them, so a
  test can show that the pieces arrive before the end, and in order.
- Chunked JSON lines, as ollama sends them; lines split across chunk
  boundaries, and chunks holding several lines; `:chunks`; a body with a
  Content-Length, and one that runs to the close; HEAD and 204; a short
  body; a refused connection; a cancel by `kill`, after which nothing more
  comes and the server sees its connection close.
- Run the new tests under a few `SLIGHT_SCHED` seeds, and with
  `SLIGHT_GC_STRESS`.

## Docs, and finishing

- `docs/LANGUAGE.md`: `http/stream` in "(@include :http)", and "bodies
  streamed in pieces" taken off its "Not yet".
- `docs/DECISIONS.md`: what was settled while building.
- `docs/PLAN.md`, step 15, and `CLAUDE.md`'s status: streamed bodies done;
  next, `:exec`.
- `make test` and `make check` pass, on both targets.
- Work on a new branch, commit, and push it. Don't open a pull request;
  I'll do that.
- Delete this file in the same branch.
