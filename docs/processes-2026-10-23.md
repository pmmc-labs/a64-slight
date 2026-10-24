# Processes in lib/ and examples/, 2026-10-23

Every `fork` and `connect` in `lib/` and `examples/`, sorted into what
it is used as, for the proposal of tasks and actors in
[`PLAN.md`](PLAN.md) ("Tasks and actors"). Taken at `a24f3fe`.

96 sites: 68 `fork`, 28 `connect` (the 7 `connect/in` and `connect/out`
in airline.slight are its own functions, not sites).

Kinds:

- **A**: actor. A loop on `recv`; nobody takes a value from it.
- **A+r**: actor whose last value the root joins at the end (a summary).
- **T**: task. No `recv`; a value is joined, raced, monitored, or nothing.
- **W**: watcher. Waits for one `(:exit ...)` and acts; no value.
- **Hd**: hybrid, device. Owns a device, reads it, ends with a value joined.
- **Hc**: hybrid, conversation. Sends, waits for replies, ends with a value.

| Site | Kind | What |
|---|---|---|
| lib/ds.slight:28 | Hc | `ask`'s box: one reply |
| lib/ds.slight:34 | Hc | `ask-within`'s box |
| lib/ds.slight:59 | A | cell |
| lib/ds.slight:85 | A | dict |
| lib/ds.slight:118 | A | queue |
| lib/ds.slight:142 | A | channel |
| lib/exec.slight:12 | Hd | `exec/run` |
| lib/exec.slight:19 | Hd | `exec/run-within` |
| lib/fs.slight:12 | Hd | `slurp` |
| lib/fs.slight:23 | Hd | `spew` |
| lib/agent.slight:71 | A | the agent |
| lib/agent.slight:80 | Hc | `agent/ask` |
| lib/agent.slight:113 | Hc | a turn: reads a stream, value by monitor |
| lib/agent.slight:226 | T | a tool call, value by monitor |
| lib/http.slight:41 | Hd | `http/request` |
| lib/http.slight:51 | Hd | `http/request-within` |
| lib/http.slight:57 | T | `(fork (sleep ms))`, a timeout to race |
| lib/http.slight:80 | Hd | a stream's connection, value by monitor |
| lib/http.slight:81 | T | `(fork (raise reason))`, a failure made uniform |
| lib/http.slight:82 | W | `http/watch` |
| lib/http.slight:123 | A | listener |
| lib/http.slight:131 | A | a server connection (finite, no value) |
| lib/http.slight:413 | T | the handler, for fault isolation |
| lib/rpc.slight:74 | Hc | `rpc/peer` |
| lib/rpc.slight:92 | Hd | `rpc/system` reading `:system` |
| lib/rpc.slight:123 | Hc | `rpc/call` |
| lib/rpc.slight:150 | W | `rpc/stream`'s relay |
| lib/rpc.slight:194 | Hc | `rpc/connect`, waits for open |
| lib/rpc.slight:208 | A | connection (unix) |
| lib/rpc.slight:209 | A | connection (tcp) |
| lib/rpc.slight:213 | A | listener (unix) |
| lib/rpc.slight:214 | A | listener (tcp) |
| lib/rpc.slight:222 | Hc | `rpc/exec`, waits for open |
| lib/rpc.slight:226 | A | connection over a command |
| lib/rpc.slight:249 | A | accepted connection |
| lib/rpc.slight:265 | A | keeper |
| lib/rpc.slight:278 | T | on-open, value ignored |
| lib/rpc.slight:391 | T | a call from the other end, value by message |
| lib/rpc.slight:393 | W | `rpc/watch` |
| lib/tools.slight:120 | Hd | `tools/read` |
| lib/tools.slight:132 | Hd | `tools/save` |
| lib/tools.slight:548 | A | notes store |
| lib/tools.slight:565 | T | `tools/save` forked to catch its raise |
| lib/tools.slight:574 | T | `tools/read` forked to catch its raise |
| examples/active-objects.slight:56 | A | ActorRef |
| examples/airline.slight:371 | A | aircraft |
| examples/airline.slight:728 | Hc | `simulate`, a branch: desk ticks aircraft, gathers |
| examples/airline.slight:802 | Hd | `read-sexps` |
| examples/airline.slight:832 | Hc | `simulate`, the base |
| examples/airline.slight:1236 | Hc | `stand-in`, waits for its port |
| examples/boids.slight:288 | Hd | display on `:system`, joined at the end |
| examples/boids.slight:290 | A | bird |
| examples/boids.slight:293 | A | keys |
| examples/chat.slight:54 | Hd | keypress UI, joined |
| examples/coral.slight:125 | T | a share of the walks, `join/all` |
| examples/coral.slight:140 | A+r | painter |
| examples/divisions.slight:67 | Hd | keypress UI, joined |
| examples/etch.slight:138 | Hd | keypress UI, joined |
| examples/even-odd-actors.slight:45 | A | is-even? |
| examples/even-odd-actors.slight:46 | A | is-odd? |
| examples/fixed-tournament.slight:40 | A | player |
| examples/fixed-tournament.slight:41 | A | player |
| examples/fixed-tournament.slight:50 | Hc | game: serves, gathers two finals |
| examples/forest.slight:155 | A | bud |
| examples/forest.slight:167 | A+r | painter |
| examples/forest.slight:182 | A | bud |
| examples/game-of-life-actors.slight:41 | Hc | watch: asks each neighbour in turn |
| examples/game-of-life-actors.slight:113 | A | cell |
| examples/key-catcher.slight:36 | Hd | keypress UI, joined |
| examples/million-forks.slight:20 | A | test-actor |
| examples/million-forks.slight:25 | A | test-actor |
| examples/ping-pong-tournament.slight:37 | A | player |
| examples/ping-pong-tournament.slight:38 | A | player |
| examples/ping-pong-tournament.slight:50 | Hc | game |
| examples/ping-pong.slight:23 | A+r | ponger |
| examples/ping-pong.slight:24 | Hc | pinger: a rally, then its value |
| examples/pub-sub.slight:36 | A+r | subscriber |
| examples/pub-sub.slight:37 | A+r | subscriber |
| examples/pub-sub.slight:38 | A+r | broker |
| examples/ring-benchmark.slight:26 | A | test-actor |
| examples/ring-benchmark.slight:43 | A | test-actor |
| examples/sieve.slight:49 | A | end-of-chain |
| examples/sieve.slight:129 | A+r | painter |
| examples/sieve.slight:131 | A | end-of-chain |
| examples/simple-db-server.slight:37 | A | db-server |
| examples/snails.slight:39 | T | crawl, `join/all` and `join/race` |
| examples/tail-chase-game.slight:17 | T | a fading trail, fire and forget |
| examples/tail-chase-game.slight:41 | Hd | keypress UI, joined |
| examples/top.slight:144 | A | ponger |
| examples/top.slight:145 | A | consumer |
| examples/top.slight:146 | T | cruncher, never ends |
| examples/top.slight:146 | A | pinger |
| examples/top.slight:146 | T | producer, never ends |
| examples/top.slight:146 | T | hoarder, never ends |
| examples/top.slight:147 | Hd | `:system` display, joined |
| examples/top.slight:148 | A | keys |

## Totals

| Kind | lib | examples | all |
|---|---|---|---|
| A | 15 | 24 | 39 |
| A+r | 0 | 7 | 7 |
| W | 3 | 0 | 3 |
| T | 8 | 6 | 14 |
| Hd | 10 | 8 | 18 |
| Hc | 8 | 7 | 15 |
| | 44 | 52 | 96 |
