#lang racket/base
;; thread-ring, the Benchmarks Game's: 503 threads in a ring pass a token
;; on n times, one less each time; the one handed 0 says who it is.
(require racket/match racket/list)

(define (member id root)
    (match (thread-receive) [(list 'next next) (relay id next root)]))

(define (relay id next root)
    (match (thread-receive)
        [0 (thread-send root id)]
        [n (thread-send next (- n 1)) (relay id next root)]))

(define me (current-thread))
(define ring (for/list ([id (in-range 1 504)]) (thread (lambda () (member id me)))))
(for ([p ring] [q (append (cdr ring) (list (car ring)))]) (thread-send p (list 'next q)))
(thread-send (car ring) (string->number (vector-ref (current-command-line-arguments) 0)))
(printf "~a\n" (thread-receive))
