#lang racket/base
;; fan-in on places, racket-mc's: 100 senders, each a thread on the place
;; (one for each core) its number picks, each sending its number n/100
;; times on one place channel, which copies each, to the main place,
;; which sums them: many senders across cores, one receiver.
(require racket/place)

(define (sender to s m)
    (when (> m 0)
        (place-channel-put to s)
        (sender to s (- m 1))))

(define (senders ch)
    (define-values (to m b cores) (apply values (place-channel-get ch)))
    (define ts (for/list ([s (in-range 1 101)] #:when (= (modulo s cores) b)) (thread (lambda () (sender to s m)))))
    (for-each thread-wait ts))

(define (gather in n sum)
    (if (= n 0) sum (gather in (- n 1) (+ sum (place-channel-get in)))))

;; A place starts by instantiating this module, so the place is made here,
;; at the module's level, and the main submodule only calls this.
(define (start-senders) (place ch (senders ch)))

(module+ main
    (define n (string->number (vector-ref (current-command-line-arguments) 0)))
    (define cores (processor-count))
    (define-values (in out) (place-channel))
    (define ps (for/list ([b (in-range cores)]) (start-senders)))
    (for ([p ps] [b (in-naturals)]) (place-channel-put p (list out (quotient n 100) b cores)))
    (printf "~a\n" (gather in n 0)))
