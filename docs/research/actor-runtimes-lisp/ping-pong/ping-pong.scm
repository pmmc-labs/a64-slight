;; ping-pong: n round trips between two Gambit threads, each a message to
;; the other's mailbox (thread-send, thread-receive). Safe code, with the
;; declarations Gambit's own benchmarks use.
(declare (standard-bindings) (extended-bindings) (block))

(define (pong)
    (let ((m (thread-receive)))
        (if (eq? (car m) 'ping)
            (begin (thread-send (cadr m) (list 'pong (caddr m))) (pong)))))

(define (ping p i n)
    (if (= i n)
        (thread-send p (list 'stop))
        (begin
            (thread-send p (list 'ping (current-thread) i))
            (thread-receive)
            (ping p (+ i 1) n))))

(define n (string->number (cadr (command-line))))
(ping (thread-start! (make-thread pong)) 0 n)
(display n)
(newline)
