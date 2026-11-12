#lang racket/base
;; parallel-fib on places, racket-mc's: a place for each core, each a VM
;; of its own, working out its share of the 64 fib(k)s and sending each
;; answer back on a place channel, which copies it; the answers summed.
(require racket/place)

(define (fib n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (worker ch)
    (define-values (to k b cores) (apply values (place-channel-get ch)))
    (for ([i (in-range 64)] #:when (= (modulo i cores) b)) (place-channel-put to (fib k))))

;; A place starts by instantiating this module, so the place is made here,
;; at the module's level, and the main submodule only calls this.
(define (start-worker) (place ch (worker ch)))

(module+ main
    (define k (string->number (vector-ref (current-command-line-arguments) 0)))
    (define cores (processor-count))
    (define-values (in out) (place-channel))
    (define ps (for/list ([b (in-range cores)]) (start-worker)))
    (for ([p ps] [b (in-naturals)]) (place-channel-put p (list out k b cores)))
    (printf "~a\n" (for/sum ([i (in-range 64)]) (place-channel-get in))))
