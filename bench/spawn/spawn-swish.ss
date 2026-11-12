;; spawn in Swish: n processes started one after another, each sending its
;; number back and ending; their numbers summed.

(define (start i n me)
    (when (< i n)
        (spawn (lambda () (send me i)))
        (start (+ i 1) n me)))

(define (gather n acc)
    (if (= n 0) acc (receive [,i (gather (- n 1) (+ acc i))])))

(define n (string->number (car (command-line-arguments))))
(start 0 n self)
(printf "~a\n" (gather n 0))
(exit 0)
