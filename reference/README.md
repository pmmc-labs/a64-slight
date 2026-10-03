# reference

Copies of files from earlier projects, kept here so a session doesn't need
those repositories. **Read-only.** Nothing here is built or tested. Code
that gets ported moves into the real source tree and is changed there.

Copied on 2026-10-03 from the `main` branches of
[pmmc-labs/ts-slight](https://github.com/pmmc-labs/ts-slight) (commit
`c0842ff`) and [pmmc-labs/ts-cpi](https://github.com/pmmc-labs/ts-cpi)
(commit `a756ab9`).

## ts-slight: the target user surface

| Path | Why it's here |
|---|---|
| `ts-slight/examples/*.slight` | The 27 examples. They define what slight should feel like; `docs/PLAN.md` has a table of what each needs to port. |
| `ts-slight/lib/Prelude.slight` | Prelude functions and their names (`map`, `grep`, `fold/l`, `range`, `assoc`, ...) |
| `ts-slight/lib/Builtins.slight` | Builtin names and behaviour |
| `ts-slight/lib/Test.slight` | The TAP-style test library (`run-tests`, `ok`, `is`, `diag`) to rebuild in step 6 |
| `ts-slight/docs/NOTES-language.md` | The language notes, including the builtin list. Parts are out of date for a64-slight (`if`, `when`, `case`, eval, `gensym`, short-circuit `and`/`or`). |
| `ts-slight/src/parser.ts`, `reader.ts` | The original reader, including how `when`/`case`/`cond` expanded |
| `ts-slight/src/terms.ts` | Semantics of `eq?` and the `pprint` format |
| `ts-slight/src/extensions.ts` | `keyEventToTerm` and `KEY_NAMES`: the key-event shape `:keypress` should send |

## ts-cpi: code to port

| Path | Why it's here |
|---|---|
| `ts-cpi/src/reader.ts` | A tokenizer and reader with source positions: the starting point for step 0. It imports `types.ts`, `values.ts` and `errors.ts`, copied alongside. Port it, don't import it. |
| `ts-cpi/tests/reader.test.ts` | Its tests (`node:test`) |
| `ts-cpi/CLAUDE.md` | ts-cpi's working rules; a64-slight's `CLAUDE.md` adapts them |
