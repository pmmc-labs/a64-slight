#lang racket/base
;; spawn: n threads started one after another, each sending its number
;; back and ending; their numbers summed.

(define (start i n me)
    (when (< i n)
        (thread (lambda () (thread-send me i)))
        (start (+ i 1) n me)))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(start 0 n (current-thread))
(printf "~a\n" (gather n 0))
