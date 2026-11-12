#lang racket/base
;; fan-in: 100 threads each send their number n/100 times to one, which
;; sums what it's sent: many senders, one mailbox.

(define (sender to s m)
    (when (> m 0)
        (thread-send to s)
        (sender to s (- m 1))))

(define (gather n sum)
    (if (= n 0) sum (gather (- n 1) (+ sum (thread-receive)))))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(define me (current-thread))
(for ([s (in-range 1 101)]) (thread (lambda () (sender me s (quotient n 100)))))
(printf "~a\n" (gather n 0))
