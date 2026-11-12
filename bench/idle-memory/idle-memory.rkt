#lang racket/base
;; idle-memory: n threads, all waiting for a message at once, then each
;; asked for 1 and ending; the answers summed.
(require racket/match)

(define (idle) (match (thread-receive) [(list 'ping from) (thread-send from 1)]))

(define (start n acc)
    (if (= n 0) acc (start (- n 1) (cons (thread idle) acc))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(define me (current-thread))
(for ([p (start n '())]) (thread-send p (list 'ping me)))
(printf "~a\n" (gather n 0))
