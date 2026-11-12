;; spawn: n threads started one after another, each sending its number
;; back and ending; their numbers summed.
(declare (standard-bindings) (extended-bindings) (block))

(define (start i n me)
    (if (< i n)
        (begin
            (thread-start! (make-thread (lambda () (thread-send me i))))
            (start (+ i 1) n me))))

(define (gather n acc)
    (if (= n 0) acc (gather (- n 1) (+ acc (thread-receive)))))

(define n (string->number (cadr (command-line))))
(start 0 n (current-thread))
(display (gather n 0))
(newline)
