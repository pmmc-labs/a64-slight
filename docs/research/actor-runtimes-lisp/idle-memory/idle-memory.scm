;; idle-memory: n Gambit threads, all waiting in thread-receive at once,
;; then each asked for 1 and ending; the answers summed.
(declare (standard-bindings) (extended-bindings) (block))

(define (idle) (let ((m (thread-receive))) (thread-send (cadr m) 1)))

(define (start n acc)
    (if (= n 0) acc (start (- n 1) (cons (thread-start! (make-thread idle)) acc))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define n (string->number (cadr (command-line))))
(for-each (lambda (p) (thread-send p (list 'ping (current-thread)))) (start n '()))
(display (gather n 0))
(newline)
