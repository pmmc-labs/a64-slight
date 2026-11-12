;; latency (D238): how long a message waits for its turn while other
;; threads work. Ping k is due k ms after the pinger starts; till it's
;; due the pinger yields and looks at the clock again, and its latency is
;; from when it was due to when its answer came. Three phases: idle;
;; busy, beside a thread working out fib(30+s); and heap, beside one
;; holding 100s blocks of 10,000 integers and replacing the oldest 1,000s
;; times, yielding after each, its pinger started once the blocks are
;; made. Prints "# phase p50 p99 max n" for each, in microseconds, then
;; fib's result and the heap's checksum. A thread's value is its result,
;; which thread-join! gives.
(declare (standard-bindings) (extended-bindings) (block))

(define (now-us) (inexact->exact (floor (* 1e6 (real-time)))))

(define (responder)
    (let ((m (thread-receive)))
        (if (eq? (car m) 'ping)
            (begin (thread-send (cadr m) 'pong) (responder)))))

(define (pinger r t0 k last samples)
    (let ((due (+ t0 (* 1000 k))))
        (cond
            ((> k last) (thread-send r (list 'stop)) samples)
            ((< (now-us) due) (thread-yield!) (pinger r t0 k last samples))
            (else (thread-send r (list 'ping (current-thread))) (answer r t0 k last samples due)))))

;; The worker says when it's done, and pings due after that don't count.
(define (answer r t0 k last samples due)
    (let ((m (thread-receive)))
        (if (pair? m)
            (answer r t0 k (quotient (- (cadr m) t0) 1000) samples due)
            (pinger r t0 (+ k 1) last (if (> k last) samples (cons (- (now-us) due) samples))))))

(define (pings last)
    (thread-start! (make-thread (lambda () (pinger (thread-start! (make-thread responder)) (now-us) 1 last '())))))

;; Gambit 4.9.3 has no sort, so a merge sort.
(define (merge a b)
    (cond
        ((null? a) b)
        ((null? b) a)
        ((< (car b) (car a)) (cons (car b) (merge a (cdr b))))
        (else (cons (car a) (merge (cdr a) b)))))

(define (msort s)
    (if (or (null? s) (null? (cdr s)))
        s
        (let ((h (quotient (length s) 2)))
            (merge (msort (take s h)) (msort (list-tail s h))))))

(define (report name samples)
    (let* ((s (msort samples)) (n (length s)))
        (for-each display
            (if (= n 0)
                (list "# " name " 0 0 0 0\n")
                (list "# " name " " (list-ref s (quotient n 2)) " " (list-ref s (quotient (* n 99) 100)) " " (last s) " " n "\n")))))

(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (block b size) (iota size b))

;; The newest block first; the oldest goes as each new one comes.
(define (churn blocks i n size)
    (if (= i n)
        blocks
        (let ((next (cons (block (+ 1000000 i) size) (take blocks (- (length blocks) 1)))))
            (thread-yield!)
            (churn next (+ i 1) n size))))

(define s (string->number (cadr (command-line))))
(report "idle" (thread-join! (pings 500)))

(define p1 (pings 1000000000))
(define w1 (thread-start! (make-thread (lambda () (let ((v (fib (+ 30 s)))) (thread-send p1 (list 'done (now-us))) v)))))
(report "busy" (thread-join! p1))
(display (thread-join! w1))
(newline)

(define h (thread-start! (make-thread (lambda ()
    (let* ((blocks (map (lambda (j) (block j 10000)) (iota (* 100 s))))
           (p (pings 1000000000))
           (q (churn blocks 0 (* 1000 s) 10000)))
        (thread-send p (list 'done (now-us)))
        (list (thread-join! p) (apply + (map car q))))))))
(define hr (thread-join! h))
(report "heap" (car hr))
(display (cadr hr))
(newline)
