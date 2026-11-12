;; payload: ping-pong with the same list of 100 integers in each message,
;; there and back. Gambit's threads share one heap, so the list is passed
;; by reference, not copied. Prints n, and the list's sum as it ends up.
(declare (standard-bindings) (extended-bindings) (block))

(define (pong)
    (let ((m (thread-receive)))
        (if (eq? (car m) 'ping)
            (begin (thread-send (cadr m) (list 'pong (caddr m))) (pong)))))

(define (ping p i n data)
    (if (= i n)
        (begin (thread-send p (list 'stop)) data)
        (begin
            (thread-send p (list 'ping (current-thread) data))
            (ping p (+ i 1) n (cadr (thread-receive))))))

(define n (string->number (cadr (command-line))))
(define data (ping (thread-start! (make-thread pong)) 0 n (iota 100 1)))
(display n)
(newline)
(display (apply + data))
(newline)
