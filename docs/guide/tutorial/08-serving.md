# 8. Serving the store

So far the store has served the program it's in. This chapter puts it on
a socket, so other programs can use it, and people too, by typing at
it.

Sockets are devices, like files: `(connect :tcp "host:port" expr)`
connects to one, and `(connect :tcp/listen port expr)` listens on one,
each connection arriving as a message. A protocol could be written on
top of those, line by line. `(@include :rpc)` is one already written:
calls between programs, in s-expressions, over a connection.

## Handlers

The other end calls the store by name, with arguments. Each name is a
**handler**: a name, what it does, its parameters with their types, and
a function of the arguments.

<!-- code: ../code/tut-serve.slight -->
```lisp
; A handler's answer is its value, and a raise is a failure, which is
; what the caller gets as (:ok v) and (:error reason).
(defun answer (reply)
    (match reply
        ((:ok value)     value)
        ((:error reason) (raise reason))))

(defun handlers (s)
    (list
        (rpc/handler "get" "A key's value." (list (list "key" :string "The key"))
            (lambda (key) (answer (call s (list :get key)))))
        (rpc/handler "put" "Gives a key a value."
            (list (list "key" :string "The key") (list "value" :any "Its value"))
            (lambda (key value) (answer (call s (list :put key value)))))
        (rpc/handler "del" "Takes a key out." (list (list "key" :string "The key"))
            (lambda (key) (answer (call s (list :del key)))))
        (rpc/handler "incr" "Adds one to a key's value." (list (list "key" :string "The key"))
            (lambda (key) (answer (call s (list :incr key)))))
        (rpc/handler "keys" "Every key." ()
            (lambda () (answer (call s (list :keys)))))))
```

Each call runs its handler in a task of its own, so calls from many
connections run at once. Each is a task, so it can `call` the store,
which, as ever, takes their commands one at a time. The library checks
the arguments against the types before the function runs, and the
handler's value is the answer. A raise is a failure, so `answer` turns
the store's `(:error reason)` back into one, and the caller sees the
same `(:ok ...)` or `(:error ...)` the store gave.

## A server

A server is the store from chapter 7, and a listener:

```
(@include "tut-serve.slight")

(let s (start "store.log"))
(match (rpc/listen "127.0.0.1:7317" (handlers s))
    ((:ok (l at)) (tty/write "listening at " at \n)))
```

`(rpc/listen where handlers)` listens at `"host:port"`, and gives
`(:ok (l at))`: the listener's pid, and the port it got. The program
then runs until it's stopped, since an open socket keeps a program
running.

`127.0.0.1` takes connections from this machine alone. There's no
authentication, so that's where a store like this belongs. To reach it
from elsewhere, use ssh, or listen at a path instead,
`(rpc/listen "./store.sock" ...)`, a Unix socket whose permissions say
who may connect.

## Calling it

From another slight program, `rpc/connect` and `rpc/call`:

<!-- code: ../code/tut-8-calls.slight -->
```lisp
; A program calling the store.
(defun program (where)
    (match (rpc/connect where ())
        ((:ok conn)
            (show (rpc/call conn "put" (list "apple" 3)))
            (show (rpc/call conn "incr" (list "apple")))
            (show (rpc/call conn "get" (list "plum")))
            (show (rpc/call conn "get" (list "apple" "pear")))
            (rpc/close conn))))
```

```
(ok 3)
(ok 4)
(error not-found)
(error (arity "get" 1))
```

`(rpc/connect where handlers)` gives `(:ok conn)` once it's connected.
The `()` is what this end offers the other: calls go both ways, but the
store calls nobody. `(rpc/call conn name args)` waits for the answer.
The last call has an argument too many, and the library said so
without bothering the store.

## People

The messages are s-expressions a line, so a person can take part with
`nc 127.0.0.1 7317`. Here's a session: what the person types follows
`>`, and what the store says follows `<`.

<!-- output: ../code/tut-8-calls.expected -->
```
(ok 3)
(ok 4)
(error not-found)
(error (arity "get" 1))
< (helo rpc 1)
> (helo rpc 1)
> (call 1 keys)
< (done 1 ("apple"))
> (call 2 put pear 5)
< (done 2 5)
> (call 3 incr pear)
< (done 3 6)
> (call 4 get plum)
< (fail 4 not-found)
> (call 5 get)
< (fail 5 (arity "get" 1))
> (call 6 help)
< (done 6 (("get" "A key's value." (("key" string "The key"))) ("put" "Gives a key a value." (("key" string "The key") ("value" any "Its value"))) ("del" "Takes a key out." (("key" string "The key"))) ("incr" "Adds one to a key's value." (("key" string "The key"))) ("keys" "Every key." ()) ("help" "What can be called here: each call's name, what it does, and its parameters." ())))
the log:
(put "apple" 3)
(incr "apple")
(put "pear" 5)
(incr "pear")
done
```

(That's the whole of the test program's output: the four calls above
first, then the session, and then the log. In the test, the person is
played by a small client in the same program, which sends each line
and waits for the answers, as a person would.)

- Each end says `(helo rpc 1)` first.
- `(call id name arg ...)` calls `name`. The id is the caller's,
  anything at all, and the answer carries it back:
  `(done id value)` or `(fail id reason)`.
- A `:string` parameter takes a bare word as well as a quoted string,
  so `pear` is `"pear"`. It's for people typing.
- `help` is on every connection, and says what can be called, from the
  handlers' own descriptions.

The log is the store's from chapter 7, so everything called over the
socket is kept, and a server started again on the same file has it all.

## Where the store has got to

Over eight chapters, the table has gone from a list to a server:

- a pure function, `run-command`, from a table and a command to the
  next table and a reply, tested by calling it (chapters 1, 2 and 6);
- an actor around it, which owns the table and takes commands one at a
  time, so there are no locks (3);
- tasks that ask it, and the `recv` rule, which says where waiting can
  go (4);
- errors as replies, faults that end only the store, and a supervisor
  that starts it again (5);
- expiry on timers, tested on the virtual clock (6);
- a log, so a restart loses nothing (7);
- and handlers, so other programs and people can call it (8).

None of it needed a lock, a mutable variable, or a `try`. Each needed a
process in the right place.

From here, [`docs/LANGUAGE.md`](../../LANGUAGE.md) has the rest:
commands (`:exec`), HTTP (`(@include :http)`), JSON, the `:system`
device that shows every process, and streams and calls back over
`rpc`. [`examples/`](../../../examples) has bigger programs, and
`simple-db-server` is a cousin of this one.

---

Previous: [7. Keeping the store in a file](07-files.md)
