#lang racket/base
;; parallel-fib: 64 threads each work out fib(k), and their answers are
;; summed. Racket's threads share one core; racket-mc's places don't.

(define (fib n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define k (string->number (vector-ref (current-command-line-arguments) 0)))
(define me (current-thread))
(for ([i (in-range 64)]) (thread (lambda () (thread-send me (fib k)))))
(printf "~a\n" (gather 64 0))
