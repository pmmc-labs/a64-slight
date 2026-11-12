#lang racket/base
;; latency (D238): how long a message waits for its turn while other
;; threads work. Ping k is due k ms after the pinger starts; till it's
;; due the pinger yields, (sleep 0), and looks at the clock again, and its
;; latency is from when it was due to when its answer came. Three phases:
;; idle; busy, beside a thread working out fib(30+s); and heap, beside one
;; holding 100s blocks of 10,000 integers and replacing the oldest 1,000s
;; times, yielding after each, its pinger started once the blocks are
;; made. Prints "# phase p50 p99 max n" for each, in microseconds, then
;; fib's result and the heap's checksum.
(require racket/match racket/list)

(define (now-us) (inexact->exact (floor (* 1000 (current-inexact-monotonic-milliseconds)))))

(define (responder)
    (match (thread-receive)
        [(list 'ping from) (thread-send from 'pong) (responder)]
        ['stop (void)]))

(define (pinger r t0 k last samples)
    (define due (+ t0 (* 1000 k)))
    (cond
        [(> k last) (thread-send r 'stop) samples]
        [(< (now-us) due) (sleep 0) (pinger r t0 k last samples)]
        [else (thread-send r (list 'ping (current-thread))) (answer r t0 k last samples due)]))

;; The worker says when it's done, and pings due after that don't count.
(define (answer r t0 k last samples due)
    (match (thread-receive)
        [(list 'done t) (answer r t0 k (quotient (- t t0) 1000) samples due)]
        ['pong (pinger r t0 (+ k 1) last (if (> k last) samples (cons (- (now-us) due) samples)))]))

;; A pinger in a thread of its own, which sends its samples to parent.
(define (pings parent last)
    (thread (lambda () (thread-send parent (list 'samples (pinger (thread responder) (now-us) 1 last '()))))))

(define (gather-samples) (match (thread-receive) [(list 'samples s) s]))

(define (report name samples)
    (define s (sort samples <))
    (define n (length s))
    (if (= n 0)
        (printf "# ~a 0 0 0 0\n" name)
        (printf "# ~a ~a ~a ~a ~a\n" name (list-ref s (quotient n 2)) (list-ref s (quotient (* n 99) 100)) (last s) n)))

(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (block b size) (range b (+ b size)))

;; The newest block first; the oldest goes as each new one comes.
(define (churn blocks i n size)
    (cond
        [(= i n) blocks]
        [else
            (define next (cons (block (+ 1000000 i) size) (take blocks (- (length blocks) 1))))
            (sleep 0)
            (churn next (+ i 1) n size)]))

(define s (string->number (vector-ref (current-command-line-arguments) 0)))
(define me (current-thread))

;; Two messages to me, the pinger's samples and the worker's result, in
;; whichever order they come: a mailbox here is read in order.
(define (both)
    (define a (thread-receive))
    (define b (thread-receive))
    (if (eq? (car a) 'samples) (values (cadr a) (cadr b)) (values (cadr b) (cadr a))))

(void (pings me 500))
(report "idle" (gather-samples))

(define p1 (pings me 1000000000))
(void (thread (lambda () (define v (fib (+ 30 s))) (thread-send p1 (list 'done (now-us))) (thread-send me (list 'fib v)))))
(define-values (busy v) (both))
(report "busy" busy)
(printf "~a\n" v)

(void (thread (lambda ()
    (define blocks (for/list ([j (in-range (* 100 s))]) (block j 10000)))
    (define p (pings me 1000000000))
    (define q (churn blocks 0 (* 1000 s) 10000))
    (thread-send p (list 'done (now-us)))
    (thread-send me (list 'heap (apply + (map car q)))))))
(define-values (heap sum) (both))
(report "heap" heap)
(printf "~a\n" sum)
