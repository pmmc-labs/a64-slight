;; payload in Swish: ping-pong with the same list of 100 integers in each
;; message, there and back. Swish's processes share one heap, so the list
;; is passed by reference, not copied. Prints n, and the list's sum as it
;; ends up.

(define (pong)
    (receive
        [(ping ,from ,data) (send from `(pong ,data)) (pong)]
        [stop 'stopped]))

(define (ping p i n data)
    (cond
        [(= i n) (send p 'stop) data]
        [else
            (send p `(ping ,self ,data))
            (receive [(pong ,back) (ping p (+ i 1) n back)])]))

(define (range a b) (if (= a b) '() (cons a (range (+ a 1) b))))

(define n (string->number (car (command-line-arguments))))
(define data (ping (spawn pong) 0 n (range 1 101)))
(printf "~a\n~a\n" n (apply + data))
(exit 0)
