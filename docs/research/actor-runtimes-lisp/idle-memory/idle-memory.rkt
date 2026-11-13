#lang racket/base
;; idle-memory: n threads, all waiting in thread-receive at once, then each
;; asked for 1 and ending; the answers summed.
(require racket/match)

(define (idle) (match (thread-receive) [(list 'ping from) (thread-send from 1)]))

(define n (string->number (vector-ref (current-command-line-arguments) 0)))
(define me (current-thread))
(for ([p (for/list ([i n]) (thread idle))]) (thread-send p (list 'ping me)))
(printf "~a\n" (for/sum ([i n]) (thread-receive)))
