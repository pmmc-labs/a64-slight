;; idle-memory in Swish: n processes, all waiting for a message at once,
;; then each asked for 1 and ending; the answers summed.

(define (idle) (receive [(ping ,from) (send from 1)]))

(define (start n acc)
    (if (= n 0) acc (start (- n 1) (cons (spawn idle) acc))))

(define (gather n acc)
    (if (= n 0) acc (receive [,i (gather (- n 1) (+ acc i))])))

(define n (string->number (car (command-line-arguments))))
(define me self)
(for-each (lambda (p) (send p `(ping ,me))) (start n '()))
(printf "~a\n" (gather n 0))
(exit 0)
