;; parallel-fib: 64 threads each work out fib(k), and their answers are
;; summed. Gambit's threads share one core.
(declare (standard-bindings) (extended-bindings) (block))

(define (fib n)
    (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define k (string->number (cadr (command-line))))
(define me (current-thread))
(for-each (lambda (i) (thread-start! (make-thread (lambda () (thread-send me (fib k)))))) (iota 64))
(display (gather 64 0))
(newline)
