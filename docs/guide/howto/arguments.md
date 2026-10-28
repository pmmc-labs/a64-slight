# Read arguments and the environment

<!-- code: ../code/howto-args.slight -->
```lisp
; args: --name Ann 3
; env: GREETING=Hello

; The word after name in args, or default.
(defun option (name args default)
    (match args
        ((flag value & rest) (if (eq? flag name) value (option name (cdr args) default)))
        (_                   default)))

(pprint @ARGV)
(let name (option "--name" @ARGV "world"))
(let times (str->int (car (reverse @ARGV))))
(let greeting (match (lookup "GREETING" %ENV)
    (:not-found "Hi")
    (g          g)))

(dotimes 0 times (lambda (i) (tty/write greeting ", " name "!" \n)))
(lookup "NO_SUCH_VARIABLE" %ENV)
```

Run as `GREETING=Hello ./greet --name Ann 3` (the `; args:` and
`; env:` lines are how the guide's test runs it):

<!-- output: ../code/howto-args.expected -->
```
("--name" "Ann" "3")
Hello, Ann!
Hello, Ann!
Hello, Ann!
not-found
```

- **`@ARGV`** is the arguments after the program's name, a list of
  strings, `()` if there are none. A number arrives as a string:
  `str->int` makes it one.
- **`%ENV`** is the environment, a list of `(name value)`, which
  `lookup` reads. A name that isn't set is `:not-found`, and one set to
  nothing is `""`.
- **Only the top level sees them.** They're its parameters, not
  globals: pass what a function needs to it. A `defun` that names
  either is a compile error. So a secret in the environment reaches only
  the code it's handed to.

A command run with `:exec` gets the same environment. `(list "env"
"NAME=value" "cmd" ...)` changes it for one command.

[How-to](README.md)
