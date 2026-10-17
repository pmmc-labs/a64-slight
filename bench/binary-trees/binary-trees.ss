; binary-trees, single-threaded: a node is a pair of its children, a
; leaf's (() . ()).
(import (chezscheme))

(define (make d)
    (if (= d 0) (cons '() '()) (cons (make (- d 1)) (make (- d 1)))))

(define (check t)
    (if (null? (car t)) 1 (+ 1 (check (car t)) (check (cdr t)))))

(define (trees n d acc)
    (if (= n 0) acc (trees (- n 1) d (+ acc (check (make d))))))

(define (depths d top)
    (when (<= d top)
        (display (trees (expt 2 (+ (- top d) 4)) d 0))
        (newline)
        (depths (+ d 2) top)))

(define top (max 6 (string->number (cadr (command-line)))))
(display (check (make (+ top 1))))
(newline)
(define long-lived (make top))
(depths 4 top)
(display (check long-lived))
(newline)
