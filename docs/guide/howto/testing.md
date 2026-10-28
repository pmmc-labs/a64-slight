# Test a program

<!-- code: ../code/howto-testing.slight -->
```lisp
(@include :test)

(defun slugify (s)
    (str/join "-" (str/split (str/lower (str/trim s)) " ")))

(run-tests (list
    (is (slugify "Hello World") "hello-world" "lower-cases, and joins with -")
    (is (slugify "  Tea  ") "tea" "trims")
    (is (slugify "a  b") "a-b" "squeezes spaces")
    (diag "slugify is here to keep the example short")))
```

<!-- output: ../code/howto-testing.expected -->
```
ok 1 - lower-cases, and joins with -
ok 2 - trims
not ok 3 - squeezes spaces
#          got: "a--b"
#     expected: "a-b"
# slugify is here to keep the example short
1..3
# Looks like you failed 1 test of 3.
#false
```

`(@include :test)` prints TAP, which any TAP harness reads (Perl's
`prove`, for one):

- `(is got expected name)` passes if the two are `eq?`, and shows both
  if not. `(ok test name)` passes if `test` is `#true`. `(diag msg)` is
  a comment.
- `(run-tests tests)` runs them in order, prints the plan, and gives
  `#true` if all passed, `#false` if not.
- The arguments are evaluated as the list is built, so a test can do
  work first, in a `do`.

**Laying it out:** keep the functions in a file of their own, with no
top-level forms but `defun`s and includes, and the tests in another that
includes it, as the tutorial's `tut-store.slight` and its tests are.
The functions can then be included by the program and by its tests.

**Processes:** test the pure functions directly. Test actors through
`ask`, from the top level, which is a task.

**Time:** run tests with `SLIGHT_CLOCK=virtual`, so timeouts and expiry
take no time and come out exact. Tutorial chapter 6 tests a store whose
keys expire this way.

**Races:** run the tests under a few `SLIGHT_SCHED` seeds. See
[Find a race](races.md).

[How-to](README.md)
