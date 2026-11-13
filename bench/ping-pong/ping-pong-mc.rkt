#lang racket/base
;; ping-pong on places, racket-mc's: a place for each core, each with a
;; ping-pong pair of threads doing its share of the n round trips, then
;; sending its count back; the counts summed.
(require racket/place racket/match)

(define (pong)
    (match (thread-receive)
        [(list 'ping from i) (thread-send from (list 'pong i)) (pong)]
        ['stop (void)]))

(define (ping p i n)
    (cond
        [(= i n) (thread-send p 'stop) n]
        [else
            (thread-send p (list 'ping (current-thread) i))
            (match (thread-receive) [(list 'pong _) (ping p (+ i 1) n)])]))

(define (pair ch)
    (define-values (to n) (apply values (place-channel-get ch)))
    (place-channel-put to (ping (thread pong) 0 n)))

;; A place starts by instantiating this module, so the place is made here,
;; at the module's level, and the main submodule only calls this.
(define (start-pair) (place ch (pair ch)))

(module+ main
    (define n (string->number (vector-ref (current-command-line-arguments) 0)))
    (define k (processor-count))
    (define-values (in out) (place-channel))
    (define ps (for/list ([b (in-range k)]) (start-pair)))
    (for ([p ps] [b (in-naturals)]) (place-channel-put p (list out (- (quotient (* (+ b 1) n) k) (quotient (* b n) k)))))
    (printf "~a\n" (for/sum ([b (in-range k)]) (place-channel-get in))))
