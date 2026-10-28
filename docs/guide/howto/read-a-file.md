# Read a file

Three ways, for three sizes of job.

<!-- code: ../code/howto-lines.slight -->
```lisp
; args: docs/guide/code/data/fog.txt
(@include :fs)

(let path (car @ARGV))

; A small file, whole.
(match (slurp path)
    ((:ok lines) (pprint (length lines))))

; A file of any size, a line at a time, from a task.
(defun words (line)
    (remove (lambda (w) (eq? w "")) (str/split line " ")))

(defun count-words (path)
    (match (open :fs/read path)
        ((:ok f)         (count-loop f 0))
        ((:error reason) (list :error reason))))

(defun count-loop (f n)
    (match (read f)
        ((:line _ s)       (count-loop f (+ n (length (words s)))))
        ((:eof _)          (list :ok n))
        ((:error _ reason) (list :error reason))))

(pprint (count-words path))
(pprint (count-words "no-such-file.txt"))

; The same from an actor, which connects to the file and takes its lines
; as messages.
(defun longest (best)
    (recv
        ((:open f)   (longest best))
        ((:line f s) (longest (if (> (str/len s) (str/len best)) s best)))
        ((:eof f)    best)))

(read (connect :fs/read path (longest "")))
```

<!-- output: ../code/howto-lines.expected -->
```
7
(ok 21)
(error (enoent "no-such-file.txt"))
(ok "over harbor and city")
```

**The whole file:** `(slurp path)`, from `(@include :fs)`, gives
`(:ok lines)`, every line without its newline, or
`(:error (name path))`. It's simplest, and holds the whole file at
once. `(spew path lines)` writes one.

**A line at a time, from a task:** `(open :fs/read path)` gives
`(:ok f)`, and each `(read f)` the next message: `(:line f s)`, then
`(:eof f)` at the end, or `(:error f reason)` if a read fails. Each
line is cut as it's asked for, so a file of any size takes the memory of
a line or so.
The error is a value: a task's device failing doesn't end the task.

**A line at a time, from an actor:** `(connect :fs/read path expr)`
starts an actor that owns the file, and sends it `(:open f)`, then the
lines as messages. The next line comes only when the actor waits in
`recv` again, so they never pile up. A file that can't be opened ends
the actor with `(:error (name path))`.

Paths are relative to the current directory. The `; args:` line at the
top gives the guide's test its argument; run by hand, pass a path.

## Variations

- **Not lines:** `(send f (list :read :chunks))` before the next `read`
  switches to `(:chunk f s)` pieces of up to 64 KB, and `(list :read n)`
  reads the next `n` bytes as one chunk. A line over 64 MB ends the
  reader with `:too-big`.
- **JSON or s-expressions:** `(list :read :json)` or `(list :read
  :sexp)`. See [JSON](json.md).
- **Writing:** `(open :fs/write path)` empties or creates the file, and
  `:fs/append` adds to it. Send it `(list :write x ...)`, and
  `(disconnect f)` when done. Tutorial chapter 7 keeps a log this way.

[How-to](README.md)
