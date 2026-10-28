# Run a command

<!-- code: ../code/howto-commands.slight -->
```lisp
(@include :exec)

; A command run to its end: its status, and all it printed.
(pprint (exec/run (list "echo" "hello")))
(pprint (exec/run (list "sh" "-c" "echo out; echo err >&2; exit 3")))
(pprint (exec/run (list "no-such-command")))

; A command given input, and read a line at a time.
(defun sorted (lines)
    (match (open :exec (list "sort"))
        ((:ok p)
            (map (lambda (line) (send p (list :write line \n))) lines)
            (send p (list :eof))
            (collect p ()))))

(defun collect (p out)
    (match (read p)
        ((:line _ s)        (collect p (cons s out)))
        ((:eof _)           (collect p out))
        ((:exited _ status) (list status (reverse out)))))

(sorted (list "pear" "apple" "fig"))
```

<!-- output: ../code/howto-commands.expected -->
```
(ok (0 "hello\n"))
(ok (3 "out\nerr\n"))
(error (enoent "no-such-command"))
(0 ("apple" "fig" "pear"))
```

**To its end:** `(exec/run cmd)`, from `(@include :exec)`, runs a
command with no input and waits for it. It gives
`(:ok (status output))`, the output being stdout and stderr together, in
the order they came; or `(:error reason)` if it couldn't start.
`(exec/run-within ms cmd)` kills it after `ms` milliseconds, and its
status is then `(:timeout ms)`.

**A command is a list of strings**, its name and its arguments. There's
no shell unless you ask for one, `(list "sh" "-c" "...")`, so nothing
in an argument is expanded or needs quoting. The name is looked for in
`PATH`.

**With input, or a line at a time:** `(open :exec cmd)` from a task gives
`(:ok p)`. `(send p (list :write x ...))` writes to its input, and
`(send p (list :eof))` closes it. A command that reads its input waits
until you close it. `read` gives its output as `(:line p s)`, then
`(:eof p)`, then `(:exited p status)`. From an actor,
`(connect :exec cmd expr)` sends the same as messages.

**Stopping it:** `(disconnect p)`, or the owner ending, kills the
command and its process group. So does the program ending, however it
ends, so no command outlives the program.

On the virtual clock, time stands still while a command runs, so
`exec/run-within` has no limit there.

[How-to](README.md)
