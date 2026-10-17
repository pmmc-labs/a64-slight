; nqueens, Gabriel's, as in the R7RS benchmarks.
(import (chezscheme))

(define (ok? row dist placed)
    (or (null? placed)
        (and (not (= (car placed) (+ row dist)))
             (not (= (car placed) (- row dist)))
             (ok? row (+ dist 1) (cdr placed)))))

; x the rows still to try, y those passed over for this column, z those placed.
(define (try-it x y z)
    (if (null? x)
        (if (null? y) 1 0)
        (+ (if (ok? (car x) 1 z) (try-it (append (cdr x) y) '() (cons (car x) z)) 0)
           (try-it (cdr x) (cons (car x) y) z))))

(define (one-to n)
    (let loop ((i n) (l '()))
        (if (= i 0) l (loop (- i 1) (cons i l)))))

(display (try-it (one-to (string->number (cadr (command-line)))) '() '()))
(newline)
