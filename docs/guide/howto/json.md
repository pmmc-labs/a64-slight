# Read and write JSON

<!-- code: ../code/howto-json.slight -->
```lisp
; args: docs/guide/code/data/events.ndjson
(pprint (json/parse "{\"a\": [1, 2.5], \"b\": null}"))
(pprint (json/parse "[1,]"))

; A field of an object, or :not-found.
(defun field (obj key)
    (match obj
        ((:object & fields) (lookup key fields))))

; Reads a file of JSON values, one after another, adding up "ms".
(defun total-ms (path)
    (match (open :fs/read path)
        ((:ok f)
            (send f (list :read :json))
            (total-loop f 0 ()))))

(defun total-loop (f ms users)
    (match (read f)
        ((:json _ event) (total-loop f (+ ms (field event "ms")) (cons (field event "user") users)))
        ((:eof _)        (list :object (list "total_ms" ms) (list "users" (reverse users))))))

(let summary (total-ms (car @ARGV)))
(tty/write (json/print summary) \n)
(field summary "total_ms")
```

The file it reads, `data/events.ndjson`, is a JSON value a line:

```
{"user": "ann", "action": "login", "ms": 120}
{"user": "bob", "action": "login", "ms": 340}
{"user": "ann", "action": "buy", "ms": 95, "item": "tea"}
```

<!-- output: ../code/howto-json.expected -->
```
(ok (object ("a" (1 2.5)) ("b" null)))
(error (bad-json 3))
{"total_ms":555,"users":["ann","bob","ann"]}
555
```

**JSON as slight values:** an array is a list, an object is
`(:object (key value) ...)` with string keys in order, `null` is
`:null`, `true` and `false` are `#true` and `#false`, and a number is an
integer if it was written as one and fits, a float otherwise. An object
is an association list after its tag, so `lookup` reads a field.

**From a string:** `(json/parse s)` gives `(:ok value)`, or
`(:error (:bad-json at))`, `at` being the byte where it went wrong.

**From a file or socket:** send the device `(list :read :json)`, and
each message is then `(:json f value)`, one top-level value at a time.
Whitespace between values doesn't matter, so NDJSON and values one
after another both work. Bad JSON closes the file after the values
before it: a task's next `read` gives `(:error f reason)`, and an actor
that connected ends with `(:error (:bad-json path))`. To go through one
huge array an element at a time, use `:json/items`.

**Writing:** `(json/print v)` gives compact JSON on one line, so it's
NDJSON as it is. Anything JSON can't hold (a symbol other than `:null`,
`#true` and `#false`, a pid, a function, `nan`) faults.

[How-to](README.md)
