#lang racket/base
;; spawn on places, racket-mc's: a place for each core, each starting its
;; share of the n threads one after another, each sending its number back
;; and ending, then sending their sum on; the sums summed.
(require racket/place)

(define (start i n me)
    (when (< i n)
        (thread (lambda () (thread-send me i)))
        (start (+ i 1) n me)))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define (share ch)
    (define-values (to lo hi) (apply values (place-channel-get ch)))
    (start lo hi (current-thread))
    (place-channel-put to (gather (- hi lo) 0)))

;; A place starts by instantiating this module, so the place is made here,
;; at the module's level, and the main submodule only calls this.
(define (start-share) (place ch (share ch)))

(module+ main
    (define n (string->number (vector-ref (current-command-line-arguments) 0)))
    (define k (processor-count))
    (define-values (in out) (place-channel))
    (define ps (for/list ([b (in-range k)]) (start-share)))
    (for ([p ps] [b (in-naturals)]) (place-channel-put p (list out (quotient (* b n) k) (quotient (* (+ b 1) n) k))))
    (printf "~a\n" (for/sum ([b (in-range k)]) (place-channel-get in))))
