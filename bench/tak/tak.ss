(import (chezscheme))

(define (tak x y z)
    (if (< y x)
        (tak (tak (- x 1) y z) (tak (- y 1) z x) (tak (- z 1) x y))
        z))

(define n (string->number (cadr (command-line))))
(display (tak (* 3 n) (* 2 n) n))
(newline)
