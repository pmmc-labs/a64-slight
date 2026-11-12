;; parallel-fib in Swish: 64 processes each work out fib(k), and their
;; answers are summed. Swish's processes share one core.

(define (fib n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (gather n acc)
    (if (= n 0) acc (receive [,i (gather (- n 1) (+ acc i))])))

(define k (string->number (car (command-line-arguments))))
(define me self)
(do ([i 0 (+ i 1)]) ((= i 64)) (spawn (lambda () (send me (fib k)))))
(printf "~a\n" (gather 64 0))
(exit 0)
