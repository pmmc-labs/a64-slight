;; latency (D238) in Swish: how long a message waits for its turn while
;; other processes work. Ping k is due k ms after the pinger starts; till
;; it's due the pinger looks at the clock again. Swish has no yield: a
;; process is preempted once its quantum, 1 ms, is spent (checked every
;; 1,000 calls), so the pinger spins and the heap's churn runs on, each
;; preempted as any Swish process is. A ping's latency is from when it
;; was due to when its answer came. Three phases: idle; busy, beside a
;; process working out fib(30+s); and heap, beside one holding 100s blocks
;; of 10,000 integers and replacing the oldest 1,000s times, its pinger
;; started once the blocks are made. Prints "# phase p50 p99 max n" for
;; each, in microseconds, then fib's result and the heap's checksum.

(define (now-us)
    (let ([t (current-time 'time-monotonic)])
        (+ (* 1000000 (time-second t)) (quotient (time-nanosecond t) 1000))))

(define (responder)
    (receive
        [(ping ,from) (send from 'pong) (responder)]
        [stop 'stopped]))

(define (pinger r t0 k last samples)
    (let ([due (+ t0 (* 1000 k))])
        (cond
            [(> k last) (send r 'stop) samples]
            [(< (now-us) due) (pinger r t0 k last samples)]
            [else (send r `(ping ,self)) (answer r t0 k last samples due)])))

;; The worker says when it's done, and pings due after that don't count.
(define (answer r t0 k last samples due)
    (receive
        [(done ,t) (answer r t0 k (quotient (- t t0) 1000) samples due)]
        [pong (pinger r t0 (+ k 1) last (if (> k last) samples (cons (- (now-us) due) samples)))]))

;; A pinger in a process of its own, which sends its samples to parent.
(define (pings parent last)
    (spawn (lambda () (send parent `(samples ,self ,(pinger (spawn responder) (now-us) 1 last '()))))))

(define (samples p) (receive [(samples ,@p ,s) s]))

(define (report name samples)
    (let* ([s (sort < samples)] [n (length s)])
        (if (= n 0)
            (printf "# ~a 0 0 0 0\n" name)
            (printf "# ~a ~a ~a ~a ~a\n" name (list-ref s (quotient n 2)) (list-ref s (quotient (* n 99) 100)) (list-ref s (- n 1)) n))))

(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (block b size) (iota-from b size))
(define (iota-from b size) (if (= size 0) '() (cons b (iota-from (+ b 1) (- size 1)))))

(define (take ls k) (if (= k 0) '() (cons (car ls) (take (cdr ls) (- k 1)))))

;; The newest block first; the oldest goes as each new one comes.
(define (churn blocks i n size)
    (if (= i n)
        blocks
        (churn (cons (block (+ 1000000 i) size) (take blocks (- (length blocks) 1))) (+ i 1) n size)))

(define s (string->number (car (command-line-arguments))))
(define me self)
(report "idle" (samples (pings me 500)))

(define p1 (pings me 1000000000))
(spawn (lambda () (let ([v (fib (+ 30 s))]) (send p1 `(done ,(now-us))) (send me `(fib ,v)))))
(report "busy" (samples p1))
(receive [(fib ,v) (printf "~a\n" v)])

(define h (spawn (lambda ()
    (let* ([blocks (map (lambda (j) (block j 10000)) (iota (* 100 s)))]
           [p (pings me 1000000000)]
           [q (churn blocks 0 (* 1000 s) 10000)])
        (send p `(done ,(now-us)))
        (send me `(heap ,p ,(apply + (map car q))))))))
(receive [(heap ,p ,sum) (report "heap" (samples p)) (printf "~a\n" sum)])
(exit 0)
