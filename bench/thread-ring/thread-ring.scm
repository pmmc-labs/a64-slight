;; thread-ring, the Benchmarks Game's: 503 threads in a ring pass a token
;; on n times, one less each time; the one handed 0 says who it is.
(declare (standard-bindings) (extended-bindings) (block))

(define (member id root)
    (relay id (cadr (thread-receive)) root))

(define (relay id next root)
    (let ((n (thread-receive)))
        (if (= n 0)
            (thread-send root id)
            (begin (thread-send next (- n 1)) (relay id next root)))))

(define me (current-thread))
(define ring (map (lambda (id) (thread-start! (make-thread (lambda () (member id me))))) (iota 503 1)))
(for-each (lambda (p q) (thread-send p (list 'next q))) ring (append (cdr ring) (list (car ring))))
(thread-send (car ring) (string->number (cadr (command-line))))
(display (thread-receive))
(newline)
