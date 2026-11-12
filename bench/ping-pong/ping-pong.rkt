#lang racket/base
;; ping-pong: n round trips between two threads, each a message there and
;; one back, through the threads' mailboxes.
(require racket/match)

(define (pong)
    (match (thread-receive)
        [(list 'ping from i) (thread-send from (list 'pong i)) (pong)]
        ['stop (void)]))

(define (ping p i n)
    (cond
        [(= i n) (thread-send p 'stop)]
        [else
            (thread-send p (list 'ping (current-thread) i))
            (match (thread-receive) [(list 'pong _) (ping p (+ i 1) n)])]))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(ping (thread pong) 0 n)
(printf "~a\n" n)
