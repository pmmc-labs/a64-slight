;; ping-pong in Swish (Erlang's processes on Chez): n round trips between
;; two processes, each a message there and one back.

(define (pong)
    (receive
        [(ping ,from ,i) (send from `(pong ,i)) (pong)]
        [stop 'stopped]))

(define (ping p i n)
    (cond
        [(= i n) (send p 'stop) n]
        [else
            (send p `(ping ,self ,i))
            (receive [(pong ,_) (ping p (+ i 1) n)])]))

(printf "~a\n" (ping (spawn pong) 0 (string->number (car (command-line-arguments)))))
(exit 0)
