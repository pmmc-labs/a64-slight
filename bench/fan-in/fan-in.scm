;; fan-in: 100 threads each send their number n/100 times to one, which
;; sums what it's sent: many senders, one mailbox.
(declare (standard-bindings) (extended-bindings) (block))

(define (sender to s m)
    (if (> m 0)
        (begin (thread-send to s) (sender to s (- m 1)))))

(define (gather n sum)
    (if (= n 0) sum (gather (- n 1) (+ sum (thread-receive)))))

(define n (string->number (cadr (command-line))))
(define me (current-thread))
(for-each (lambda (s) (thread-start! (make-thread (lambda () (sender me s (quotient n 100)))))) (iota 100 1))
(display (gather n 0))
(newline)
