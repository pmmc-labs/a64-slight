; deriv, Gabriel's, as in the R7RS benchmarks.
(import (chezscheme))

(define (deriv a)
    (cond
        ((not (pair? a)) (if (eq? a 'x) 1 0))
        ((eq? (car a) '+) (cons '+ (map deriv (cdr a))))
        ((eq? (car a) '-) (cons '- (map deriv (cdr a))))
        ((eq? (car a) '*) (list '* a (cons '+ (map (lambda (a) (list '/ (deriv a) a)) (cdr a)))))
        ((eq? (car a) '/) (list '- (list '/ (deriv (cadr a)) (caddr a))
                                   (list '/ (cadr a) (list '* (caddr a) (caddr a) (deriv (caddr a))))))
        (else (error 'deriv "no derivative" (car a)))))

(define (atoms a)
    (if (pair? a) (fold-left (lambda (n e) (+ n (atoms e))) 0 a) 1))

(define (run n e total last)
    (if (= n 0)
        (begin (display total) (newline) (display (atoms last)) (newline))
        (let ((d (deriv e)))
            (run (- n 1) e (+ total (length d)) d))))

(run (string->number (cadr (command-line))) '(+ (* 3 x x) (* a x x) (* b x) 5) 0 '())
