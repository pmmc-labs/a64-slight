;; fan-in in Swish: 100 processes each send their number n/100 times to
;; one, which sums what it's sent: many senders, one mailbox.

(define (sender to s m)
    (when (> m 0)
        (send to s)
        (sender to s (- m 1))))

(define (gather n sum)
    (if (= n 0) sum (receive [,s (gather (- n 1) (+ sum s))])))

(define n (string->number (car (command-line-arguments))))
(define me self)
(do ([s 1 (+ s 1)]) ((> s 100)) (let ([s s]) (spawn (lambda () (sender me s (quotient n 100))))))
(printf "~a\n" (gather n 0))
(exit 0)
