;; thread-ring in Swish, the Benchmarks Game's: 503 processes in a ring
;; pass a token on n times, one less each time; the one handed 0 says who
;; it is.

(define (ring-member id root)
    (receive [(next ,next) (relay id next root)]))

(define (relay id next root)
    (receive
        [0 (send root id)]
        [,n (send next (- n 1)) (relay id next root)]))

(define (range a b) (if (= a b) '() (cons a (range (+ a 1) b))))

(define me self)
(define ring (map (lambda (id) (spawn (lambda () (ring-member id me)))) (range 1 504)))
(for-each (lambda (p q) (send p `(next ,q))) ring (append (cdr ring) (list (car ring))))
(send (car ring) (string->number (car (command-line-arguments))))
(receive [,id (printf "~a\n" id)])
(exit 0)
