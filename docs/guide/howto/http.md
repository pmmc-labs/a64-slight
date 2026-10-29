# Serve and call HTTP

<!-- code: ../code/howto-http.slight -->
```lisp
(@include :http)

(defun handler (req)
    (match (list (lookup :method req) (lookup :path req))
        ((:get "/hello")
            (list 200 (list (list "Content-Type" "text/plain"))
                (str "hello, " (lookup :query req))))
        ((:post "/sum")
            (match (json/parse (lookup :body req))
                ((:ok numbers) (list 200 (list (list "Content-Type" "application/json")) (json/print (sum numbers))))
                ((:error _)    (list 400 () "not JSON"))))
        (_
            (list 404 () "not found"))))

; A response's status and body.
(defun show (response)
    (match response
        ((:ok (status _ body)) (list status body))
        ((:error reason)       (list :error reason))))

(match (http/listen 0 handler)
    ((:ok (l port))
        (let base (str "http://127.0.0.1:" port))
        (pprint (show (http/get (str base "/hello?name=ann"))))
        (pprint (show (http/get (str base "/nope"))))
        (pprint (show (http/request :post (str base "/sum") () "[1, 2, 3.5]")))
        (pprint (show (http/request :post (str base "/sum") () "[1, 2,")))
        (kill l)))
:done
```

<!-- output: ../code/howto-http.expected -->
```
(200 "hello, name=ann")
(404 "not found")
(200 "6.5")
(400 "not JSON")
done
```

**A server** is a function from a request to a response.
`(http/listen port handler)`, from a task, listens on all interfaces and
gives `(:ok (l port))`. Port 0 lets the system pick, which is what a
test wants. `(kill l)` stops it. From an actor, `(http/serve port
handler)` does the same and sends `(:listening port)`.

- A request is a keyed list: `lookup` it for `:method` (`:get`,
  `:post`, ... as keywords), `:path`, `:query` (`""` if none),
  `:headers` (`(name value)` pairs, names in lower case) and `:body`.
- A response is `(status headers body)`. Content-Length and
  `Connection: close` are added for you.
- Each request runs the handler in a task of its own. If it faults or
  raises, the client gets a 500, and the server goes on. A handler can
  `call` an actor, or make requests of its own.

Matching on `(method path)` as a list is the router.

**A client:** `(http/get url)` and `(http/request method url headers
body)` give `(:ok (status headers body))`, or `(:error reason)`, such as
`(:econnrefused "127.0.0.1:1")`. `http/request-within` adds a timeout.
`(http/stream ...)` gives the body as it arrives, line by line or in
chunks, for streaming APIs such as a local model's.

**Not yet:** HTTPS (`:no-tls` till it goes through curl; a server sits
behind a proxy such as nginx, which ends TLS), keep-alive, and a Date
header. One request per connection.

[How-to](README.md)
