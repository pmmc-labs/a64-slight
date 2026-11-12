#lang racket/base
;; idle-memory on places, racket-mc's: a place for each core, each
;; starting its share of the n threads waiting; once every place says its
;; share is waiting, each is told to go, asks its threads for 1 each, and
;; sends their sum on; the sums summed.
(require racket/place racket/match)

(define (idle) (match (thread-receive) [(list 'ping from) (thread-send from 1)]))

(define (start n acc)
    (if (= n 0) acc (start (- n 1) (cons (thread idle) acc))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define (keeper ch)
    (define-values (to n) (apply values (place-channel-get ch)))
    (define ps (start n '()))
    (place-channel-put to 'waiting)
    (place-channel-get ch)
    (define me (current-thread))
    (for ([p ps]) (thread-send p (list 'ping me)))
    (place-channel-put to (gather n 0)))

;; A place starts by instantiating this module, so the place is made here,
;; at the module's level, and the main submodule only calls this.
(define (start-keeper) (place ch (keeper ch)))

(module+ main
    (define n (string->number (vector-ref (current-command-line-arguments) 0)))
    (define k (processor-count))
    (define-values (in out) (place-channel))
    (define ps (for/list ([b (in-range k)]) (start-keeper)))
    (for ([p ps] [b (in-naturals)]) (place-channel-put p (list out (- (quotient (* (+ b 1) n) k) (quotient (* b n) k)))))
    (for ([b (in-range k)]) (place-channel-get in))
    (for ([p ps]) (place-channel-put p 'go))
    (printf "~a\n" (for/sum ([b (in-range k)]) (place-channel-get in))))
