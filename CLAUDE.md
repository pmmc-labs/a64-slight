# a64-slight

slight (an immutable, actor-based Lisp) compiled ahead of time to native
AArch64, on a small actor runtime in C and assembly. The compiler is
TypeScript for now and should self-host later.

## Status

**Design agreed, nothing built yet.** The next step is step 0 of
[`docs/PLAN.md`](docs/PLAN.md). Update this section as steps land.

## Read first, in this order

1. [`docs/DESIGN.md`](docs/DESIGN.md): the language, runtime and compiler
   as agreed. **(open)** marks proposals that haven't been confirmed.
2. [`docs/PLAN.md`](docs/PLAN.md): the build order and the example-port
   table.
3. [`docs/DECISIONS.md`](docs/DECISIONS.md): why, and what was turned down.
   Check it before "improving" something; it may have been decided against.
4. [`docs/BACKGROUND.md`](docs/BACKGROUND.md): AVM, VM3, the spike, prior
   art. Optional.

Reference material (read-only, never built): [`reference/`](reference/README.md)
holds ts-slight's examples and libraries (the target user surface) and the
ts-cpi reader to port. [`spike/`](spike/) holds the AArch64 spike, which
works and has the runtime pieces to borrow.

## Working rules

- **Aggressively simple** is the main design rule. When a feature needs
  machinery, say so and offer to cut or change it.
- **Propose before building** any change to language semantics, a new
  special form or builtin, a change to the `recv` rule, or anything marked
  (open). Give options and a recommendation. Record the outcome in
  `docs/DECISIONS.md` (who decided, why, what was rejected) and update
  `docs/DESIGN.md`.
- Keep a working compiler with passing tests at the end of every step
  (Ghuloum). Each step adds golden tests.
- Verify a reported misbehavior against an independent implementation
  before calling it a bug.
- When editing with scripts, assert that each replacement matched.
- Test timing uses a virtual clock; real time is never used in tests.

## Code style

- **4-space indent everywhere**: TypeScript, `.slight`, C, assembly
  operands, JSON, and code blocks in Markdown.
- slight code indents 4 spaces per open parenthesis:

  ```lisp
  (defun sum-to (n acc)
      (cond
          ((== n 0) acc)
          (#true    (sum-to (- n 1) (+ acc n)))))
  ```

- The compiler is **slight-shaped**: pure functions over immutable
  s-expressions, recursion over loops, association lists for environments,
  no classes. It will be ported to slight line by line.
- In C headers shared with assembly, prefix every name `RT_`/`rt_` (macOS's
  `<stdlib.h>` pulls in `<sys/wait.h>`, which has a `P_PID`).
- Comments explain why, not what. Match the density of the spike's code.

## Toolchain

- **macOS on Apple Silicon:** Xcode's clang, Node ≥ 22.18. Everything runs
  natively.
- **x86-64 Linux (this is how cloud sessions develop):** cross-compile with
  clang and run under qemu:

  ```
  apt-get install -y gcc-aarch64-linux-gnu qemu-user   # sysroot + qemu-aarch64
  # clang and lld must also be installed (clang-18 works)
  ```

  The spike's Makefile shows the flags:
  `clang --target=aarch64-linux-gnu --sysroot=/usr/aarch64-linux-gnu -fuse-ld=lld -static`,
  and run binaries with `qemu-aarch64`. Timings under qemu are meaningless.
- Check the setup with `make -C spike/aarch64 test`. It should print five
  `ok` lines.
- Use clang's integrated assembler, not GNU `as` (it rejects some of the
  macros).
- Don't use `-Werror`; a different Apple clang version shouldn't break the
  build.

## Commands

None yet. Step 0 adds `npm test`, `npm run check` (`tsc --noEmit`), `make`,
`make test`, and `node bin/slightc.ts file.slight -o out`. Record them here.
