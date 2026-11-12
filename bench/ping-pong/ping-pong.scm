;; ping-pong: n round trips between two threads, each a message there and
;; one back, through the threads' mailboxes. Gambit's own benchmarks'
;; declarations: the standard procedures aren't redefined (still safe).
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
