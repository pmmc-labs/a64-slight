#lang racket/base
;; payload: ping-pong with the same list of 100 integers in each message,
;; there and back. Racket's threads share one heap, so the list is passed
;; by reference, not copied. Prints n, and the list's sum as it ends up.
(require racket/match racket/list)

(define (pong)
    (match (thread-receive)
        [(list 'ping from data) (thread-send from (list 'pong data)) (pong)]
        ['stop (void)]))

(define (ping p i n data)
    (cond
        [(= i n) (thread-send p 'stop) data]
        [else
            (thread-send p (list 'ping (current-thread) data))
            (match (thread-receive) [(list 'pong back) (ping p (+ i 1) n back)])]))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(define data (ping (thread pong) 0 n (range 1 101)))
(printf "~a\n~a\n" n (apply + data))
